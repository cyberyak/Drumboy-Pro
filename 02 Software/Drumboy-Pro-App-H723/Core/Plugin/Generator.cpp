#include "Generator.h"

#ifdef DRUMBOY_PLUGINS

#include <stdarg.h>

#include "Controller.h"
#include "Manager.h"
#include "PatternGen.h"
#include "Plugin.h"

#ifndef GEN_FAST_SECTION
#define GEN_FAST_SECTION __attribute__((section(".DTCMRAM")))
#endif

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint8_t  kGenTracks                           = kLayerLibrarySize;
const uint8_t  kKeypadLayer                         = 0;
const uint8_t  kKeypadLeft                          = 1;
const uint8_t  kKeypadRight                         = 2;
const uint16_t kGenTextX                            = 20;
const uint16_t kGenLine1Y                           = 6;                // Parameters (where the CPU meter line is)
const uint16_t kGenLine2Y                           = 16;               // Selected track / status / keys
const uint16_t kGenTextWidth                        = 920;
const uint16_t kGenMarkerX                          = 6;                // Left margin beside the track rows
const uint32_t kGenStatusMs                         = 4000;
const uint32_t kGenRedrawMs                         = 500;
const uint8_t  kGenFillChoices[]                    = {0, 2, 4, 8};     // Phrase fill every N bars
const uint8_t  kGenEvolveChoices[]                  = {0, 4, 8};        // EVOLVE every N bars
const uint8_t  kGenEvolveTracks                     = 2;                // Tracks re-rolled per EVOLVE
const uint8_t  kGenHistorySize                      = 8;
const uint16_t kGenSongStepsMax                     = 12 * 48;          // 12 bars of 12/4

// File extension block: [payload][crc32][payload length]["CYBX"] after the stock EOF marker
const char     kGenBlockMagic[4]                    = {'C', 'Y', 'B', 'X'};
const uint8_t  kGenBlockVersion                     = 1;
const uint16_t kGenBlockMaxPayload                  = 32768;

// clang-format on

/* Controller Access (friend of Controller) -------------------------*/

struct PluginAccess {
    static Layer& layer(Controller& c, uint8_t i) { return c.layerLibrary[i]; }
    static uint8_t beatsPerBar(Controller& c) { return c.rhythm.measure; }
    static uint8_t bars(Controller& c) { return c.rhythm.bar; }
    static uint16_t songInterval(Controller& c) { return c.songInterval; }
    static uint8_t activeBank(Controller& c) { return c.activeSongBank; }
    static bool recording(Controller& c) { return c.recordActive; }
    static void recalculate(Controller& c, uint8_t layer, uint8_t bank) {
        c.layerSong_calculateLastActiveBeatNum(layer, bank);
        c.layerSong_calculatePlayBeatNum(layer, bank, c.playInterval);
    }
    static void drawSong(Controller& c, uint8_t layer, uint8_t bank) { c.lcd_drawSong(layer, bank); }
    static void setFill(Controller& c, uint8_t layer, bool on) { c.layerInst_setFill(layer, on); }
};

extern Controller controller;                                           // main.cpp (file hooks run before the plugin host starts)

/* Step Meta (per generated beat) -----------------------------------*/
// Probability and velocity the stock beat format can't hold. Keyed by beat slot
// and validated against the beat's start tick, so beats edited by hand afterwards
// simply play normally. Saved in the File's extension block.

struct StepMeta {
    uint16_t tick;
    uint8_t probability;                                                // 0 = not generated
    uint8_t gain;                                                       // Volume multiplier x100
};

static StepMeta meta[kGenTracks][kBankLibrarySize][kBeatLibrarySize] GEN_FAST_SECTION;

/* Undo (one level: the bank as it was before the last change) -----*/

static Bank undoBanks[kGenTracks] GEN_FAST_SECTION;
static StepMeta undoMeta[kGenTracks][kBeatLibrarySize] GEN_FAST_SECTION;
static bool undoFill[kGenTracks];
static uint8_t undoBank = 0;
static bool undoValid = false;

/* Settings, History, Page State ------------------------------------*/

enum GenKnob : uint8_t { KNOB_STYLE, KNOB_DENSITY, KNOB_VARIATION, KNOB_SWING, KNOB_HUMANIZE, KNOB_DYNAMICS, KNOB_PROBABILITY, KNOB_RATCHET };

struct GenSettings {
    GenParams params;
    uint32_t salt[kGenTracks];                                          // Per-track re-roll offset
    uint8_t fillChoice;
};

static GenSettings settings = {{STYLE_TECHNO, 60, 30, 50, 20, 60, 40, 20, 4, 1, 0}, {}, 2};
static uint8_t evolveChoice = 0;
static bool locked[kGenTracks];
static int8_t roleOverride[kGenTracks];                                // -1 = automatic
static GenRole detected[kGenTracks];

static GenSettings history[kGenHistorySize];
static uint8_t historyCount = 0;                                        // Entries stored
static uint8_t historyNewest = 0;                                       // Ring index of the newest entry
static uint8_t historyBack = 0;                                         // 0 = newest, 1 = one older, ...

static bool open = false;
static bool clearPending = false;
static bool fillHeld = false;
static bool dirty = false;
static uint32_t lastDrawTick = 0;
static uint8_t selected = 0;
static uint8_t markerRow = 0xFF;
static char status[112] = "";
static bool statusError = false;
static uint32_t statusUntil = 0;

static uint16_t evolveBarCount = 0;
static uint32_t evolveRandom = 0x9E3779B9u;

static GenStep steps[kGenMaxSteps];
static uint32_t occupied[(kGenSongStepsMax + 31) / 32];
static uint32_t gateRandom = 0x2545F491u;
static bool stateReady = false;

static void ensureState() {
    // DTCM is not zeroed at boot; set up once, by whichever runs first (boot-time File load or plugin start)
    if (stateReady) return;
    memset(meta, 0, sizeof(meta));
    for (uint8_t i = 0; i < kGenTracks; i++) {
        roleOverride[i] = -1;
        locked[i] = false;
    }
    stateReady = true;
}

/* Helpers ----------------------------------------------------------*/

static uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

static GenRole roleOf(uint8_t track) {
    return (roleOverride[track] >= 0) ? (GenRole)roleOverride[track] : detected[track];
}

static bool isBackbone(GenRole role) {
    return (role == ROLE_KICK) || (role == ROLE_SNARE) || (role == ROLE_CLAP);
}

static void setStatus(bool error, const char* format, ...) __attribute__((format(printf, 2, 3)));
static void setStatus(bool error, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(status, sizeof(status), format, args);
    va_end(args);
    statusError = error;
    statusUntil = HAL_GetTick() + kGenStatusMs;
    dirty = true;
}

static void detectRoles(Controller& c) {
    for (uint8_t i = 0; i < kGenTracks; i++) {
        Layer& layer = PluginAccess::layer(c, i);
        detected[i] = gen_detectRole(layer.instLoadedData.folderName, layer.instLoadedData.fileName, layer.sampleLoadedData.fileName, i);
    }
}

static void markOccupied(uint16_t tick) {
    uint16_t step = (tick + kGenTicksPerStep / 2) / kGenTicksPerStep;
    if (step < kGenSongStepsMax) occupied[step >> 5] |= (1u << (step & 31));
}

static void buildOccupied(Controller& c, uint8_t bank, uint16_t regenerateMask) {
    // Kick / snare / clap tracks that are NOT being regenerated guide the percussion with their current beats
    memset(occupied, 0, sizeof(occupied));
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if ((regenerateMask & (1u << i)) || !isBackbone(roleOf(i))) continue;
        Bank& b = PluginAccess::layer(c, i).bankLibrary[bank];
        for (int16_t k = 0; k <= b.lastActiveBeatNum; k++) markOccupied(b.beatLibrary[k].getStartInterval());
    }
}

static uint16_t unlockedMask() {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (!locked[i]) mask |= (1u << i);
    }
    return mask;
}

/* Writing Tracks ---------------------------------------------------*/

static void writeTrack(Controller& c, uint8_t track, uint8_t bank, const GenStep* list, uint8_t count) {
    /// @brief Replaces a track's beats in one bank with generated steps (no quantize: swing/humanize stay),
    /// ratchets as stock BASIC_2/3/4 fills within one 16th, ghosts as stock GHOST beats.

    Layer& layer = PluginAccess::layer(c, track);
    Bank& b = layer.bankLibrary[bank];
    uint16_t song = PluginAccess::songInterval(c);
    bool anyRatchet = false;

    uint32_t primask = __get_PRIMASK();
    __disable_irq();  // The play timer must not see a half-written track

    for (uint8_t k = 0; k < kBeatLibrarySize; k++) {
        b.resetBeat(k);
        meta[track][bank][k] = {0, 0, 0};
    }
    // Bank::resetBeat() deactivates the beat before looking up its fill owner, so it never frees the
    // fill slot; the whole track is rewritten here, so free them all (else ratchets run out of slots)
    for (BeatFill& fill : b.beatFillLibrary) fill.reset();
    b.lastActiveBeatNum = -1;
    b.playBeatNum = -1;

    uint8_t written = 0;
    for (uint8_t k = 0; (k < count) && (written < kBeatLibrarySize); k++) {
        uint16_t start = list[k].tick;
        if (start >= song) break;
        uint16_t next = ((k + 1 < count) && (list[k + 1].tick < song)) ? list[k + 1].tick : song;
        if (next <= start) continue;

        uint8_t pattern = 0;  // FLAT
        float base = 1.0f;
        uint16_t end = next;
        if (list[k].ratchet) {
            pattern = (list[k].ratchet == 2) ? 3 : (list[k].ratchet == 3) ? 4 : 5;  // BASIC_2 / BASIC_3 / BASIC_4
            end = (next - start > kGenTicksPerStep) ? start + kGenTicksPerStep : next;
            anyRatchet = true;
        } else if (list[k].velocity < 60) {
            pattern = 1;  // GHOST (stock plays it at 50%)
            base = 0.5f;
        }

        b.setBeat(written, start, end);
        b.setBeatFill(written, 0, pattern, 0, 0);

        float gain = (list[k].velocity / 127.0f) / base;
        if (gain > 2.5f) gain = 2.5f;
        meta[track][bank][written] = {start, list[k].probability, (uint8_t)(gain * 100.0f)};
        written++;
    }
    PluginAccess::recalculate(c, track, bank);

    __set_PRIMASK(primask);

    if (anyRatchet && !layer.fill) PluginAccess::setFill(c, track, true);  // Ratchet repeats only sound with F on
    // Redraw the lane unless a plugin page (manager / show mode) owns that screen area; it restores the lanes itself
    if ((bank == PluginAccess::activeBank(c)) && !pluginHost_isScreenCaptured()) PluginAccess::drawSong(c, track, bank);
}

static void snapshot(Controller& c, uint8_t bank) {
    for (uint8_t i = 0; i < kGenTracks; i++) {
        Layer& layer = PluginAccess::layer(c, i);
        undoBanks[i] = layer.bankLibrary[bank];
        memcpy(undoMeta[i], meta[i][bank], sizeof(undoMeta[i]));
        undoFill[i] = layer.fill;
    }
    undoBank = bank;
    undoValid = true;
}

static void undo(Controller& c) {
    if (!undoValid) {
        setStatus(true, "NOTHING TO UNDO");
        return;
    }
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (uint8_t i = 0; i < kGenTracks; i++) {
        Layer& layer = PluginAccess::layer(c, i);
        layer.bankLibrary[undoBank] = undoBanks[i];
        memcpy(meta[i][undoBank], undoMeta[i], sizeof(undoMeta[i]));
        PluginAccess::recalculate(c, i, undoBank);
    }
    __set_PRIMASK(primask);

    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (PluginAccess::layer(c, i).fill != undoFill[i]) PluginAccess::setFill(c, i, undoFill[i]);
        if (undoBank == PluginAccess::activeBank(c)) PluginAccess::drawSong(c, i, undoBank);
    }
    undoValid = false;
    setStatus(false, "UNDONE - BANK %u IS BACK TO HOW IT WAS", undoBank + 1);
}

static uint8_t regenerate(Controller& c, uint16_t mask, const uint32_t* mutation) {
    /// @brief Regenerates the tracks in 'mask' (kick/snare/clap first so percussion can avoid them).
    /// 'mutation' (per track, may be null) re-rolls per-bar details only (EVOLVE).

    uint8_t bank = PluginAccess::activeBank(c);
    uint8_t stepsPerBar = PluginAccess::beatsPerBar(c) * 4;
    uint8_t bars = PluginAccess::bars(c);
    buildOccupied(c, bank, mask);

    uint8_t generated = 0;
    for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint8_t i = 0; i < kGenTracks; i++) {
            GenRole role = roleOf(i);
            if (!(mask & (1u << i)) || (isBackbone(role) != (pass == 0))) continue;

            GenParams p = settings.params;
            p.fillEvery = kGenFillChoices[settings.fillChoice];
            p.seed ^= settings.salt[i] * 0x9E3779B9u;
            p.mutation = (mutation != nullptr) ? mutation[i] : 0;

            uint8_t n = gen_generate(role, p, stepsPerBar, bars, i, occupied, steps);
            writeTrack(c, i, bank, steps, n);
            if (isBackbone(role)) {
                for (uint8_t k = 0; k < n; k++) markOccupied(steps[k].tick);
            }
            generated++;
        }
    }
    return generated;
}

/* History ----------------------------------------------------------*/

static void historyPush() {
    historyNewest = (historyCount == 0) ? 0 : (historyNewest + 1) % kGenHistorySize;
    history[historyNewest] = settings;
    if (historyCount < kGenHistorySize) historyCount++;
    historyBack = 0;
}

static void historyGo(Controller& c, int8_t direction) {
    // direction -1 = older (LEFT), +1 = newer (RIGHT)
    if (historyCount == 0) {
        setStatus(true, "NO HISTORY YET - GENERATE FIRST");
        return;
    }
    int16_t back = (int16_t)historyBack - direction;
    if ((back < 0) || (back >= historyCount)) {
        setStatus(true, (direction < 0) ? "OLDEST PATTERN IN HISTORY" : "NEWEST PATTERN IN HISTORY");
        return;
    }
    historyBack = (uint8_t)back;
    settings = history[(historyNewest + kGenHistorySize - historyBack) % kGenHistorySize];

    snapshot(c, PluginAccess::activeBank(c));
    regenerate(c, unlockedMask(), nullptr);
    setStatus(false, "HISTORY %u/%u: %s SEED %lu", historyCount - historyBack, historyCount, kGenStyleNames[settings.params.style],
              (unsigned long)settings.params.seed);
}

/* Actions ----------------------------------------------------------*/

static void generateAll(Controller& c, bool newSeed) {
    if (newSeed) {
        settings.params.seed = (settings.params.seed * 1103515245u + 12345u + HAL_GetTick()) % 10000u;
        memset(settings.salt, 0, sizeof(settings.salt));
    }
    uint8_t bank = PluginAccess::activeBank(c);
    snapshot(c, bank);
    uint8_t n = regenerate(c, unlockedMask(), nullptr);
    historyPush();
    setStatus(false, "GENERATED %u TRACKS (%s, SEED %lu) INTO BANK %u - FILE > SAVE TO KEEP, CLEAR = UNDO", n,
              kGenStyleNames[settings.params.style], (unsigned long)settings.params.seed, bank + 1);
}

static void rerollSelected(Controller& c) {
    if (locked[selected]) {
        setStatus(true, "TRACK %02u IS LOCKED - SELECT UNLOCKS IT", selected + 1);
        return;
    }
    settings.salt[selected] = nextRandom(evolveRandom) | 1u;
    snapshot(c, PluginAccess::activeBank(c));
    regenerate(c, (uint16_t)(1u << selected), nullptr);
    historyPush();
    setStatus(false, "RE-ROLLED TRACK %02u (%s) - CLEAR = UNDO", selected + 1, kGenRoleNames[roleOf(selected)]);
}

static void evolve(Controller& c) {
    /// @brief Re-rolls the per-bar details of a few unlocked tracks (base groove stays), while playing.

    uint16_t candidates = unlockedMask();
    uint16_t mask = 0;
    uint32_t mutation[kGenTracks] = {};
    for (uint8_t n = 0; (n < kGenEvolveTracks) && candidates; n++) {
        uint8_t pick = nextRandom(evolveRandom) % kGenTracks;
        for (uint8_t k = 0; k < kGenTracks; k++) {
            uint8_t i = (pick + k) % kGenTracks;
            if (candidates & (1u << i)) {
                mask |= (1u << i);
                candidates &= ~(1u << i);
                mutation[i] = nextRandom(evolveRandom) | 1u;
                break;
            }
        }
    }
    if (mask == 0) return;
    regenerate(c, mask, mutation);
    if (open) setStatus(false, "EVOLVED %s%s", kGenRoleNames[roleOf(__builtin_ctz(mask))], (mask & (mask - 1)) ? " + 1 MORE TRACK" : "");
}

/* Drawing ----------------------------------------------------------*/

static void drawMarker(Lcd& lcd, uint8_t row, bool on) {
    if (row >= kGenTracks) return;
    lcd.setForeColor(on ? CYAN : BLACK);
    lcd.fillRect(kGenMarkerX, kImageLayerY[row] + 8, 8, 14);
}

static void drawLines(Controller& c) {
    Lcd& lcd = c.lcd;
    char text[140];
    const GenParams& p = settings.params;

    lcd.setFont(FONT_05x07);
    lcd.setAlignment(LEFT);
    lcd.setBackColor(BLACK);
    lcd.clearRect(kGenTextX, kGenLine1Y - 1, kGenTextWidth, 19);

    // 1. PARAMETERS (kept short: the File menu's build label sits at the right end of this line)
    char fill[8], evo[8];
    (kGenFillChoices[settings.fillChoice] == 0) ? snprintf(fill, sizeof(fill), "OFF") : snprintf(fill, sizeof(fill), "%u", kGenFillChoices[settings.fillChoice]);
    (kGenEvolveChoices[evolveChoice] == 0) ? snprintf(evo, sizeof(evo), "OFF") : snprintf(evo, sizeof(evo), "%u", kGenEvolveChoices[evolveChoice]);
    snprintf(text, sizeof(text), "GEN %s  DENS %u  VAR %u  SWING %u  HUM %u  DYN %u  PROB %u  RATCH %u  FILL %s  EVOLVE %s  SEED %lu",
             kGenStyleNames[p.style], p.density, p.variation, p.swing, p.humanize, p.dynamics, p.probability, p.ratchet, fill, evo,
             (unsigned long)p.seed);
    lcd.setForeColor(CYAN);
    lcd.drawText(text, strlen(text), kGenTextX, kGenLine1Y);

    // 2. STATUS, OR SELECTED TRACK + KEYS
    if (HAL_GetTick() < statusUntil) {
        lcd.setForeColor(statusError ? RED : YELLOW);
        lcd.drawText(status, strlen(status), kGenTextX, kGenLine2Y);
    } else {
        snprintf(text, sizeof(text), "TRK %02u %s %s%s | GLOBAL GEN  PASTE NEW  MUTE TRK  L/R HIST  CLEAR UNDO  SELECT LOCK  COPY ROLE  SONG FILL  REC EVOLVE",
                 selected + 1, kGenRoleNames[roleOf(selected)], (roleOverride[selected] >= 0) ? "SET" : "AUTO", locked[selected] ? " LOCK" : "");
        lcd.setForeColor(WHITE);
        lcd.drawText(text, strlen(text), kGenTextX, kGenLine2Y);
    }

    // 3. TRACK MARKER
    if (markerRow != selected) {
        drawMarker(lcd, markerRow, false);
        markerRow = selected;
    }
    drawMarker(lcd, markerRow, true);
}

/* File Extension Block ---------------------------------------------*/
// Payload: version, settings, salts, locks, role overrides, then one 7-byte record per
// generated beat (track, bank, beat, probability, gain, tick). Written and read in small
// pieces so FatFs copies through the FIL buffer (DMA never touches DTCM).

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, uint32_t len) {
    crc = ~crc;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xEDB88320 & (0u - (crc & 1)));
    }
    return ~crc;
}

struct BlockWriter {
    FIL* file;
    uint32_t crc;
    uint32_t length;
    bool ok;
    void put(const void* data, uint16_t n) {
        UINT written = 0;
        if (ok && ((f_write(file, data, n, &written) != FR_OK) || (written != n))) ok = false;
        crc = crc32Update(crc, (const uint8_t*)data, n);
        length += n;
    }
};

struct BlockReader {
    FIL* file;
    uint32_t crc;
    bool ok;
    void get(void* data, uint16_t n) {
        UINT bytesRead = 0;
        if (ok && ((f_read(file, data, n, &bytesRead) != FR_OK) || (bytesRead != n))) ok = false;
        if (ok) crc = crc32Update(crc, (const uint8_t*)data, n);
    }
};

static bool metaValid(Controller& c, uint8_t track, uint8_t bank, uint8_t beat) {
    const StepMeta& m = meta[track][bank][beat];
    if (m.probability == 0) return false;
    Bank& b = PluginAccess::layer(c, track).bankLibrary[bank];
    return (beat <= b.lastActiveBeatNum) && b.beatLibrary[beat].getActive() && (b.beatLibrary[beat].getStartInterval() == m.tick);
}

static void restoreTiming(Controller& c, uint8_t track, uint8_t bank) {
    /// @brief After a File load, puts generated beats back at their saved ticks. The stock loader
    /// re-quantizes every beat (layerSong_setBeat), which drops swing/humanize, stretches ratchets to
    /// the next beat and moves the beats off the ticks their probability/velocity are keyed to.

    struct Slot {
        bool active, generated;
        uint16_t start;
        uint8_t type, pattern, time, level;
    } slots[kBeatLibrarySize];

    Bank& b = PluginAccess::layer(c, track).bankLibrary[bank];
    uint16_t song = PluginAccess::songInterval(c);
    int32_t previous = -1;
    for (uint8_t k = 0; k < kBeatLibrarySize; k++) {
        const Beat& beat = b.beatLibrary[k];
        StepMeta& m = meta[track][bank][k];
        Slot& slot = slots[k];
        slot = {beat.getActive(), false, beat.getStartInterval(), beat.getFillType(), beat.getFillPattern(), beat.getFillTime(), beat.getFillLevel()};
        if (slot.active && (m.probability != 0)) {
            // The loaded beat is the saved one if quantizing moved it by less than half a step
            int16_t moved = (int16_t)(slot.start - m.tick);
            if ((moved > -(int16_t)(kGenTicksPerStep / 2)) && (moved < (int16_t)(kGenTicksPerStep / 2)) && (m.tick < song)) {
                slot.start = m.tick;
                slot.generated = true;
            }
        }
        if (!slot.generated) m = {0, 0, 0};
        if (slot.active) {
            if ((int32_t)slot.start <= previous) return;  // Order changed (quantize collision): keep the stock load
            previous = slot.start;
        }
    }

    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    for (uint8_t k = 0; k < kBeatLibrarySize; k++) b.resetBeat(k);
    for (BeatFill& fill : b.beatFillLibrary) fill.reset();
    for (uint8_t k = 0; k < kBeatLibrarySize; k++) {
        const Slot& slot = slots[k];
        if (!slot.active) continue;
        uint16_t next = song;
        for (uint8_t j = k + 1; j < kBeatLibrarySize; j++) {
            if (slots[j].active) {
                next = slots[j].start;
                break;
            }
        }
        // Generated ratchets fit in one 16th, as written by writeTrack(); everything else runs to the next beat
        bool ratchet = slot.generated && (slot.type == 0) && (slot.pattern >= 3) && (slot.pattern <= 5);
        uint16_t end = (ratchet && (next - slot.start > kGenTicksPerStep)) ? slot.start + kGenTicksPerStep : next;
        b.setBeat(k, slot.start, end);
        b.setBeatFill(k, slot.type, slot.pattern, slot.time, slot.level);
    }
    PluginAccess::recalculate(c, track, bank);
    __set_PRIMASK(primask);
}

static void saveBlock(FIL& file) {
    ensureState();
    Controller& c = controller;

    // 1. COUNT RECORDS
    uint16_t records = 0;
    for (uint8_t t = 0; t < kGenTracks; t++)
        for (uint8_t b = 0; b < kBankLibrarySize; b++)
            for (uint8_t k = 0; k < kBeatLibrarySize; k++) records += metaValid(c, t, b, k);

    // 2. HEADER + SETTINGS
    BlockWriter w = {&file, 0, 0, true};
    const GenParams& p = settings.params;
    uint8_t header[12] = {kGenBlockVersion, p.style, p.density, p.variation, p.swing, p.humanize,
                          p.dynamics, p.probability, p.ratchet, settings.fillChoice, evolveChoice, 0};
    w.put(header, sizeof(header));
    w.put(&p.seed, sizeof(p.seed));
    w.put(settings.salt, sizeof(settings.salt));
    uint16_t lockMask = 0;
    for (uint8_t i = 0; i < kGenTracks; i++) lockMask |= locked[i] ? (1u << i) : 0;
    w.put(&lockMask, sizeof(lockMask));
    w.put(roleOverride, sizeof(roleOverride));
    w.put(&records, sizeof(records));

    // 3. RECORDS
    for (uint8_t t = 0; t < kGenTracks; t++) {
        for (uint8_t b = 0; b < kBankLibrarySize; b++) {
            for (uint8_t k = 0; k < kBeatLibrarySize; k++) {
                if (!metaValid(c, t, b, k)) continue;
                const StepMeta& m = meta[t][b][k];
                uint8_t record[7] = {t, b, k, m.probability, m.gain, (uint8_t)(m.tick & 0xFF), (uint8_t)(m.tick >> 8)};
                w.put(record, sizeof(record));
            }
        }
    }

    // 4. TRAILER
    uint32_t crc = w.crc, length = w.length;
    UINT written = 0;
    if (w.ok) {
        f_write(&file, &crc, sizeof(crc), &written);
        f_write(&file, &length, sizeof(length), &written);
        f_write(&file, kGenBlockMagic, sizeof(kGenBlockMagic), &written);
    }
}

static void loadBlock(FIL& file, const char* path, bool songLoaded) {
    // 1. A NEWLY LOADED SONG STARTS WITHOUT GENERATED-STEP DATA
    ensureState();
    memset(meta, 0, sizeof(meta));
    undoValid = false;
    if (!songLoaded) return;
    Controller& c = controller;

    // 2. FIND THE TRAILER AT THE END OF THE FILE
    if (f_open(&file, path, FA_READ) != FR_OK) return;
    FSIZE_t size = f_size(&file);
    uint32_t trailer[3] = {};
    UINT bytesRead = 0;
    bool found = (size > 12) && (f_lseek(&file, size - 12) == FR_OK) && (f_read(&file, trailer, 12, &bytesRead) == FR_OK) &&
                 (bytesRead == 12) && (memcmp(&trailer[2], kGenBlockMagic, 4) == 0) && (trailer[1] <= kGenBlockMaxPayload) &&
                 (trailer[1] + 12 <= size);
    if (!found) {
        f_close(&file);
        return;
    }
    uint32_t crcExpected = trailer[0], length = trailer[1];

    // 3. VERIFY CRC (first pass), THEN PARSE (second pass)
    BlockReader r = {&file, 0, true};
    f_lseek(&file, size - 12 - length);
    uint8_t chunk[64];
    for (uint32_t done = 0; r.ok && (done < length); done += sizeof(chunk)) {
        r.get(chunk, (length - done < sizeof(chunk)) ? (uint16_t)(length - done) : (uint16_t)sizeof(chunk));
    }
    if (!r.ok || (r.crc != crcExpected)) {
        f_close(&file);
        pluginHost_message("FILE: GENERATOR DATA DAMAGED - BEATS LOADED WITHOUT PROBABILITY", true);
        return;
    }

    r = {&file, 0, true};
    f_lseek(&file, size - 12 - length);
    uint8_t header[12];
    r.get(header, sizeof(header));
    if (header[0] != kGenBlockVersion) {
        f_close(&file);
        return;
    }
    GenSettings loaded = settings;
    GenParams& p = loaded.params;
    p.style = header[1] % STYLE_COUNT;
    p.density = header[2];
    p.variation = header[3];
    p.swing = header[4];
    p.humanize = header[5];
    p.dynamics = header[6];
    p.probability = header[7];
    p.ratchet = header[8];
    loaded.fillChoice = header[9] % sizeof(kGenFillChoices);
    uint8_t evolveLoaded = header[10] % sizeof(kGenEvolveChoices);
    r.get(&p.seed, sizeof(p.seed));
    r.get(loaded.salt, sizeof(loaded.salt));
    uint16_t lockMask = 0;
    r.get(&lockMask, sizeof(lockMask));
    int8_t roles[kGenTracks];
    r.get(roles, sizeof(roles));
    uint16_t records = 0;
    r.get(&records, sizeof(records));
    if (!r.ok) {
        f_close(&file);
        return;
    }

    settings = loaded;
    evolveChoice = evolveLoaded;
    for (uint8_t i = 0; i < kGenTracks; i++) {
        locked[i] = lockMask & (1u << i);
        roleOverride[i] = ((roles[i] >= -1) && (roles[i] < ROLE_COUNT)) ? roles[i] : -1;
    }

    // 4. RECORDS, THEN THE SAVED TIMING FOR EVERY BANK THAT HAS GENERATED BEATS
    uint8_t touched[kGenTracks] = {};                                   // Bit per bank
    for (uint16_t n = 0; r.ok && (n < records); n++) {
        uint8_t record[7];
        r.get(record, sizeof(record));
        uint8_t t = record[0], b = record[1], k = record[2];
        if (!r.ok || (t >= kGenTracks) || (b >= kBankLibrarySize) || (k >= kBeatLibrarySize) || (record[3] == 0)) continue;
        meta[t][b][k] = {(uint16_t)(record[5] | (record[6] << 8)), record[3], record[4]};
        touched[t] |= (1u << b);
    }
    f_close(&file);
    for (uint8_t t = 0; t < kGenTracks; t++) {
        for (uint8_t b = 0; b < kBankLibrarySize; b++) {
            if (touched[t] & (1u << b)) restoreTiming(c, t, b);
        }
    }
}

/* Open / Close -----------------------------------------------------*/

static void openPage(Controller& c) {
    detectRoles(c);
    open = true;
    dirty = true;
    clearPending = false;
    statusUntil = 0;
}

static void closePage() {
    open = false;
    clearPending = true;
}

/* Public -----------------------------------------------------------*/

void generator_initialize() {
    ensureState();  // Keeps what a boot-time File load already restored
}

bool generator_isOpen() {
    return open;
}

bool generator_needsFrame() {
    return open || clearPending;
}

bool generator_keyPasses(uint8_t keypad, uint8_t key) {
    // Transport keeps working while the generator is open
    return open && (keypad == kKeypadLeft) && ((key == KEY_PLAYSTOP) || (key == KEY_RESET));
}

bool generator_handleEvent(const PluginEvent& e, Controller& c) {
    // 1. EVOLVE (open or closed): every N bars while playing
    if (e.type == PLUGIN_EVENT_STEP) {
        uint8_t every = kGenEvolveChoices[evolveChoice];
        uint16_t step = e.a | (e.b << 8);
        uint8_t stepsPerBar = PluginAccess::beatsPerBar(c) * 4;
        if ((every != 0) && (stepsPerBar != 0) && (step % stepsPerBar == 0) && !PluginAccess::recording(c)) {
            if (++evolveBarCount >= every) {
                evolveBarCount = 0;
                evolve(c);
            }
        }
        return false;  // Steps still reach plugins
    }

    // 2. FILL HELD (tracked open or closed: FILL + STYLE toggles the page)
    if ((e.type == PLUGIN_EVENT_KEY) && (e.a == kKeypadLeft) && (e.b == KEY_FILL)) {
        fillHeld = e.c;
        return open;
    }

    // 3. CLOSED: WATCH FOR FILL + STYLE
    if (!open) {
        if ((e.type == PLUGIN_EVENT_KEY) && e.c && (e.a == kKeypadLeft) && (e.b == KEY_STYLE) && fillHeld && !manager_isOpen()) {
            if (PluginAccess::recording(c)) {
                pluginHost_message("STOP RECORDING TO OPEN THE GENERATOR", true);
            } else {
                openPage(c);
            }
            return true;
        }
        return false;
    }

    // 4. OPEN: KNOBS = PARAMETERS
    GenParams& p = settings.params;
    if (e.type == PLUGIN_EVENT_ENCODER) {
        int8_t d = (int8_t)e.b;
        auto step = [d](uint8_t& v, int16_t perClick, int16_t lo, int16_t hi) {
            int16_t n = (int16_t)v + d * perClick;
            v = (uint8_t)((n < lo) ? lo : (n > hi) ? hi : n);
        };
        switch (e.a) {
            case KNOB_STYLE: p.style = (uint8_t)((p.style + STYLE_COUNT + d) % STYLE_COUNT); break;
            case KNOB_DENSITY: step(p.density, 5, 0, 100); break;
            case KNOB_VARIATION: step(p.variation, 5, 0, 100); break;
            case KNOB_SWING: step(p.swing, 1, 50, 75); break;
            case KNOB_HUMANIZE: step(p.humanize, 5, 0, 100); break;
            case KNOB_DYNAMICS: step(p.dynamics, 5, 0, 100); break;
            case KNOB_PROBABILITY: step(p.probability, 5, 0, 100); break;
            case KNOB_RATCHET: step(p.ratchet, 5, 0, 100); break;
            default: break;
        }
        dirty = true;
        return true;
    }
    if (e.type != PLUGIN_EVENT_KEY) return false;  // Notes and MIDI still reach plugins
    if (!e.c) return true;                          // Releases (stock gets them anyway)

    // 5. OPEN: KEYS (printed names in comments where they differ)
    if (e.a == kKeypadLayer) {
        if (e.b < kGenTracks) selected = e.b;  // Layer key selects that track
    } else if (e.a == kKeypadRight) {
        if (e.b == KEY_FILE) closePage();
    } else if (e.a == kKeypadLeft) {
        switch (e.b) {
            case KEY_UP: selected = (selected + kGenTracks - 1) % kGenTracks; break;
            case KEY_DOWN: selected = (selected + 1) % kGenTracks; break;
            case KEY_ADD:  // SELECT
                locked[selected] = !locked[selected];
                setStatus(false, "TRACK %02u %s", selected + 1, locked[selected] ? "LOCKED - GENERATING LEAVES IT AS IT IS" : "UNLOCKED");
                break;
            case KEY_COPY: roleOverride[selected] = (roleOverride[selected] + 2 > ROLE_COUNT) ? -1 : roleOverride[selected] + 1; break;
            case KEY_CENTER: generateAll(c, false); break;  // GLOBAL
            case KEY_PASTE: generateAll(c, true); break;
            case KEY_MUTE: rerollSelected(c); break;
            case KEY_CLEAR: undo(c); break;
            case KEY_LEFT: historyGo(c, -1); break;
            case KEY_RIGHT: historyGo(c, +1); break;
            case KEY_SONG: settings.fillChoice = (settings.fillChoice + 1) % sizeof(kGenFillChoices); break;
            case KEY_REC:
                evolveChoice = (evolveChoice + 1) % sizeof(kGenEvolveChoices);
                evolveBarCount = 0;
                break;
            case KEY_STYLE:
                if (fillHeld) closePage();  // FILL + STYLE again
                break;
            default: break;
        }
    }
    dirty = true;
    return true;
}

void generator_frame(Controller& c) {
    uint32_t tick = HAL_GetTick();
    if (open) {
        // Redraw on change, when a status expires, and periodically (stock UI may touch the strip)
        if (dirty || ((statusUntil != 0) && (tick >= statusUntil)) || ((tick - lastDrawTick) >= kGenRedrawMs)) {
            if ((statusUntil != 0) && (tick >= statusUntil)) statusUntil = 0;
            dirty = false;
            lastDrawTick = tick;
            drawLines(c);
        }
    } else if (clearPending) {
        clearPending = false;
        c.lcd.setBackColor(BLACK);
        c.lcd.clearRect(kGenTextX, kGenLine1Y - 1, kGenTextWidth, 19);
        drawMarker(c.lcd, markerRow, false);
        markerRow = 0xFF;
    }
}

/* Hooks Called From Stock Code -------------------------------------*/

void pluginHost_beatGate(uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval, float& volume) {
    /// @brief Play-timer interrupt: per-step probability roll and velocity for generated beats.

    if ((layer >= kGenTracks) || (bank >= kBankLibrarySize) || (beatNum >= kBeatLibrarySize)) return;
    const StepMeta& m = meta[layer][bank][beatNum];
    if ((m.probability == 0) || (m.tick != startInterval)) return;  // Not generated, or edited by hand

    if ((m.probability < 100) && ((nextRandom(gateRandom) % 100u) >= m.probability)) {
        volume = 0.0f;
        return;
    }
    volume *= m.gain * 0.01f;
}

void pluginHost_beatColor(Lcd& lcd, uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval) {
    /// @brief lcd_drawBeat: tick colour by probability (100% keeps the track colour).

    if ((layer >= kGenTracks) || (bank >= kBankLibrarySize) || (beatNum >= kBeatLibrarySize)) return;
    const StepMeta& m = meta[layer][bank][beatNum];
    if ((m.probability == 0) || (m.tick != startInterval) || (m.probability >= 95)) return;
    lcd.setForeColor((m.probability >= 70) ? GREEN : (m.probability >= 45) ? YELLOW : (m.probability >= 20) ? ORANGE : RED);
}

void pluginHost_fileSaved(FIL& file) {
    saveBlock(file);
}

void pluginHost_fileLoaded(FIL& file, const char* path, bool songLoaded) {
    loadBlock(file, path, songLoaded);
}

#endif
