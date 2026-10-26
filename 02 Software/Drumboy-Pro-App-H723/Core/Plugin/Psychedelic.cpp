#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include <math.h>

#include "Lcd.h"

#ifndef PSY_FAST_SECTION
#define PSY_FAST_SECTION __attribute__((section(".DTCMRAM")))  // Overridable for host-side tests
#endif

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kPsySinSize                          = 1024;             // Power of two
const float    kPsyTwoPi                            = 6.28318531f;
const uint8_t  kPsyMinBlock                         = 2;                // Pixels per computed cell (square)
const uint8_t  kPsyMaxBlock                         = 8;
const uint32_t kPsySlowUs                           = 40000;            // AUTO detail: slower frames -> bigger blocks
const uint32_t kPsyFastUs                           = 22000;            //              faster frames -> smaller blocks
const uint8_t  kPsyFastFrames                       = 20;
const uint16_t kPsyMaxCells                         = kShowWidth / kPsyMinBlock + 1;
const uint32_t kPsyLabelMs                          = 1500;
const uint32_t kPsyAutoModeMs                       = 20000;            // AUTO mode: next visual every 20 s
const uint8_t  kPsyJuliaIterations                  = 24;

// clang-format on

/* Modes, Palettes, Knobs -------------------------------------------*/

enum PsyMode : uint8_t { MODE_AUTO, MODE_JULIA, MODE_PLASMA, MODE_WARP, MODE_KALEIDO, MODE_COUNT };
static const char* const kModeNames[MODE_COUNT] = {"AUTO", "JULIA", "PLASMA", "WARP", "KALEIDO"};

struct CosPalette {
    // colour(t) = a + b * cos(2pi * (c * t + d)) per channel (Inigo Quilez' cosine palettes)
    float a[3], b[3], c[3], d[3];
    const char* name;
};

static const CosPalette kPalettes[] = {
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {0.00f, 0.33f, 0.67f}, "RAINBOW"},
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f}, {0.30f, 0.20f, 0.20f}, "CANDY"},
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.5f}, {0.80f, 0.90f, 0.30f}, "ACID"},
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 0.7f, 0.4f}, {0.00f, 0.15f, 0.20f}, "FIRE"},
    {{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {2.0f, 1.0f, 0.0f}, {0.50f, 0.20f, 0.25f}, "NEON"},
    {{0.8f, 0.5f, 0.4f}, {0.2f, 0.4f, 0.2f}, {2.0f, 1.0f, 1.0f}, {0.00f, 0.25f, 0.25f}, "DUSK"},
};
const uint8_t kPaletteCount = sizeof(kPalettes) / sizeof(kPalettes[0]);

enum KnobId : uint8_t { KNOB_MODE, KNOB_SPEED, KNOB_ZOOM, KNOB_COLOUR, KNOB_DETAIL, KNOB_REACTION, KNOB_SYMMETRY, KNOB_PALETTE, KNOB_COUNT };

struct Knob {
    const char* name;
    int16_t value, min, max;
    bool wrap;
};

static const uint8_t kDetailBlocks[] = {0, 2, 3, 4, 6};  // DETAIL 0 = AUTO

/* Tables (fast DTCM, CPU only) -------------------------------------*/

static float psySin[kPsySinSize] PSY_FAST_SECTION;
static RGB16Color psyPalette[256] PSY_FAST_SECTION;
static RGB16Color psyRow[kPsyMaxCells];

static inline float fsin(float x) {
    return psySin[(int32_t)(x * (kPsySinSize / kPsyTwoPi)) & (kPsySinSize - 1)];
}

static inline float fcos(float x) {
    return fsin(x + kPsyTwoPi * 0.25f);
}

/* Psychedelic ------------------------------------------------------*/
// Full-canvas, endlessly evolving visual for show mode. Every motion is driven
// by a slowly advancing time value through several oscillators at unrelated
// frequencies, so the picture never repeats. The image is computed in square
// cells (block size adapts to keep ~25+ fps) and streamed to the LCD one band
// of cells at a time.

class Psychedelic : public Plugin {
   public:
    const char* id() const override { return "psychedelic"; }
    const char* name() const override { return "Psychedelic"; }
    bool canShow() const override { return true; }

    bool onStart() override {
        for (uint16_t i = 0; i < kPsySinSize; i++) psySin[i] = sinf(i * kPsyTwoPi / kPsySinSize);
        return true;
    }

    void onEvent(const PluginEvent& e) override {
        if (e.type == PLUGIN_EVENT_NOTE) {
            float v = e.b / 127.0f;
            if (e.a < 10 && v > energy[e.a]) energy[e.a] = v;
            if (e.a == 1) colourKick += 48.0f * v;  // Snare (track 2): colour jump
        } else if ((e.type == PLUGIN_EVENT_ENCODER) && (e.a < KNOB_COUNT)) {
            Knob& k = knobs[e.a];
            int16_t v = k.value + (int8_t)e.b;
            if (k.wrap) {
                int16_t span = k.max - k.min + 1;
                v = k.min + ((v - k.min) % span + span) % span;
            } else {
                v = (v < k.min) ? k.min : (v > k.max) ? k.max : v;
            }
            k.value = v;
            labelKnob = e.a;
            labelUntil = HAL_GetTick() + kPsyLabelMs;
        }
    }

    void onShowStart() override {
        lastTick = HAL_GetTick();
        modeStartTick = lastTick;
        labelKnob = KNOB_MODE;
        labelUntil = lastTick + 3000;  // Show the controls hint at start
    }

    void onShowFrame(Lcd& lcd) override {
        uint32_t startCycles = DWT_CYCCNT;
        updateParameters();
        buildPalette();
        render(lcd);
        drawLabel(lcd);
        adaptDetail((DWT_CYCCNT - startCycles) / (SystemCoreClock / 1000000));
    }

   private:
    Knob knobs[KNOB_COUNT] = {
        {"MODE", MODE_AUTO, 0, MODE_COUNT - 1, true},
        {"SPEED", 10, 0, 30, false},
        {"ZOOM", 10, 2, 40, false},
        {"COLOUR", 8, 0, 30, false},
        {"DETAIL", 0, 0, 4, false},
        {"REACTION", 10, 0, 20, false},
        {"SYMMETRY", 4, 1, 8, false},
        {"PALETTE", 0, 0, kPaletteCount - 1, true},
    };

    float energy[10] = {};
    float t = 0.0f;                                                     // Visual time (speed-scaled seconds)
    float colourShift = 0.0f;                                           // Palette rotation, 0..256
    float colourKick = 0.0f;
    uint32_t lastTick = 0, modeStartTick = 0, frameUs = 0;
    uint8_t autoBlock = 4, fastCount = 0, block = 4;
    uint8_t labelKnob = 0;
    uint32_t labelUntil = 0;

    // Per-frame parameters
    PsyMode mode = MODE_JULIA;
    float pulse = 0, zoom = 1, cosRot = 1, sinRot = 0, juliaX = 0, juliaY = 0, warpAmount = 0, brightness = 1, foldAngle = 0;

    void updateParameters() {
        // 1. TIME
        uint32_t tick = HAL_GetTick();
        float dt = (tick - lastTick) * 0.001f;
        if (dt > 0.1f) dt = 0.1f;
        lastTick = tick;
        t += dt * knobs[KNOB_SPEED].value * 0.1f;

        // 2. MODE (AUTO walks through the visuals)
        mode = (PsyMode)knobs[KNOB_MODE].value;
        if (mode == MODE_AUTO) mode = (PsyMode)(MODE_JULIA + ((tick - modeStartTick) / kPsyAutoModeMs) % (MODE_COUNT - 1));

        // 3. AUDIO
        const PluginAudio& audio = pluginHost_getAudio();
        float react = knobs[KNOB_REACTION].value * 0.1f;
        pulse = react * (0.7f * audio.bass + 0.5f * energy[0]);
        if (pulse > 1.0f) pulse = 1.0f;
        float shimmer = react * (audio.treble + 0.3f * (energy[2] + energy[3]));

        // 4. EVOLVING SHAPE (incommensurate frequencies: never repeats)
        zoom = knobs[KNOB_ZOOM].value * 0.1f * (1.0f + 0.25f * fsin(t * 0.11f)) * (1.0f + 0.4f * pulse);
        float rot = t * 0.05f + 0.6f * fsin(t * 0.031f);
        cosRot = fcos(rot);
        sinRot = fsin(rot);
        float cAngle = t * 0.12f + 1.3f * fsin(t * 0.017f);
        float cRadius = 0.7885f + 0.05f * fsin(t * 0.043f) + 0.04f * shimmer;
        juliaX = cRadius * fcos(cAngle);
        juliaY = cRadius * fsin(cAngle);
        warpAmount = 0.35f + 0.25f * fsin(t * 0.07f) + 0.3f * pulse;
        foldAngle = t * 0.09f + 0.5f * fsin(t * 0.023f);

        // 5. COLOUR CYCLING + BRIGHTNESS
        colourShift += dt * knobs[KNOB_COLOUR].value * 12.0f + colourKick + 6.0f * shimmer;
        colourKick = 0.0f;
        while (colourShift >= 256.0f) colourShift -= 256.0f;
        brightness = 0.8f + 0.45f * pulse;
        for (uint8_t i = 0; i < 10; i++) energy[i] *= 0.85f;
    }

    void buildPalette() {
        const CosPalette& p = kPalettes[knobs[KNOB_PALETTE].value];
        for (uint16_t i = 0; i < 256; i++) {
            float f = i / 128.0f;  // Two colour cycles across the palette
            float rgb[3];
            for (uint8_t ch = 0; ch < 3; ch++) {
                float v = (p.a[ch] + p.b[ch] * fcos(kPsyTwoPi * (p.c[ch] * f + p.d[ch]))) * brightness;
                rgb[ch] = (v < 0.0f) ? 0.0f : (v > 1.0f) ? 1.0f : v;
            }
            psyPalette[i] = (RGB16Color)((((uint16_t)(rgb[0] * 31.0f)) << 11) | (((uint16_t)(rgb[1] * 63.0f)) << 5) | (uint16_t)(rgb[2] * 31.0f));
        }
    }

    inline float plasma(float x, float y) const {
        return fsin(x * 3.1f + t) + fsin(y * 2.7f - t * 1.3f) + fsin((x + y) * 2.3f + t * 0.7f) + fsin(sqrtf(x * x + y * y) * 4.0f - t * 2.1f);
    }

    inline float julia(float x, float y) const {
        // Orbit-trap colouring: how close the orbit comes to the axes (star/filament patterns
        // everywhere, not just at the set's edge), plus iteration bands.
        uint8_t i = 0;
        float zx = x, zy = y, zx2 = x * x, zy2 = y * y;
        float trap = 10.0f;
        while ((i < kPsyJuliaIterations) && (zx2 + zy2 < 16.0f)) {
            zy = 2.0f * zx * zy + juliaY;
            zx = zx2 - zy2 + juliaX;
            zx2 = zx * zx;
            zy2 = zy * zy;
            float d = fminf(fabsf(zx), fabsf(zy));
            if (d < trap) trap = d;
            i++;
        }
        float glow = 1.0f / (0.08f + trap * 4.0f);                          // 0.4 .. 12
        if (i == kPsyJuliaIterations) return 128.0f + 10.0f * glow + 20.0f * plasma(zx * 0.5f, zy * 0.5f);
        return i * 9.0f + 14.0f * glow;
    }

    static inline float mirrorTile(float x, float period) {
        // Folds x into [-period/2, period/2], mirroring alternate copies (seamless repeats across the wide canvas)
        float half = period * 0.5f;
        float m = x + half;
        m -= 2.0f * period * floorf(m / (2.0f * period));
        return (m < period) ? m - half : 2.0f * period - m - half;
    }

    inline float kaleido(float x, float y) const {
        // Kaleidoscopic fold: mirror into one wedge, rotate, shift, repeat
        uint8_t folds = (uint8_t)knobs[KNOB_SYMMETRY].value;
        float c = fcos(foldAngle), s = fsin(foldAngle);
        for (uint8_t k = 0; k < folds; k++) {
            x = fabsf(x);
            y = fabsf(y);
            if (y > x) {
                float tmp = x;
                x = y;
                y = tmp;
            }
            float nx = x * c - y * s;
            y = x * s + y * c;
            x = nx - 0.35f;
        }
        return 32.0f * plasma(x * 1.5f, y * 1.5f) + 40.0f * fsin(x * y * 6.0f + t);
    }

    inline float value(float x, float y) const {
        switch (mode) {
            case MODE_PLASMA:
                return 32.0f * plasma(x, y);
            case MODE_WARP: {
                float wx = x + warpAmount * fsin(y * 1.7f + t * 0.9f);
                float wy = y + warpAmount * fsin(x * 1.9f - t * 1.1f);
                return julia(mirrorTile(wx, 3.0f), wy) + 12.0f * plasma(x, y);
            }
            case MODE_KALEIDO:
                return kaleido(x, y);
            case MODE_JULIA:
            default:
                return julia(mirrorTile(x, 2.6f), y) + 8.0f * plasma(x * 0.5f, y * 0.5f);
        }
    }

    void render(Lcd& lcd) {
        // 1. GEOMETRY: canvas centre = origin; ~1.25 units from centre to top edge at zoom 1
        block = (knobs[KNOB_DETAIL].value == 0) ? autoBlock : kDetailBlocks[knobs[KNOB_DETAIL].value];
        const uint16_t w = kShowWidth, h = kShowHeight;
        const uint16_t cols = (w + block - 1) / block;
        const float scale = 1.25f / (zoom * (h * 0.5f));

        // 2. BANDS OF CELLS: compute one row of cells, then stream it as 'block' pixel lines
        for (uint16_t y0 = 0; y0 < h; y0 += block) {
            uint16_t bandHeight = (h - y0 < block) ? h - y0 : block;
            float v = ((y0 + bandHeight * 0.5f) - h * 0.5f) * scale;

            for (uint16_t c = 0; c < cols; c++) {
                float u = ((c * block + block * 0.5f) - w * 0.5f) * scale;
                float x = u * cosRot - v * sinRot;
                float y = u * sinRot + v * cosRot;
                int32_t index = (int32_t)(value(x, y) + colourShift);
                psyRow[c] = psyPalette[index & 0xFF];
            }

            lcd.beginStream(kShowX, kShowY + y0, kShowX + w - 1, kShowY + y0 + bandHeight - 1);
            for (uint16_t line = 0; line < bandHeight; line++) {
                for (uint16_t c = 0; c < cols; c++) {
                    RGB16Color color = psyRow[c];
                    uint16_t cellWidth = (c == cols - 1) ? w - c * block : block;
                    for (uint16_t k = 0; k < cellWidth; k++) lcd.streamPixel(color);
                }
            }
            lcd.endStream();
        }
    }

    void drawLabel(Lcd& lcd) {
        if (HAL_GetTick() >= labelUntil) return;
        char text[96];
        const Knob& k = knobs[labelKnob];
        if (labelKnob == KNOB_MODE) {
            snprintf(text, sizeof(text), "PSYCHEDELIC  MODE %s  (KNOBS 1-8, FILE EXIT)", kModeNames[k.value]);
        } else if (labelKnob == KNOB_PALETTE) {
            snprintf(text, sizeof(text), "PALETTE %s", kPalettes[k.value].name);
        } else if (labelKnob == KNOB_DETAIL) {
            snprintf(text, sizeof(text), "DETAIL %s  (BLOCK %u PX, %lu MS)", (k.value == 0) ? "AUTO" : "FIXED", block, (unsigned long)(frameUs / 1000));
        } else {
            snprintf(text, sizeof(text), "%s %d", k.name, k.value);
        }
        lcd.setFont(FONT_07x09);
        lcd.setAlignment(LEFT);
        lcd.setForeColor(WHITE);
        lcd.setBackColor(BLACK);
        lcd.drawText(text, strlen(text), kShowX + 8, kShowY + 6);
    }

    void adaptDetail(uint32_t us) {
        frameUs = us;
        if (us > kPsySlowUs) {
            if (autoBlock < kPsyMaxBlock) autoBlock++;
            fastCount = 0;
        } else if ((us < kPsyFastUs) && (autoBlock > kPsyMinBlock)) {
            if (++fastCount >= kPsyFastFrames) {
                autoBlock--;
                fastCount = 0;
            }
        } else {
            fastCount = 0;
        }
    }
};

static Psychedelic psychedelic;

Plugin* builtin_psychedelic() {
    return &psychedelic;
}

#endif
