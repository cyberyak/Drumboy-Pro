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
const uint16_t kGenLine2Y                           = 16;               // Selected track / status / hints
const uint16_t kGenTextWidth                        = 920;
const uint16_t kGenMarkerX                          = 6;                // Left margin beside the track rows
const uint32_t kGenStatusMs                         = 4000;
const uint32_t kGenRedrawMs                         = 500;
const uint8_t  kGenFillChoices[]                    = {0, 2, 4, 8};
const uint16_t kGenSongStepsMax                     = 12 * 48;          // 12 bars of 12/4

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

/* Step Meta (per generated beat) -----------------------------------*/
// Probability and velocity the stock beat format can't hold. Keyed by beat slot
// and validated against the beat's start tick, so beats edited by hand afterwards
// simply play normally.

struct StepMeta {
    uint16_t tick;
    uint8_t probability;                                                // 0 = not generated
    uint8_t gain;                                                       // Volume multiplier x100
};

static StepMeta meta[kGenTracks][kBankLibrarySize][kBeatLibrarySize] GEN_FAST_SECTION;

/* Undo (one level: the bank as it was before the last generation) --*/

static Bank undoBanks[kGenTracks] GEN_FAST_SECTION;
static StepMeta undoMeta[kGenTracks][kBeatLibrarySize] GEN_FAST_SECTION;
static bool undoFill[kGenTracks];
static uint8_t undoBank = 0;
static bool undoValid = false;

/* Page State -------------------------------------------------------*/

enum GenKnob : uint8_t { KNOB_STYLE, KNOB_DENSITY, KNOB_VARIATION, KNOB_SWING, KNOB_HUMANIZE, KNOB_DYNAMICS, KNOB_PROBABILITY, KNOB_RATCHET };

static bool open = false;
static bool clearPending = false;
static bool fillHeld = false;
static bool dirty = false;
static uint32_t lastDrawTick = 0;
static uint8_t selected = 0;
static uint8_t markerRow = 0xFF;
static bool locked[kGenTracks];
static int8_t roleOverride[kGenTracks];                                // -1 = automatic
static GenRole detected[kGenTracks];
static uint8_t fillChoice = 2;                                          // Index into kGenFillChoices
static GenParams params = {STYLE_TECHNO, 60, 30, 50, 20, 60, 40, 20, 4, 1};
static char status[112] = "";
static bool statusError = false;
static uint32_t statusUntil = 0;
static GenStep steps[kGenMaxSteps];
static uint32_t occupied[(kGenSongStepsMax + 31) / 32];

static uint32_t gateRandom = 0x2545F491u;

/* Helpers ----------------------------------------------------------*/

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

static void markOccupied(uint16_t tick, uint8_t beatsPerBar) {
    (void)beatsPerBar;
    uint16_t step = (tick + kGenTicksPerStep / 2) / kGenTicksPerStep;
    if (step < kGenSongStepsMax) occupied[step >> 5] |= (1u << (step & 31));
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
    b.lastActiveBeatNum = -1;
    b.playBeatNum = -1;

    uint8_t written = 0;
    for (uint8_t k = 0; (k < count) && (written < kBeatLibrarySize); k++) {
        uint16_t start = list[k].tick;
        if (start >= song) break;
        uint16_t next = ((k + 1 < count) && (list[k + 1].tick < song)) ? list[k + 1].tick : song;
        if (next <= start) continue;

        uint8_t pattern = 0;                                            // FLAT
        float base = 1.0f;
        uint16_t end = next;
        if (list[k].ratchet) {
            pattern = (list[k].ratchet == 2) ? 3 : (list[k].ratchet == 3) ? 4 : 5;  // BASIC_2 / BASIC_3 / BASIC_4
            end = (next - start > kGenTicksPerStep) ? start + kGenTicksPerStep : next;
            anyRatchet = true;
        } else if (list[k].velocity < 60) {
            pattern = 1;                                                // GHOST (stock plays it at 50%)
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
    if (bank == PluginAccess::activeBank(c)) PluginAccess::drawSong(c, track, bank);
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

static void generate(Controller& c, bool newSeed) {
    uint8_t bank = PluginAccess::activeBank(c);
    uint8_t beatsPerBar = PluginAccess::beatsPerBar(c);
    uint8_t stepsPerBar = beatsPerBar * 4;
    uint8_t bars = PluginAccess::bars(c);

    if (newSeed) params.seed = (params.seed * 1103515245u + 12345u + HAL_GetTick()) % 10000u;
    params.fillEvery = kGenFillChoices[fillChoice];
    snapshot(c, bank);

    // 1. LOCKED BACKBONE TRACKS STILL GUIDE THE PERCUSSION
    memset(occupied, 0, sizeof(occupied));
    for (uint8_t i = 0; i < kGenTracks; i++) {
        if (!locked[i] || !isBackbone(roleOf(i))) continue;
        Bank& b = PluginAccess::layer(c, i).bankLibrary[bank];
        for (int16_t k = 0; k <= b.lastActiveBeatNum; k++) markOccupied(b.beatLibrary[k].getStartInterval(), beatsPerBar);
    }

    // 2. KICK / SNARE / CLAP FIRST, THEN EVERYTHING ELSE
    uint8_t generated = 0;
    for (uint8_t pass = 0; pass < 2; pass++) {
        for (uint8_t i = 0; i < kGenTracks; i++) {
            GenRole role = roleOf(i);
            if (locked[i] || (isBackbone(role) != (pass == 0))) continue;
            uint8_t n = gen_generate(role, params, stepsPerBar, bars, i, occupied, steps);
            writeTrack(c, i, bank, steps, n);
            if (isBackbone(role)) {
                for (uint8_t k = 0; k < n; k++) markOccupied(steps[k].tick, beatsPerBar);
            }
            generated++;
        }
    }
    setStatus(false, "GENERATED %u TRACKS (%s, SEED %lu) INTO BANK %u - FILE > SAVE TO KEEP, CLEAR = UNDO", generated,
              kGenStyleNames[params.style], (unsigned long)params.seed, bank + 1);
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

    lcd.setFont(FONT_05x07);
    lcd.setAlignment(LEFT);
    lcd.setBackColor(BLACK);
    lcd.clearRect(kGenTextX, kGenLine1Y - 1, kGenTextWidth, 19);

    // 1. PARAMETERS (knobs 1-8, LEFT/RIGHT fill)
    char fill[12];
    (kGenFillChoices[fillChoice] == 0) ? snprintf(fill, sizeof(fill), "OFF") : snprintf(fill, sizeof(fill), "%u BARS", kGenFillChoices[fillChoice]);
    snprintf(text, sizeof(text), "GENERATOR  %s  DENS %u  VAR %u  SWING %u  HUMAN %u  DYN %u  PROB %u  RATCH %u  FILL %s  SEED %lu",
             kGenStyleNames[params.style], params.density, params.variation, params.swing, params.humanize, params.dynamics,
             params.probability, params.ratchet, fill, (unsigned long)params.seed);
    lcd.setForeColor(CYAN);
    lcd.drawText(text, strlen(text), kGenTextX, kGenLine1Y);

    // 2. STATUS, OR SELECTED TRACK + KEYS
    if (HAL_GetTick() < statusUntil) {
        lcd.setForeColor(statusError ? RED : YELLOW);
        lcd.drawText(status, strlen(status), kGenTextX, kGenLine2Y);
    } else {
        GenRole role = roleOf(selected);
        snprintf(text, sizeof(text), "TRACK %02u %s%s%s   GLOBAL GENERATE  PASTE NEW SEED  CLEAR UNDO  SELECT LOCK  COPY ROLE  L/R FILL  FILE CLOSE",
                 selected + 1, kGenRoleNames[role], (roleOverride[selected] >= 0) ? " (SET)" : " (AUTO)", locked[selected] ? "  LOCKED" : "");
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
    memset(meta, 0, sizeof(meta));  // DTCM is not zeroed at boot
    for (uint8_t i = 0; i < kGenTracks; i++) {
        roleOverride[i] = -1;
        locked[i] = false;
    }
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
    // 1. FILL HELD (tracked open or closed: FILL + STYLE toggles the page)
    if ((e.type == PLUGIN_EVENT_KEY) && (e.a == kKeypadLeft) && (e.b == KEY_FILL)) {
        fillHeld = e.c;
        return open;
    }

    // 2. CLOSED: WATCH FOR FILL + STYLE
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

    // 3. OPEN: KNOBS = PARAMETERS
    if (e.type == PLUGIN_EVENT_ENCODER) {
        int8_t d = (int8_t)e.b;
        auto step = [d](uint8_t& v, int16_t perClick, int16_t lo, int16_t hi) {
            int16_t n = (int16_t)v + d * perClick;
            v = (uint8_t)((n < lo) ? lo : (n > hi) ? hi : n);
        };
        switch (e.a) {
            case KNOB_STYLE:
                params.style = (uint8_t)((params.style + STYLE_COUNT + d) % STYLE_COUNT);
                break;
            case KNOB_DENSITY: step(params.density, 5, 0, 100); break;
            case KNOB_VARIATION: step(params.variation, 5, 0, 100); break;
            case KNOB_SWING: step(params.swing, 1, 50, 75); break;
            case KNOB_HUMANIZE: step(params.humanize, 5, 0, 100); break;
            case KNOB_DYNAMICS: step(params.dynamics, 5, 0, 100); break;
            case KNOB_PROBABILITY: step(params.probability, 5, 0, 100); break;
            case KNOB_RATCHET: step(params.ratchet, 5, 0, 100); break;
            default: break;
        }
        dirty = true;
        return true;
    }
    if (e.type != PLUGIN_EVENT_KEY) return false;  // Steps, notes, MIDI still reach plugins
    if (!e.c) return true;                          // Releases (stock gets them anyway)

    // 4. OPEN: KEYS
    if (e.a == kKeypadLayer) {
        if (e.b < kGenTracks) selected = e.b;  // Layer key selects that track
    } else if (e.a == kKeypadRight) {
        if (e.b == KEY_FILE) closePage();
    } else if (e.a == kKeypadLeft) {
        switch (e.b) {
            case KEY_UP: selected = (selected + kGenTracks - 1) % kGenTracks; break;
            case KEY_DOWN: selected = (selected + 1) % kGenTracks; break;
            case KEY_ADD: locked[selected] = !locked[selected]; break;  // Printed "SELECT"
            case KEY_COPY:
                // AUTO -> each role -> AUTO
                roleOverride[selected] = (roleOverride[selected] + 2 > ROLE_COUNT) ? -1 : roleOverride[selected] + 1;
                break;
            case KEY_CENTER: generate(c, false); break;  // Printed "GLOBAL"
            case KEY_PASTE: generate(c, true); break;
            case KEY_CLEAR: undo(c); break;
            case KEY_LEFT: fillChoice = (fillChoice + sizeof(kGenFillChoices) - 1) % sizeof(kGenFillChoices); break;
            case KEY_RIGHT: fillChoice = (fillChoice + 1) % sizeof(kGenFillChoices); break;
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

/* Beat Hooks (called from stock code) ------------------------------*/

void pluginHost_beatGate(uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval, float& volume) {
    /// @brief Play-timer interrupt: per-step probability roll and velocity for generated beats.

    if ((layer >= kGenTracks) || (bank >= kBankLibrarySize) || (beatNum >= kBeatLibrarySize)) return;
    const StepMeta& m = meta[layer][bank][beatNum];
    if ((m.probability == 0) || (m.tick != startInterval)) return;  // Not generated, or edited by hand

    if (m.probability < 100) {
        gateRandom ^= gateRandom << 13;
        gateRandom ^= gateRandom >> 17;
        gateRandom ^= gateRandom << 5;
        if ((gateRandom % 100u) >= m.probability) {
            volume = 0.0f;
            return;
        }
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

#endif
