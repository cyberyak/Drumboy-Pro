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
const uint32_t kGenLiveMs                           = 80;               // GRIDS / EUCLID knobs: regenerate at most this often

// File extension block: [payload][crc32][payload length]["CYBX"] after the stock EOF marker
const char     kGenBlockMagic[4]                    = {'C', 'Y', 'B', 'X'};
const uint8_t  kGenBlockVersion                     = 2;                // 2: conditions, GRIDS, EUCLID, track sources
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
// Probability, velocity and trig condition the stock beat format can't hold. Keyed by
// beat slot and validated against the beat's start tick, so beats edited by hand
// afterwards simply play normally. Saved in the File's extension block.

struct StepMeta {
    uint16_t tick;
    uint8_t probability;                                                // 0 = no meta for this beat
    uint8_t gain;                                                       // Volume multiplier x100
    uint8_t cond;                                                       // TrigCondition
};

/* Trig Conditions (OXI / Elektron style) ---------------------------*/
// 0 = none; 1-35 = A:B (play on loop A of every B, B = 2..8); then FILL, NOT FILL,
// FIRST (first loop after PLAY), NOT FIRST, PRE (previous condition on the track
// was true), NOT PRE.

enum TrigCondition : uint8_t { COND_NONE = 0, COND_AB_LAST = 35, COND_FILL, COND_NOT_FILL, COND_FIRST, COND_NOT_FIRST, COND_PRE, COND_NOT_PRE, COND_COUNT };

static bool condRatio(uint8_t cond, uint8_t& a, uint8_t& b) {
    if ((cond == COND_NONE) || (cond > COND_AB_LAST)) return false;
    uint8_t index = cond - 1;
    for (b = 2; b <= 8; b++) {
        if (index < b) {
            a = index + 1;
            return true;
        }
        index -= b;
    }
    return false;
}

static void condName(uint8_t cond, char* out, size_t size) {
    static const char* const kNames[] = {"FILL", "NOT FILL", "FIRST", "NOT FIRST", "PRE", "NOT PRE"};
    uint8_t a, b;
    if (cond == COND_NONE) {
        snprintf(out, size, "NONE");
    } else if (condRatio(cond, a, b)) {
        snprintf(out, size, "%u:%u", a, b);
    } else if (cond < COND_COUNT) {
        snprintf(out, size, "%s", kNames[cond - COND_FILL]);
    } else {
        snprintf(out, size, "?");
    }
}

static StepMeta meta[kGenTracks][kBankLibrarySize][kBeatLibrarySize] GEN_FAST_SECTION;

/* Undo (one level: the bank as it was before the last change) -----*/

static Bank undoBanks[kGenTracks] GEN_FAST_SECTION;
static StepMeta undoMeta[kGenTracks][kBeatLibrarySize] GEN_FAST_SECTION;
static bool undoFill[kGenTracks];
static uint8_t undoSource[kGenTracks];
static bool undoLocked[kGenTracks];
static EuclidParams undoEuclid[kGenTracks];
static uint8_t undoBank = 0;
static bool undoValid = false;

/* Settings, History, Page State ------------------------------------*/

enum GenKnob : uint8_t { KNOB_STYLE, KNOB_DENSITY, KNOB_VARIATION, KNOB_SWING, KNOB_HUMANIZE, KNOB_DYNAMICS, KNOB_PROBABILITY, KNOB_RATCHET };
enum GenPage : uint8_t { PAGE_GEN, PAGE_GRIDS, PAGE_EUCLID, PAGE_STEP, PAGE_COUNT };
enum TrackSource : uint8_t { SOURCE_GEN, SOURCE_GRIDS, SOURCE_EUCLID, SOURCE_COUNT };

const char* const kGenPageNames[PAGE_COUNT] = {"GEN", "GRIDS", "EUCLID", "STEP"};
const char* const kGenSourceNames[SOURCE_COUNT] = {"GEN", "GRIDS", "EUCLID"};

struct GenSettings {
    GenParams params;
    uint32_t salt[kGenTracks];                                          // Per-track re-roll offset
    uint8_t fillChoice;
    GridsParams grids;
    uint8_t source[kGenTracks];                                         // TrackSource: which engine made the track
};

static GenSettings settings = {{STYLE_TECHNO, 60, 30, 50, 20, 60, 40, 20, 4, 1, 0}, {}, 2, {25, 0, {60, 60, 60}, 0}, {}};
static EuclidParams euclid[kGenTracks];
static uint8_t evolveChoice = 0;
static bool locked[kGenTracks];
static int8_t roleOverride[kGenTracks];                                // -1 = automatic
static GenRole detected[kGenTracks];

static GenSettings history[kGenHistorySize];
static uint8_t historyCount = 0;                                        // Entries stored
static uint8_t historyNewest = 0;                                       // Ring index of the newest entry
static uint8_t historyBack = 0;                                         // 0 = newest, 1 = one older, ...

static bool open = false;
static uint8_t page = PAGE_GEN;
static uint8_t stepSel = 0;                                             // STEP page: beat index on the selected track
static bool editOpen = false;                                           // An undo snapshot covers the current knob edits
static uint16_t livePending = 0;                                        // Tracks to regenerate on the next frame (GRIDS / EUCLID knobs)
static uint32_t lastLiveTick = 0;
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

// Play state per track for conditions (written in the play-timer interrupt)
static volatile uint16_t gateLastStart[kGenTracks];                     // 0xFFFF = no beat since PLAY / RESET
static volatile uint16_t loopCount[kGenTracks];
static volatile bool gatePlay[kGenTracks];                              // Decision for the current beat (ratchet repeats reuse it)
static volatile float gateGain[kGenTracks];
static volatile bool preResult[kGenTracks];
static bool stateReady = false;

static void ensureState() {
    // DTCM is not zeroed at boot; set up once, by whichever runs first (boot-time File load or plugin start)
    if (stateReady) return;
    memset(meta, 0, sizeof(meta));
    for (uint8_t i = 0; i < kGenTracks; i++) {
        roleOverride[i] = -1;
        locked[i] = false;
        euclid[i] = {16, 4, 0, RATE_16, 110, 0, 100};
        gateLastStart[i] = 0xFFFF;
        loopCount[i] = 0;
        gatePlay[i] = true;
        gateGain[i] = 1.0f;
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
        meta[track][bank][k] = {0, 0, 0, COND_NONE};
    }
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
        meta[track][bank][written] = {start, list[k].probability, (uint8_t)(gain * 100.0f), COND_NONE};
        written++;
    }
    PluginAccess::recalculate(c, track, bank);

    __set_PRIMASK(primask);

    if (anyRatchet && !layer.fill) PluginAccess::setFill(c, track, true);  // Ratchet repeats only sound with F on
    // Redraw the lane unless a plugin page (manager / show mode) owns that screen area; it restores the lanes itself
    if ((bank == PluginAccess::activeBank(c)) && !pluginHost_isScreenCaptured()) PluginAccess::drawSong(c, track, bank);
}

static void snapshot(Controller& c, uint8_t bank) {
    editOpen = false;  // The next knob edit starts a new undo step
    for (uint8_t i = 0; i < kGenTracks; i++) {
        Layer& layer = PluginAccess::layer(c, i);
        undoBanks[i] = layer.bankLibrary[bank];
        memcpy(undoMeta[i], meta[i][bank], sizeof(undoMeta[i]));
        undoFill[i] = layer.fill;
        undoSource[i] = settings.source[i];
        undoLocked[i] = locked[i];
        undoEuclid[i] = euclid[i];
    }
    undoBank = bank;
    undoValid = true;
}

static void undo(Controller& c) {
    if (!undoValid) {
        setStatus(true, "NOTHING TO UNDO");
        return;
    }
    livePending = 0;  // A pending live edit must not overwrite the undo
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
        settings.source[i] = undoSource[i];
        locked[i] = undoLocked[i];
        euclid[i] = undoEuclid[i];
        if (PluginAccess::layer(c, i).fill != undoFill[i]) PluginAccess::setFill(c, i, undoFill[i]);
        if (undoBank == PluginAccess::activeBank(c)) PluginAccess::drawSong(c, i, undoBank);
    }
    undoValid = false;
    editOpen = false;
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

            uint8_t n;
            if ((settings.source[i] == SOURCE_GRIDS) && (gen_gridsPart(role) != GRIDS_NONE)) {
                n = gen_grids(role, settings.grids, p, stepsPerBar, bars, i, steps);
            } else if (settings.source[i] == SOURCE_EUCLID) {
                n = gen_euclid(euclid[i], PluginAccess::songInterval(c), steps, nullptr);
            } else {
                n = gen_generate(role, p, stepsPerBar, bars, i, occupied, steps);
            }
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
    uint8_t sources[kGenTracks];
    memcpy(sources, settings.source, sizeof(sources));
    settings = history[(historyNewest + kGenHistorySize - historyBack) % kGenHistorySize];
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (sources[i] == SOURCE_EUCLID) settings.source[i] = SOURCE_EUCLID;  // Euclid tracks are edited, not part of history
    }

    snapshot(c, PluginAccess::activeBank(c));
    regenerate(c, unlockedMask(), nullptr);
    setStatus(false, "HISTORY %u/%u: %s SEED %lu", historyCount - historyBack, historyCount, kGenStyleNames[settings.params.style],
              (unsigned long)settings.params.seed);
}

/* Actions ----------------------------------------------------------*/

static uint16_t gridsMask() {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (gen_gridsPart(roleOf(i)) != GRIDS_NONE) mask |= (1u << i);
    }
    return mask;
}

static uint16_t sourceMask(uint8_t source) {
    uint16_t mask = 0;
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (settings.source[i] == source) mask |= (1u << i);
    }
    return mask;
}

static void generateAll(Controller& c, bool newSeed) {
    // GEN page: every unlocked track from the recipes. GRIDS page: unlocked kick / snare / hat tracks from the map.
    bool grids = (page == PAGE_GRIDS);
    uint16_t mask = unlockedMask() & (grids ? gridsMask() : 0xFFFF);
    if (mask == 0) {
        setStatus(true, grids ? "NO UNLOCKED KICK, SNARE OR HAT TRACKS" : "ALL TRACKS ARE LOCKED");
        return;
    }
    if (newSeed) {
        settings.params.seed = (settings.params.seed * 1103515245u + 12345u + HAL_GetTick()) % 10000u;
        memset(settings.salt, 0, sizeof(settings.salt));
    }
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (mask & (1u << i)) settings.source[i] = grids ? SOURCE_GRIDS : SOURCE_GEN;
    }
    uint8_t bank = PluginAccess::activeBank(c);
    snapshot(c, bank);
    uint8_t n = regenerate(c, mask, nullptr);
    historyPush();
    setStatus(false, "GENERATED %u TRACKS (%s, SEED %lu) INTO BANK %u - FILE > SAVE TO KEEP, CLEAR = UNDO", n,
              grids ? "GRIDS" : kGenStyleNames[settings.params.style], (unsigned long)settings.params.seed, bank + 1);
}

static void rerollSelected(Controller& c) {
    if (locked[selected]) {
        setStatus(true, "TRACK %02u IS LOCKED - SELECT UNLOCKS IT", selected + 1);
        return;
    }
    if (settings.source[selected] == SOURCE_EUCLID) {
        setStatus(true, "TRACK %02u IS EUCLIDEAN - EDIT IT ON THE EUCLID PAGE", selected + 1);
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

    uint16_t candidates = unlockedMask() & ~sourceMask(SOURCE_EUCLID);
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

/* Live Edits (GRIDS / EUCLID knobs, STEP page) ---------------------*/

static bool metaValid(Controller& c, uint8_t track, uint8_t bank, uint8_t beat);

static void beginEdit(Controller& c) {
    // One undo step per run of knob edits (cleared by any other action, page or track change)
    if (editOpen) return;
    snapshot(c, PluginAccess::activeBank(c));
    editOpen = true;
}

static void endEdit() {
    editOpen = false;
}

static Bank& selectedBank(Controller& c) {
    return PluginAccess::layer(c, selected).bankLibrary[PluginAccess::activeBank(c)];
}

static uint8_t stepCount(Controller& c) {
    return (uint8_t)(selectedBank(c).lastActiveBeatNum + 1);
}

static void redrawLane(Controller& c, uint8_t track) {
    if (!pluginHost_isScreenCaptured()) PluginAccess::drawSong(c, track, PluginAccess::activeBank(c));
}

static StepMeta& selectedMeta(Controller& c) {
    // Meta for the selected beat; hand-made beats get a neutral entry (always plays, level as recorded)
    uint8_t bank = PluginAccess::activeBank(c);
    if (!metaValid(c, selected, bank, stepSel)) {
        meta[selected][bank][stepSel] = {selectedBank(c).beatLibrary[stepSel].getStartInterval(), 100, 100, COND_NONE};
    }
    return meta[selected][bank][stepSel];
}

static void selectTrack(Controller& c, uint8_t track) {
    if (track == selected) return;
    uint8_t old = selected;
    selected = track;
    stepSel = 0;
    endEdit();
    if (page == PAGE_STEP) {
        redrawLane(c, old);
        redrawLane(c, selected);
    }
}

static void setPage(Controller& c, uint8_t next) {
    bool stepShown = (page == PAGE_STEP) || (next == PAGE_STEP);
    page = next;
    endEdit();
    if (stepShown) redrawLane(c, selected);  // Show / hide the selected-step highlight
}

/* Drawing ----------------------------------------------------------*/

static void drawMarker(Lcd& lcd, uint8_t row, bool on) {
    if (row >= kGenTracks) return;
    lcd.setForeColor(on ? CYAN : BLACK);
    lcd.fillRect(kGenMarkerX, kImageLayerY[row] + 8, 8, 14);
}

static void drawParams(Controller& c, char* text, size_t size) {
    const GenParams& p = settings.params;
    char fill[8], evo[8];
    (kGenFillChoices[settings.fillChoice] == 0) ? snprintf(fill, sizeof(fill), "OFF") : snprintf(fill, sizeof(fill), "%u", kGenFillChoices[settings.fillChoice]);
    (kGenEvolveChoices[evolveChoice] == 0) ? snprintf(evo, sizeof(evo), "OFF") : snprintf(evo, sizeof(evo), "%u", kGenEvolveChoices[evolveChoice]);

    switch (page) {
        case PAGE_GRIDS: {
            static const char* const kFeel[] = {"STRAIGHT", "ELECTRO", "BROKEN"};
            const GridsParams& g = settings.grids;
            snprintf(text, size, "GRIDS X %u Y %u (%s)  KICK %u  SNARE %u  HATS %u  CHAOS %u  SWING %u  DYN %u  EVOLVE %s  SEED %lu", g.x, g.y,
                     kFeel[(g.y + 25) / 50 > 2 ? 2 : (g.y + 25) / 50], g.density[GRIDS_KICK], g.density[GRIDS_SNARE], g.density[GRIDS_HATS], g.chaos,
                     p.swing, p.dynamics, evo, (unsigned long)p.seed);
            break;
        }
        case PAGE_EUCLID: {
            const EuclidParams& e = euclid[selected];
            snprintf(text, size, "EUCLID TRK %02u  E(%u,%u)  STEPS %u  PULSES %u  ROTATE %u  RATE %s  VELO %u  ACCENTS %u  PROB %u%s", selected + 1,
                     e.pulses, e.steps, e.steps, e.pulses, e.rotate, kGenRateNames[e.rate], e.velocity, e.accents, e.probability,
                     (settings.source[selected] == SOURCE_EUCLID) ? "" : "  (TURN A KNOB TO START)");
            break;
        }
        case PAGE_STEP: {
            uint8_t count = stepCount(c);
            if (count == 0) {
                snprintf(text, size, "STEP TRK %02u  NO BEATS IN THIS BANK", selected + 1);
                break;
            }
            if (stepSel >= count) stepSel = count - 1;
            uint8_t bank = PluginAccess::activeBank(c);
            uint16_t tick = selectedBank(c).beatLibrary[stepSel].getStartInterval();
            uint8_t stepsPerBar = PluginAccess::beatsPerBar(c) * 4;
            uint16_t step16 = tick / kGenTicksPerStep;
            bool has = metaValid(c, selected, bank, stepSel);
            const StepMeta& m = meta[selected][bank][stepSel];
            char cond[12];
            condName(has ? m.cond : COND_NONE, cond, sizeof(cond));
            snprintf(text, size, "STEP TRK %02u  %02u/%02u  BAR %u STEP %u%s  COND %s  PROB %u  LEVEL %u%%", selected + 1, stepSel + 1, count,
                     step16 / stepsPerBar + 1, step16 % stepsPerBar + 1, (tick % kGenTicksPerStep) ? "+" : "", cond, has ? m.probability : 100,
                     has ? m.gain : 100);
            break;
        }
        default:
            snprintf(text, size, "GEN %s  DENS %u  VAR %u  SWING %u  HUM %u  DYN %u  PROB %u  RATCH %u  FILL %s  EVOLVE %s  SEED %lu",
                     kGenStyleNames[p.style], p.density, p.variation, p.swing, p.humanize, p.dynamics, p.probability, p.ratchet, fill, evo,
                     (unsigned long)p.seed);
            break;
    }
}

static void drawKeys(char* text, size_t size) {
    // Keep in sync with the key handling in generator_handleEvent
    static const char* const kKeys[PAGE_COUNT] = {
        "GLOBAL GEN  PASTE NEW  MUTE TRK  L/R HIST  CLEAR UNDO  SELECT LOCK  COPY ROLE  SONG FILL  REC EVOLVE  STYLE PAGE",
        "GLOBAL GEN DRUMS  PASTE NEW  MUTE TRK  L/R HIST  CLEAR UNDO  SELECT LOCK  COPY ROLE  REC EVOLVE  STYLE PAGE",
        "KNOBS EDIT LIVE  L/R ROTATE  PASTE RANDOM  MUTE RELEASE  CLEAR UNDO  STYLE PAGE",
        "L/R STEP  KNOB1 COND  KNOB2 PROB  KNOB3 LEVEL  GLOBAL RESET STEP  CLEAR UNDO  STYLE PAGE",
    };
    snprintf(text, size, "TRK %02u %s %s %s%s | %s", selected + 1, kGenRoleNames[roleOf(selected)], (roleOverride[selected] >= 0) ? "SET" : "AUTO",
             kGenSourceNames[settings.source[selected] % SOURCE_COUNT], locked[selected] ? " LOCK" : "", kKeys[page]);
}

static void drawLines(Controller& c) {
    Lcd& lcd = c.lcd;
    char text[160];

    lcd.setFont(FONT_05x07);
    lcd.setAlignment(LEFT);
    lcd.setBackColor(BLACK);
    lcd.clearRect(kGenTextX, kGenLine1Y - 1, kGenTextWidth, 19);

    // 1. PARAMETERS OF THE PAGE (kept short: the File menu's build label sits at the right end of this line)
    drawParams(c, text, sizeof(text));
    lcd.setForeColor(CYAN);
    lcd.drawText(text, strlen(text), kGenTextX, kGenLine1Y);

    // 2. STATUS, OR SELECTED TRACK + KEYS
    if (HAL_GetTick() < statusUntil) {
        lcd.setForeColor(statusError ? RED : YELLOW);
        lcd.drawText(status, strlen(status), kGenTextX, kGenLine2Y);
    } else {
        drawKeys(text, sizeof(text));
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
// Payload: version, settings, salts, locks, role overrides; version 2 adds the GRIDS
// settings, track sources and per-track Euclid settings, and conditions in the records.
// Then one record per beat with meta: track, bank, beat, probability, gain, tick (u16)
// and (v2) condition. Written and read in small pieces so FatFs copies through the FIL
// buffer (DMA never touches DTCM).

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
    const GridsParams& g = settings.grids;
    uint8_t grids[6] = {g.x, g.y, g.density[GRIDS_KICK], g.density[GRIDS_SNARE], g.density[GRIDS_HATS], g.chaos};
    w.put(grids, sizeof(grids));
    w.put(settings.source, sizeof(settings.source));
    for (uint8_t i = 0; i < kGenTracks; i++) {
        const EuclidParams& e = euclid[i];
        uint8_t data[7] = {e.steps, e.pulses, e.rotate, e.rate, e.velocity, e.accents, e.probability};
        w.put(data, sizeof(data));
    }
    w.put(&records, sizeof(records));

    // 3. RECORDS
    for (uint8_t t = 0; t < kGenTracks; t++) {
        for (uint8_t b = 0; b < kBankLibrarySize; b++) {
            for (uint8_t k = 0; k < kBeatLibrarySize; k++) {
                if (!metaValid(c, t, b, k)) continue;
                const StepMeta& m = meta[t][b][k];
                uint8_t record[8] = {t, b, k, m.probability, m.gain, (uint8_t)(m.tick & 0xFF), (uint8_t)(m.tick >> 8), m.cond};
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
    uint8_t version = header[0];
    if ((version == 0) || (version > kGenBlockVersion)) {
        f_close(&file);
        pluginHost_message("FILE: GENERATOR DATA FROM A NEWER FIRMWARE - BEATS LOADED WITHOUT IT", true);
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
    memset(loaded.source, SOURCE_GEN, sizeof(loaded.source));
    EuclidParams euclidLoaded[kGenTracks];
    memcpy(euclidLoaded, euclid, sizeof(euclidLoaded));
    if (version >= 2) {
        uint8_t grids[6];
        r.get(grids, sizeof(grids));
        loaded.grids = {grids[0], grids[1], {grids[2], grids[3], grids[4]}, grids[5]};
        r.get(loaded.source, sizeof(loaded.source));
        for (uint8_t i = 0; i < kGenTracks; i++) {
            uint8_t d[7] = {};
            r.get(d, sizeof(d));
            loaded.source[i] %= SOURCE_COUNT;
            uint8_t steps = (d[0] < 1) ? 1 : (d[0] > 32) ? 32 : d[0];
            uint8_t pulses = (d[1] > steps) ? steps : d[1];
            euclidLoaded[i] = {steps, pulses, (uint8_t)(d[2] % steps), (uint8_t)(d[3] % RATE_COUNT), d[4], (d[5] > pulses) ? pulses : d[5], d[6]};
        }
    }
    uint16_t records = 0;
    r.get(&records, sizeof(records));
    if (!r.ok) {
        f_close(&file);
        return;
    }

    settings = loaded;
    memcpy(euclid, euclidLoaded, sizeof(euclid));
    evolveChoice = evolveLoaded;
    for (uint8_t i = 0; i < kGenTracks; i++) {
        locked[i] = lockMask & (1u << i);
        roleOverride[i] = ((roles[i] >= -1) && (roles[i] < ROLE_COUNT)) ? roles[i] : -1;
    }

    // 4. RECORDS (applied only where the loaded beat matches)
    for (uint16_t n = 0; r.ok && (n < records); n++) {
        uint8_t record[8] = {};
        r.get(record, (version >= 2) ? 8 : 7);
        uint8_t t = record[0], b = record[1], k = record[2];
        if (!r.ok || (t >= kGenTracks) || (b >= kBankLibrarySize) || (k >= kBeatLibrarySize)) continue;
        meta[t][b][k] = {(uint16_t)(record[5] | (record[6] << 8)), record[3], record[4], (uint8_t)((record[7] < COND_COUNT) ? record[7] : COND_NONE)};
        if (!metaValid(c, t, b, k)) meta[t][b][k] = {0, 0, 0, COND_NONE};
    }
    f_close(&file);
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
    return open || clearPending || livePending;  // Live edits still land after closing
}

bool generator_keyPasses(uint8_t keypad, uint8_t key) {
    // Transport keeps working while the generator is open
    return open && (keypad == kKeypadLeft) && ((key == KEY_PLAYSTOP) || (key == KEY_RESET));
}

static void resetLoops() {
    // PLAY / RESET: conditions count loops from here (FIRST = the first loop)
    for (uint8_t i = 0; i < kGenTracks; i++) {
        gateLastStart[i] = 0xFFFF;
        loopCount[i] = 0;
        preResult[i] = false;
    }
}

static void clampStep(uint8_t& v, int8_t d, int16_t perClick, int16_t lo, int16_t hi) {
    int16_t n = (int16_t)v + d * perClick;
    v = (uint8_t)((n < lo) ? lo : (n > hi) ? hi : n);
}

static void knobGen(uint8_t knob, int8_t d) {
    GenParams& p = settings.params;
    switch (knob) {
        case KNOB_STYLE: p.style = (uint8_t)((p.style + STYLE_COUNT + d) % STYLE_COUNT); break;
        case KNOB_DENSITY: clampStep(p.density, d, 5, 0, 100); break;
        case KNOB_VARIATION: clampStep(p.variation, d, 5, 0, 100); break;
        case KNOB_SWING: clampStep(p.swing, d, 1, 50, 75); break;
        case KNOB_HUMANIZE: clampStep(p.humanize, d, 5, 0, 100); break;
        case KNOB_DYNAMICS: clampStep(p.dynamics, d, 5, 0, 100); break;
        case KNOB_PROBABILITY: clampStep(p.probability, d, 5, 0, 100); break;
        case KNOB_RATCHET: clampStep(p.ratchet, d, 5, 0, 100); break;
        default: break;
    }
}

static void knobGrids(Controller& c, uint8_t knob, int8_t d) {
    // X, Y, KICK, SNARE, HATS, CHAOS, SWING, DYNAMICS; GRIDS tracks follow live
    GridsParams& g = settings.grids;
    switch (knob) {
        case 0: clampStep(g.x, d, 5, 0, 100); break;
        case 1: clampStep(g.y, d, 5, 0, 100); break;
        case 2: clampStep(g.density[GRIDS_KICK], d, 5, 0, 100); break;
        case 3: clampStep(g.density[GRIDS_SNARE], d, 5, 0, 100); break;
        case 4: clampStep(g.density[GRIDS_HATS], d, 5, 0, 100); break;
        case 5: clampStep(g.chaos, d, 5, 0, 100); break;
        case 6: clampStep(settings.params.swing, d, 1, 50, 75); break;
        case 7: clampStep(settings.params.dynamics, d, 5, 0, 100); break;
        default: return;
    }
    uint16_t live = sourceMask(SOURCE_GRIDS) & unlockedMask();
    if (live == 0) {
        setStatus(false, "PRESS GLOBAL TO GENERATE THE DRUMS FROM THE MAP");
        return;
    }
    beginEdit(c);
    livePending |= live;
}

static void applyEuclid(Controller& c) {
    // The selected track becomes (and stays) Euclidean: locked so GEN / GRIDS keep it
    beginEdit(c);
    settings.source[selected] = SOURCE_EUCLID;
    locked[selected] = true;
    livePending |= (1u << selected);
}

static void knobEuclid(Controller& c, uint8_t knob, int8_t d) {
    EuclidParams& e = euclid[selected];
    switch (knob) {
        case 0: clampStep(e.steps, d, 1, 1, 32); break;
        case 1: clampStep(e.pulses, d, 1, 0, e.steps); break;
        case 2: e.rotate = (uint8_t)((e.rotate + e.steps + d) % e.steps); break;
        case 3: clampStep(e.rate, d, 1, 0, RATE_COUNT - 1); break;
        case 4: clampStep(e.velocity, d, 5, 10, 127); break;
        case 5: clampStep(e.accents, d, 1, 0, e.pulses); break;
        case 6: clampStep(e.probability, d, 5, 10, 100); break;
        default: return;
    }
    if (e.pulses > e.steps) e.pulses = e.steps;
    if (e.rotate >= e.steps) e.rotate = 0;
    if (e.accents > e.pulses) e.accents = e.pulses;
    applyEuclid(c);
}

static void knobStep(Controller& c, uint8_t knob, int8_t d) {
    if (stepCount(c) == 0) return;
    if (stepSel >= stepCount(c)) stepSel = stepCount(c) - 1;
    beginEdit(c);
    StepMeta& m = selectedMeta(c);
    switch (knob) {
        case 0: m.cond = (uint8_t)((m.cond + COND_COUNT + d) % COND_COUNT); break;
        case 1: clampStep(m.probability, d, 5, 5, 100); break;
        case 2: clampStep(m.gain, d, 5, 10, 250); break;
        default: return;
    }
    redrawLane(c, selected);
}

static void stepGo(Controller& c, int8_t direction) {
    uint8_t count = stepCount(c);
    if (count == 0) return;
    stepSel = (uint8_t)((stepSel + count + direction) % count);
    endEdit();
    redrawLane(c, selected);
}

static void stepReset(Controller& c) {
    if (stepCount(c) == 0) return;
    beginEdit(c);
    StepMeta& m = selectedMeta(c);
    m.cond = COND_NONE;
    m.probability = 100;
    m.gain = 100;
    redrawLane(c, selected);
    setStatus(false, "STEP %u RESET: ALWAYS PLAYS AT ITS RECORDED LEVEL", stepSel + 1);
}

static void euclidRandom(Controller& c) {
    EuclidParams& e = euclid[selected];
    e.pulses = 1 + nextRandom(evolveRandom) % e.steps;
    e.rotate = nextRandom(evolveRandom) % e.steps;
    e.accents = (e.pulses > 2) ? nextRandom(evolveRandom) % (e.pulses / 2 + 1) : 0;
    applyEuclid(c);
}

static void euclidRelease() {
    if (settings.source[selected] != SOURCE_EUCLID) {
        setStatus(true, "TRACK %02u IS NOT EUCLIDEAN", selected + 1);
        return;
    }
    settings.source[selected] = SOURCE_GEN;
    locked[selected] = false;
    setStatus(false, "TRACK %02u RELEASED: BEATS KEPT, GEN / GRIDS MAY CHANGE IT AGAIN", selected + 1);
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

    // 2. TRANSPORT (open or closed): conditions count loops from PLAY / RESET
    if ((e.type == PLUGIN_EVENT_KEY) && e.c && (e.a == kKeypadLeft) && ((e.b == KEY_PLAYSTOP) || (e.b == KEY_RESET))) {
        resetLoops();
        return false;
    }

    // 3. FILL HELD (tracked open or closed: FILL + STYLE toggles the page, FILL / NOT FILL conditions)
    if ((e.type == PLUGIN_EVENT_KEY) && (e.a == kKeypadLeft) && (e.b == KEY_FILL)) {
        fillHeld = e.c;
        return open;
    }

    // 4. CLOSED: WATCH FOR FILL + STYLE
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

    // 5. OPEN: KNOBS = PARAMETERS OF THE PAGE
    if (e.type == PLUGIN_EVENT_ENCODER) {
        int8_t d = (int8_t)e.b;
        switch (page) {
            case PAGE_GRIDS: knobGrids(c, e.a, d); break;
            case PAGE_EUCLID: knobEuclid(c, e.a, d); break;
            case PAGE_STEP: knobStep(c, e.a, d); break;
            default: knobGen(e.a, d); break;
        }
        dirty = true;
        return true;
    }
    if (e.type != PLUGIN_EVENT_KEY) return false;  // Notes and MIDI still reach plugins
    if (!e.c) return true;                          // Releases (stock gets them anyway)

    // 6. OPEN: KEYS (printed names in comments where they differ; keep drawKeys in sync)
    if (e.a == kKeypadLayer) {
        if (e.b < kGenTracks) selectTrack(c, e.b);  // Layer key selects that track
    } else if (e.a == kKeypadRight) {
        if (e.b == KEY_FILE) closePage();
    } else if (e.a == kKeypadLeft) {
        // Keys on every page
        switch (e.b) {
            case KEY_UP: selectTrack(c, (selected + kGenTracks - 1) % kGenTracks); break;
            case KEY_DOWN: selectTrack(c, (selected + 1) % kGenTracks); break;
            case KEY_ADD: locked[selected] = !locked[selected]; break;  // SELECT
            case KEY_COPY: roleOverride[selected] = (roleOverride[selected] + 2 > ROLE_COUNT) ? -1 : roleOverride[selected] + 1; break;
            case KEY_CLEAR: undo(c); break;
            case KEY_SONG: settings.fillChoice = (settings.fillChoice + 1) % sizeof(kGenFillChoices); break;
            case KEY_REC:
                evolveChoice = (evolveChoice + 1) % sizeof(kGenEvolveChoices);
                evolveBarCount = 0;
                break;
            case KEY_STYLE:
                fillHeld ? closePage() : setPage(c, (page + 1) % PAGE_COUNT);  // FILL + STYLE closes, STYLE alone: next page
                break;
            default: break;
        }

        // Keys that depend on the page
        switch (page) {
            case PAGE_GEN:
            case PAGE_GRIDS:
                switch (e.b) {
                    case KEY_CENTER: generateAll(c, false); break;  // GLOBAL
                    case KEY_PASTE: generateAll(c, true); break;
                    case KEY_MUTE: rerollSelected(c); break;
                    case KEY_LEFT: historyGo(c, -1); break;
                    case KEY_RIGHT: historyGo(c, +1); break;
                    default: break;
                }
                break;
            case PAGE_EUCLID:
                switch (e.b) {
                    case KEY_CENTER: applyEuclid(c); break;  // GLOBAL: write the Euclid rhythm again
                    case KEY_PASTE: euclidRandom(c); break;
                    case KEY_MUTE: euclidRelease(); break;
                    case KEY_LEFT: knobEuclid(c, 2, -1); break;
                    case KEY_RIGHT: knobEuclid(c, 2, +1); break;
                    default: break;
                }
                break;
            case PAGE_STEP:
                switch (e.b) {
                    case KEY_CENTER: stepReset(c); break;  // GLOBAL
                    case KEY_LEFT: stepGo(c, -1); break;
                    case KEY_RIGHT: stepGo(c, +1); break;
                    default: break;
                }
                break;
            default: break;
        }
    }
    dirty = true;
    return true;
}

void generator_frame(Controller& c) {
    uint32_t tick = HAL_GetTick();

    // 1. LIVE EDITS (GRIDS / EUCLID knobs), throttled
    if (livePending && ((tick - lastLiveTick) >= kGenLiveMs)) {
        uint16_t mask = livePending;
        livePending = 0;
        lastLiveTick = tick;
        regenerate(c, mask, nullptr);
        if ((page == PAGE_EUCLID) && (mask & (1u << selected))) {
            uint16_t wanted = 0;
            gen_euclid(euclid[selected], PluginAccess::songInterval(c), steps, &wanted);
            if (wanted > kGenMaxSteps) setStatus(true, "E(%u,%u) NEEDS %u HITS - CUT AT THE 64-BEAT LIMIT", euclid[selected].pulses, euclid[selected].steps, wanted);
        }
        dirty = true;
    }

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

static bool condTrue(uint8_t cond, uint8_t layer) {
    uint8_t a, b;
    if (condRatio(cond, a, b)) return (loopCount[layer] % b) == (a - 1);
    switch (cond) {
        case COND_FILL: return fillHeld;
        case COND_NOT_FILL: return !fillHeld;
        case COND_FIRST: return loopCount[layer] == 0;
        case COND_NOT_FIRST: return loopCount[layer] != 0;
        case COND_PRE: return preResult[layer];
        case COND_NOT_PRE: return !preResult[layer];
        default: return true;
    }
}

void pluginHost_beatGate(uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval, int8_t micro, float& volume) {
    /// @brief Play-timer interrupt: trig condition, probability roll and velocity, decided once per beat
    /// (micro 0); fill / ratchet repeats of the same beat follow that decision.

    if ((layer >= kGenTracks) || (bank >= kBankLibrarySize) || (beatNum >= kBeatLibrarySize)) return;

    if (micro <= 0) {
        // 1. LOOP COUNT: a beat that doesn't start later than the previous one means the track wrapped around
        if ((gateLastStart[layer] != 0xFFFF) && (startInterval <= gateLastStart[layer])) loopCount[layer]++;
        gateLastStart[layer] = startInterval;

        // 2. DECIDE (beats without valid meta always play at their own level)
        const StepMeta& m = meta[layer][bank][beatNum];
        bool play = true;
        float gain = 1.0f;
        if ((m.probability != 0) && (m.tick == startInterval)) {
            if (m.cond != COND_NONE) {
                play = condTrue(m.cond, layer);
                preResult[layer] = play;
            }
            if (play && (m.probability < 100) && ((nextRandom(gateRandom) % 100u) >= m.probability)) play = false;
            gain = m.gain * 0.01f;
        }
        gatePlay[layer] = play;
        gateGain[layer] = gain;
    }

    if (!gatePlay[layer]) {
        volume = 0.0f;
    } else {
        volume *= gateGain[layer];
    }
}

void pluginHost_beatColor(Lcd& lcd, uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval) {
    /// @brief lcd_drawBeat: selected step (STEP page) white, conditional steps magenta, otherwise colour by
    /// probability (100% keeps the track colour).

    if ((layer >= kGenTracks) || (bank >= kBankLibrarySize) || (beatNum >= kBeatLibrarySize)) return;
    if (open && (page == PAGE_STEP) && (layer == selected) && (beatNum == stepSel) && (bank == controller.getActiveSongBank())) {
        lcd.setForeColor(WHITE);
        return;
    }
    const StepMeta& m = meta[layer][bank][beatNum];
    if ((m.probability == 0) || (m.tick != startInterval)) return;
    if (m.cond != COND_NONE) {
        lcd.setForeColor(MAGENTA);
    } else if (m.probability < 95) {
        lcd.setForeColor((m.probability >= 70) ? GREEN : (m.probability >= 45) ? YELLOW : (m.probability >= 20) ? ORANGE : RED);
    }
}

void pluginHost_fileSaved(FIL& file) {
    saveBlock(file);
}

void pluginHost_fileLoaded(FIL& file, const char* path, bool songLoaded) {
    loadBlock(file, path, songLoaded);
}

#endif
