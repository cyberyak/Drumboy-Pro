#include "PatternGen.h"

#include <ctype.h>
#include <string.h>

#ifndef GEN_WORK_SECTION
#define GEN_WORK_SECTION __attribute__((section(".DTCMRAM")))  // Overridable for host-side tests
#endif

/* Names ------------------------------------------------------------*/

const char* const kGenStyleNames[STYLE_COUNT] = {"TECHNO", "ELECTRO", "BREAKBEAT"};
const char* const kGenRoleNames[ROLE_COUNT] = {"KICK", "SNARE", "CLAP", "HAT-C", "HAT-O", "RIDE", "CRASH", "TOM",
                                               "RIM", "SHAKER", "HANDPERC", "BELL", "WOOD", "PERC", "BASS", "FX"};

/* Recipes ----------------------------------------------------------*/
// Per style and role: a weight (0-100) for each 16th step of a 4/4 bar. Steps whose
// weight clears the density threshold play; 95+ are anchors (always play, 100%).
// An optional Euclidean rhythm E(k,n) is laid over the weights, 'complement' makes
// the role avoid steps used by kick and snare, and 'fill' is its phrase-end behaviour.

enum FillKind : uint8_t { FILL_NONE, FILL_ROLL, FILL_RUN, FILL_DROP, FILL_CRASH, FILL_OPEN };

struct Recipe {
    uint8_t weight[16];
    uint8_t euclidK, euclidN, euclidRotate;
    int8_t densityBias;                                                 // Added to the global density
    uint8_t ghost;                                                      // % chance a weak step plays as a ghost note
    uint8_t ratchetBias;                                                // 0-100: how ratchet-prone the role is
    uint8_t humanizeScale;                                              // % of the global humanize
    bool complement;
    FillKind fill;
};

#define W(...) {__VA_ARGS__}

// clang-format off
static const Recipe kRecipes[STYLE_COUNT][ROLE_COUNT] = {
    // TECHNO: four-on-the-floor kick, clap/snare on 2 and 4, off-beat open hats, driving 16th hats, rolling off-beat bass
    {
        /* KICK     */ {W(100,0,4,0, 100,0,4,0, 100,0,4,0, 100,0,10,22),   0,0,0,   0,  0,  5,  25, false, FILL_NONE},
        /* SNARE    */ {W(0,0,0,0, 95,0,0,8, 0,0,8,0, 95,0,12,0),          0,0,0,   0, 40, 40, 100, false, FILL_ROLL},
        /* CLAP     */ {W(0,0,0,0, 100,0,0,0, 0,0,0,0, 100,0,0,14),        0,0,0,   0, 20, 25, 100, false, FILL_ROLL},
        /* HAT-C    */ {W(45,30,92,30, 45,30,92,30, 45,30,92,30, 45,30,92,52), 0,0,0, 0, 50, 60, 100, false, FILL_NONE},
        /* HAT-O    */ {W(0,0,100,0, 0,0,100,0, 0,0,100,0, 0,0,100,0),     0,0,0,   0,  0,  5,  60, false, FILL_OPEN},
        /* RIDE     */ {W(72,22,62,22, 72,22,62,22, 72,22,62,22, 72,22,62,30), 0,0,0, -10, 30, 10, 100, false, FILL_NONE},
        /* CRASH    */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            0,0,0,   0,  0,  0,  50, false, FILL_CRASH},
        /* TOM      */ {W(0,0,0,0, 0,0,0,0, 0,0,0,12, 0,0,22,0),           0,0,0,   0, 20, 20, 100, true,  FILL_RUN},
        /* RIM      */ {W(10,0,10,0, 10,0,10,0, 10,0,10,0, 10,0,10,0),     3,8,2,   0, 30, 15, 100, true,  FILL_NONE},
        /* SHAKER   */ {W(70,45,70,45, 70,45,70,45, 70,45,70,45, 70,45,70,45), 0,0,0, -15, 60, 20, 100, false, FILL_NONE},
        /* HANDPERC */ {W(0,10,0,10, 0,10,0,10, 0,10,0,10, 0,10,0,10),     5,16,3,  0, 40, 15, 100, true,  FILL_NONE},
        /* BELL     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            3,16,2, -10, 20, 10, 100, true,  FILL_NONE},
        /* WOOD     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            3,8,1,  -5, 20, 10, 100, true,  FILL_NONE},
        /* PERC     */ {W(0,8,0,8, 0,8,0,8, 0,8,0,8, 0,8,0,8),             5,16,6,  0, 40, 20, 100, true,  FILL_NONE},
        /* BASS     */ {W(0,0,92,40, 0,0,92,40, 0,0,92,40, 0,0,92,40),     0,0,0,   0, 20, 10,  30, false, FILL_NONE},
        /* FX       */ {W(30,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),           0,0,0, -30,  0,  0, 100, false, FILL_CRASH},
    },
    // ELECTRO: syncopated 808 kick, snare on 2 and 4, straight 16th hats, 3-3-2 cowbell, clave, tom runs
    {
        /* KICK     */ {W(100,0,0,0, 0,0,92,0, 0,0,88,0, 0,20,0,12),       0,0,0,   0,  0, 10,  25, false, FILL_NONE},
        /* SNARE    */ {W(0,0,0,0, 100,0,0,0, 0,0,0,0, 100,0,0,15),        0,0,0,   0, 30, 30, 100, false, FILL_ROLL},
        /* CLAP     */ {W(0,0,0,0, 95,0,0,0, 0,0,0,0, 95,0,0,25),          0,0,0,   0, 20, 25, 100, false, FILL_ROLL},
        /* HAT-C    */ {W(92,55,72,55, 92,55,72,55, 92,55,72,55, 92,55,72,62), 0,0,0, 0, 40, 40,  60, false, FILL_NONE},
        /* HAT-O    */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,85,0),            0,0,0,   0,  0,  5,  60, false, FILL_OPEN},
        /* RIDE     */ {W(50,0,30,0, 50,0,30,0, 50,0,30,0, 50,0,30,0),     0,0,0, -10, 20, 10, 100, false, FILL_NONE},
        /* CRASH    */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            0,0,0,   0,  0,  0,  50, false, FILL_CRASH},
        /* TOM      */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,35,28,28),          0,0,0,   0, 10, 30, 100, true,  FILL_RUN},
        /* RIM      */ {W(0,0,0,40, 0,0,0,0, 0,0,0,40, 0,0,0,0),           0,0,0,   0, 20, 10, 100, true,  FILL_NONE},
        /* SHAKER   */ {W(60,40,60,40, 60,40,60,40, 60,40,60,40, 60,40,60,40), 0,0,0, -15, 50, 15, 100, false, FILL_NONE},
        /* HANDPERC */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            7,16,1,  0, 40, 15, 100, true,  FILL_NONE},
        /* BELL     */ {W(95,0,0,90, 0,0,90,0, 95,0,0,90, 0,0,90,0),       0,0,0,   0, 10, 10,  60, false, FILL_NONE},
        /* WOOD     */ {W(100,0,0,100, 0,0,100,0, 0,0,100,0, 100,0,0,0),   0,0,0,   0, 10,  5,  60, false, FILL_NONE},
        /* PERC     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            5,16,2,  0, 40, 20, 100, true,  FILL_NONE},
        /* BASS     */ {W(100,0,0,82, 0,0,90,0, 0,0,86,0, 0,72,0,0),       0,0,0,   0, 10, 10,  30, false, FILL_NONE},
        /* FX       */ {W(25,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),           0,0,0, -30,  0,  0, 100, false, FILL_CRASH},
    },
    // BREAKBEAT: break-style kick (1, 3, 11, 12), snare 2 and 4 with ghost notes, 8th hats, swung, rolls and kick drops
    {
        /* KICK     */ {W(100,0,82,0, 0,0,0,0, 0,0,92,88, 0,0,0,0),        0,0,0,   0, 20, 10,  60, false, FILL_DROP},
        /* SNARE    */ {W(0,0,0,0, 100,0,0,58, 0,52,0,0, 100,0,0,60),      0,0,0,   0, 70, 40, 100, false, FILL_ROLL},
        /* CLAP     */ {W(0,0,0,0, 85,0,0,0, 0,0,0,0, 85,0,0,0),           0,0,0, -10, 20, 20, 100, false, FILL_ROLL},
        /* HAT-C    */ {W(92,22,82,22, 92,22,82,22, 92,22,82,22, 92,22,82,35), 0,0,0, 0, 40, 35, 100, false, FILL_NONE},
        /* HAT-O    */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,80,0),            0,0,0,   0,  0,  5, 100, false, FILL_OPEN},
        /* RIDE     */ {W(80,0,62,0, 80,0,62,0, 80,0,62,0, 80,0,62,0),     0,0,0, -10, 30, 10, 100, false, FILL_NONE},
        /* CRASH    */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            0,0,0,   0,  0,  0,  50, false, FILL_CRASH},
        /* TOM      */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,25,25),           0,0,0,   0, 20, 30, 100, true,  FILL_RUN},
        /* RIM      */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            3,16,5,  0, 40, 15, 100, true,  FILL_NONE},
        /* SHAKER   */ {W(60,35,60,35, 60,35,60,35, 60,35,60,35, 60,35,60,35), 0,0,0, -15, 60, 15, 100, false, FILL_NONE},
        /* HANDPERC */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            5,16,1,  0, 50, 20, 100, true,  FILL_NONE},
        /* BELL     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            2,8,3, -10, 20, 10, 100, true,  FILL_NONE},
        /* WOOD     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            3,8,0,  -5, 20, 10, 100, true,  FILL_NONE},
        /* PERC     */ {W(0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),            5,16,4,  0, 40, 20, 100, true,  FILL_NONE},
        /* BASS     */ {W(100,0,0,0, 0,0,0,80, 0,0,90,0, 0,0,70,0),        0,0,0,   0, 20, 10,  60, false, FILL_NONE},
        /* FX       */ {W(25,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0),           0,0,0, -30,  0,  0, 100, false, FILL_CRASH},
    },
};
// clang-format on

/* Random (stateless hash: same seed -> same pattern) ---------------*/

static uint32_t mix(uint32_t a) {
    a ^= a >> 16;
    a *= 0x7FEB352Du;
    a ^= a >> 15;
    a *= 0x846CA68Bu;
    a ^= a >> 16;
    return a;
}

static float rnd(uint32_t seed, uint32_t track, uint32_t bar, uint32_t step, uint32_t stream) {
    uint32_t h = mix(seed ^ mix(track * 0x9E3779B9u ^ bar * 0x85EBCA6Bu ^ step * 0xC2B2AE35u ^ stream * 0x27D4EB2Fu));
    return (h >> 8) * (1.0f / 16777216.0f);  // 0 <= r < 1
}

enum RandomStream : uint32_t { RS_BASE = 1, RS_BAR, RS_GHOST, RS_RATCHET, RS_RATCHET_N, RS_HUMAN_A, RS_HUMAN_B, RS_VEL };

static inline int32_t clampi(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* Role Detection ---------------------------------------------------*/

struct RoleKeyword {
    const char* word;
    GenRole role;
};

// Exact instrument folder names of the factory sample library (case-insensitive)
static const RoleKeyword kFolderRoles[] = {
    {"kick", ROLE_KICK},       {"snare", ROLE_SNARE},   {"clap", ROLE_CLAP},          {"snap", ROLE_CLAP},
    {"rim", ROLE_RIM},         {"hhclosed", ROLE_HAT_CLOSED}, {"hhopen", ROLE_HAT_OPEN}, {"ride", ROLE_RIDE},
    {"crash", ROLE_CRASH},     {"tom", ROLE_TOM},       {"perc", ROLE_PERC},          {"shaker", ROLE_SHAKER},
    {"tambourine", ROLE_SHAKER}, {"conga", ROLE_HAND_PERC}, {"bongo", ROLE_HAND_PERC}, {"cowbell", ROLE_BELL},
    {"clave", ROLE_WOOD},      {"bass", ROLE_BASS},     {"fx", ROLE_FX},
};

// Keywords in user folder / sample names, checked in order (open hat before closed hat, kick before bass)
static const RoleKeyword kKeywordRoles[] = {
    {"hhopen", ROLE_HAT_OPEN}, {"openhat", ROLE_HAT_OPEN}, {"open hat", ROLE_HAT_OPEN}, {"ohh", ROLE_HAT_OPEN},
    {"hh_o", ROLE_HAT_OPEN},   {"_oh", ROLE_HAT_OPEN},
    {"kick", ROLE_KICK},       {"bassdrum", ROLE_KICK}, {"bass drum", ROLE_KICK},     {"kik", ROLE_KICK},
    {"bd_", ROLE_KICK},        {"_bd", ROLE_KICK},
    {"snare", ROLE_SNARE},     {"snr", ROLE_SNARE},     {"sd_", ROLE_SNARE},          {"_sd", ROLE_SNARE},
    {"clap", ROLE_CLAP},       {"snap", ROLE_CLAP},     {"cp_", ROLE_CLAP},
    {"rimshot", ROLE_RIM},     {"rim", ROLE_RIM},       {"stick", ROLE_RIM},
    {"hhclosed", ROLE_HAT_CLOSED}, {"hihat", ROLE_HAT_CLOSED}, {"hi-hat", ROLE_HAT_CLOSED}, {"hat", ROLE_HAT_CLOSED},
    {"chh", ROLE_HAT_CLOSED},  {"hh", ROLE_HAT_CLOSED},
    {"ride", ROLE_RIDE},       {"crash", ROLE_CRASH},   {"cymbal", ROLE_CRASH},       {"splash", ROLE_CRASH},
    {"china", ROLE_CRASH},     {"tom", ROLE_TOM},
    {"shaker", ROLE_SHAKER},   {"shk", ROLE_SHAKER},    {"maraca", ROLE_SHAKER},      {"cabasa", ROLE_SHAKER},
    {"tamb", ROLE_SHAKER},
    {"conga", ROLE_HAND_PERC}, {"bongo", ROLE_HAND_PERC}, {"tabla", ROLE_HAND_PERC},  {"djembe", ROLE_HAND_PERC},
    {"timbal", ROLE_HAND_PERC},
    {"cowbell", ROLE_BELL},    {"agogo", ROLE_BELL},    {"bell", ROLE_BELL},
    {"clave", ROLE_WOOD},      {"wood", ROLE_WOOD},     {"block", ROLE_WOOD},
    {"808", ROLE_BASS},        {"bass", ROLE_BASS},     {"sub", ROLE_BASS},
    {"perc", ROLE_PERC},
    {"fx", ROLE_FX},           {"noise", ROLE_FX},      {"riser", ROLE_FX},           {"sweep", ROLE_FX},
    {"vox", ROLE_FX},          {"vocal", ROLE_FX},
};

static const GenRole kTrackDefaultRoles[] = {ROLE_KICK, ROLE_SNARE, ROLE_HAT_CLOSED, ROLE_HAT_OPEN, ROLE_PERC,
                                             ROLE_CLAP, ROLE_FX, ROLE_WOOD, ROLE_BELL, ROLE_PERC};

static void lowerBase(const char* in, char* out, size_t size) {
    // Last path component, without extension, lower case
    const char* base = in;
    for (const char* c = in; *c != '\0'; c++) {
        if ((*c == '/') || (*c == '\\')) base = c + 1;
    }
    size_t n = 0;
    for (const char* c = base; (*c != '\0') && (*c != '.') && (n + 1 < size); c++) out[n++] = (char)tolower((unsigned char)*c);
    out[n] = '\0';
}

GenRole gen_detectRole(const char* folder, const char* instrument, const char* sample, uint8_t trackIndex) {
    char names[3][48];
    lowerBase(folder ? folder : "", names[0], sizeof(names[0]));
    lowerBase(instrument ? instrument : "", names[1], sizeof(names[1]));
    lowerBase(sample ? sample : "", names[2], sizeof(names[2]));

    // 1. EXACT FACTORY FOLDER / INSTRUMENT NAME
    for (uint8_t n = 0; n < 2; n++) {
        for (const RoleKeyword& k : kFolderRoles) {
            if (strcmp(names[n], k.word) == 0) return k.role;
        }
    }

    // 2. KEYWORDS IN ANY NAME
    for (const RoleKeyword& k : kKeywordRoles) {
        for (uint8_t n = 0; n < 3; n++) {
            if (strstr(names[n], k.word) != nullptr) return k.role;
        }
    }

    // 3. USUAL ROLE OF THE TRACK NUMBER
    return kTrackDefaultRoles[trackIndex % 10];
}

/* Generation -------------------------------------------------------*/

static bool euclidHit(uint8_t step, uint8_t k, uint8_t n, uint8_t rotate) {
    if ((k == 0) || (n == 0)) return false;
    return (((step + rotate) % n) * k) % n < k;
}

static bool isFillBar(uint8_t bar, uint8_t bars, uint8_t fillEvery) {
    if (fillEvery == 0) return false;
    if (fillEvery > bars) return bar == bars - 1;  // Short loops still get a fill at the end
    return ((bar + 1) % fillEvery) == 0;
}

static bool isAfterFillBar(uint8_t bar, uint8_t bars, uint8_t fillEvery) {
    if ((fillEvery == 0) || (bar == 0)) return false;
    if (fillEvery > bars) return false;
    return (bar % fillEvery) == 0;
}

uint8_t gen_generate(GenRole role, const GenParams& p, uint8_t stepsPerBar, uint8_t bars, uint8_t trackIndex,
                     const uint32_t* occupied, GenStep* out) {
    const Recipe& r = kRecipes[p.style % STYLE_COUNT][role % ROLE_COUNT];
    const uint8_t fillLength = (stepsPerBar >= 8) ? 4 : stepsPerBar / 2;  // Last beat of the bar
    uint8_t count = 0;

    // Room for every candidate step of the song (trimmed to the stock limits at the end)
    static GenStep candidates[12 * 48] GEN_WORK_SECTION;  // 3.5 KB, CPU-only
    uint16_t candidateCount = 0;

    const uint32_t barSeed = p.seed ^ (p.mutation * 0x9E3779B9u);  // EVOLVE re-rolls details, not the groove
    int32_t density = clampi((int32_t)p.density + r.densityBias, 0, 100);
    int32_t threshold = 100 - density;

    for (uint8_t bar = 0; bar < bars; bar++) {
        bool fillBar = isFillBar(bar, bars, p.fillEvery);
        bool afterFill = isAfterFillBar(bar, bars, p.fillEvery);

        for (uint8_t s = 0; s < stepsPerBar; s++) {
            uint16_t songStep = (uint16_t)bar * stepsPerBar + s;
            bool inFillZone = fillBar && (s >= stepsPerBar - fillLength);

            // 1. WEIGHT: recipe + Euclidean overlay + base groove noise + per-bar variation
            int32_t w = r.weight[s % 16];
            if (euclidHit(s, r.euclidK, r.euclidN, r.euclidRotate)) w = (w > 72) ? w : 72;
            bool anchor = (w >= 95);
            if (!anchor && w > 0) {
                w += (int32_t)((rnd(p.seed, trackIndex, 0xFFFF, s, RS_BASE) - 0.5f) * 24.0f);
                w += (int32_t)((rnd(barSeed, trackIndex, bar, s, RS_BAR) - 0.5f) * p.variation * 0.8f);
            } else if (!anchor && w == 0 && p.variation > 0) {
                // Rare surprise hits on empty steps, more with variation
                if (rnd(barSeed, trackIndex, bar, s, RS_BAR) < p.variation * 0.0012f) w = 60;
            }
            if (r.complement && (occupied != nullptr) && (occupied[songStep >> 5] & (1u << (songStep & 31)))) w = w * 4 / 10;

            // 2. PHRASE FILLS
            int32_t fillVelocity = -1;
            uint8_t fillRatchet = 0;
            if (inFillZone) {
                uint8_t pos = s - (stepsPerBar - fillLength);  // 0..fillLength-1
                switch (r.fill) {
                    case FILL_ROLL:
                        w = 100;
                        fillVelocity = 70 + pos * 50 / fillLength;
                        if (pos >= fillLength - 2) fillRatchet = (p.ratchet > 40) ? 4 : 2;
                        break;
                    case FILL_RUN:
                        w = 100;
                        fillVelocity = 115 - pos * 10;
                        break;
                    case FILL_DROP:
                        if (pos > 0) w = 0;
                        break;
                    case FILL_OPEN:
                        if (pos == fillLength - 1) w = 100;
                        break;
                    default:
                        break;
                }
            }
            if ((r.fill == FILL_CRASH) && afterFill && (s == 0)) {
                w = 100;
                fillVelocity = 120;
            }
            if (w <= 0 || w < threshold || (r.fill == FILL_CRASH && w < 100 && r.weight[s % 16] == 0)) continue;
            anchor = anchor || (w >= 95);

            // 3. VELOCITY: weight-based accents, spread by DYNAMICS, ghosts on weak steps
            int32_t velocity;
            if (fillVelocity >= 0) {
                velocity = fillVelocity;
            } else {
                int32_t base = 50 + w * 77 / 100;
                velocity = 127 - (127 - base) * p.dynamics / 100;
                velocity += (int32_t)((rnd(barSeed, trackIndex, bar, s, RS_VEL) - 0.5f) * p.dynamics * 0.2f);
                if (!anchor && (w < 70) && (rnd(barSeed, trackIndex, bar, s, RS_GHOST) * 100.0f < r.ghost)) {
                    velocity = 22 + velocity * (100 - p.dynamics / 2) / 300;  // Ghost note
                }
            }

            // 4. PROBABILITY: anchors always play; steps just above the threshold are the least certain
            int32_t probability = 100;
            if (!anchor && fillVelocity < 0 && w < 80) {  // Structural steps (80+) always play
                float margin = (float)(w - threshold) / (float)(101 - threshold);
                if (margin > 1.0f) margin = 1.0f;
                probability = 100 - (int32_t)(p.probability * (1.0f - margin) * 0.85f);
            }

            // 5. RATCHET
            uint8_t ratchet = fillRatchet;
            if (ratchet == 0 && p.ratchet > 0) {
                float chance = p.ratchet * 0.01f * r.ratchetBias * 0.01f * (anchor ? 0.35f : 1.0f);
                if (rnd(barSeed, trackIndex, bar, s, RS_RATCHET) < chance) {
                    float n = rnd(barSeed, trackIndex, bar, s, RS_RATCHET_N);
                    ratchet = (n < 0.55f) ? 2 : (n < 0.85f) ? 3 : 4;
                }
            }

            // 6. TIMING: swing on every second 16th, humanize (scaled per role)
            int32_t tick = (int32_t)songStep * kGenTicksPerStep;
            if (s & 1) tick += ((int32_t)p.swing * 4) - 200;  // 50% straight .. 75% = +100 ticks
            float spread = (rnd(barSeed, trackIndex, bar, s, RS_HUMAN_A) + rnd(barSeed, trackIndex, bar, s, RS_HUMAN_B) - 1.0f);
            tick += (int32_t)(spread * p.humanize * 0.35f * r.humanizeScale * 0.01f);
            if (tick < 0) tick = 0;

            GenStep& step = candidates[candidateCount++];
            step.tick = (uint16_t)tick;
            step.velocity = (uint8_t)clampi(velocity, 1, 127);
            step.probability = (uint8_t)clampi(probability, 10, 100);
            step.ratchet = ratchet;
            step.weight = (uint8_t)clampi(anchor ? 100 : w, 0, 99);
        }
    }

    // 7. BUDGET: at most kGenMaxSteps beats, keeping the most important ones
    while (candidateCount > kGenMaxSteps) {
        uint16_t weakest = 0;
        for (uint16_t i = 1; i < candidateCount; i++) {
            if (candidates[i].weight < candidates[weakest].weight) weakest = i;
        }
        memmove(&candidates[weakest], &candidates[weakest + 1], (candidateCount - weakest - 1) * sizeof(GenStep));
        candidateCount--;
    }

    // 8. BUDGET: at most kGenMaxRatchets fills (the stock engine's fill slots)
    uint16_t ratchets = 0;
    for (uint16_t i = 0; i < candidateCount; i++) ratchets += (candidates[i].ratchet != 0);
    while (ratchets > kGenMaxRatchets) {
        int32_t weakest = -1;
        for (uint16_t i = 0; i < candidateCount; i++) {
            if (candidates[i].ratchet && ((weakest < 0) || (candidates[i].weight < candidates[weakest].weight))) weakest = i;
        }
        candidates[weakest].ratchet = 0;
        ratchets--;
    }

    for (uint16_t i = 0; i < candidateCount; i++) out[count++] = candidates[i];
    return count;
}
