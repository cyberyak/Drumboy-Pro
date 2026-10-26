#ifndef __PATTERN_GEN_H
#define __PATTERN_GEN_H

#include <stdint.h>

/* Pattern Generator Engine -----------------------------------------*/
// Pure pattern logic (no hardware access): turns a style, a track role and
// the generator parameters into a list of steps with timing, velocity,
// probability and ratchet. Device integration lives in Generator.cpp.

enum GenRole : uint8_t {
    ROLE_KICK,
    ROLE_SNARE,
    ROLE_CLAP,
    ROLE_HAT_CLOSED,
    ROLE_HAT_OPEN,
    ROLE_RIDE,
    ROLE_CRASH,
    ROLE_TOM,
    ROLE_RIM,
    ROLE_SHAKER,
    ROLE_HAND_PERC,
    ROLE_BELL,
    ROLE_WOOD,
    ROLE_PERC,
    ROLE_BASS,
    ROLE_FX,
    ROLE_COUNT
};

enum GenStyle : uint8_t { STYLE_TECHNO, STYLE_ELECTRO, STYLE_BREAKBEAT, STYLE_COUNT };

struct GenParams {
    uint8_t style;                                                      // GenStyle
    uint8_t density;                                                    // 0-100: how many steps play
    uint8_t variation;                                                  // 0-100: how much each bar differs
    uint8_t swing;                                                      // 50-75: % delay of every second 16th
    uint8_t humanize;                                                   // 0-100: timing spread off the grid
    uint8_t dynamics;                                                   // 0-100: velocity spread (accents vs ghosts)
    uint8_t probability;                                                // 0-100: how many steps get < 100% chance
    uint8_t ratchet;                                                    // 0-100: chance of ratchets / rolls
    uint8_t fillEvery;                                                  // 0 = off, else phrase fill every N bars
    uint32_t seed;                                                      // Base groove
    uint32_t mutation;                                                  // Per-bar details only (EVOLVE); 0 = none
};

struct GenStep {
    uint16_t tick;                                                      // Position in the song (200 ticks = one 16th)
    uint8_t velocity;                                                   // 1-127
    uint8_t probability;                                                // 1-100
    uint8_t ratchet;                                                    // 0 = single hit, else 2/3/4 hits
    uint8_t weight;                                                     // Importance 0-100 (for budget trimming)
};

const uint16_t kGenTicksPerStep = 200;
const uint8_t kGenMaxSteps = 64;                                        // Stock limit: beats per track per bank
const uint8_t kGenMaxRatchets = 32;                                     // Stock limit: fills per track per bank

extern const char* const kGenStyleNames[STYLE_COUNT];
extern const char* const kGenRoleNames[ROLE_COUNT];

// Track role from the instrument folder / instrument name / sample name (any may be empty);
// falls back to the usual role of the track number (factory kits: kick, snare, closed hat, ...).
GenRole gen_detectRole(const char* folder, const char* instrument, const char* sample, uint8_t trackIndex);

// Generates one track. 'occupied' marks steps used by kick and snare (one bit per 16th step of the
// song, may be null) so percussion can fill the gaps. Returns the number of steps written to 'out'
// (at most kGenMaxSteps, at most kGenMaxRatchets ratchets), sorted by tick.
uint8_t gen_generate(GenRole role, const GenParams& params, uint8_t stepsPerBar, uint8_t bars, uint8_t trackIndex,
                     const uint32_t* occupied, GenStep* out);

/* Grids-style Map --------------------------------------------------*/
// A 3x3 map of kick / snare / hats weight patterns (own data, not Mutable Instruments'),
// morphed by X (complexity: minimal -> busy) and Y (feel: straight -> electro -> broken).
// CHAOS jitters the weights per bar and gives borderline steps a probability, so the
// pattern keeps changing from loop to loop.

enum GridsPart : int8_t { GRIDS_NONE = -1, GRIDS_KICK, GRIDS_SNARE, GRIDS_HATS, GRIDS_PARTS };

struct GridsParams {
    uint8_t x;                                                          // 0-100
    uint8_t y;                                                          // 0-100
    uint8_t density[GRIDS_PARTS];                                       // 0-100 per part
    uint8_t chaos;                                                      // 0-100
};

// Which part a role follows (GRIDS_NONE: the role keeps the recipe engine).
GridsPart gen_gridsPart(GenRole role);

// Generates one track from the map. Uses swing, humanize, dynamics, seed and mutation from 'shared'.
uint8_t gen_grids(GenRole role, const GridsParams& grids, const GenParams& shared, uint8_t stepsPerBar, uint8_t bars,
                  uint8_t trackIndex, GenStep* out);

/* Euclidean Track --------------------------------------------------*/

enum EuclidRate : uint8_t { RATE_32, RATE_16T, RATE_16, RATE_8T, RATE_8, RATE_4, RATE_COUNT };

struct EuclidParams {
    uint8_t steps;                                                      // 1-32
    uint8_t pulses;                                                     // 0-steps
    uint8_t rotate;                                                     // 0-(steps-1)
    uint8_t rate;                                                       // EuclidRate
    uint8_t velocity;                                                   // 1-127 (accented hits)
    uint8_t accents;                                                    // 0-pulses: accented hits, spread evenly
    uint8_t probability;                                                // 10-100 for unaccented hits
};

extern const char* const kGenRateNames[RATE_COUNT];

// E(pulses, steps) repeated from the song start and cut at 'songTicks'. Returns the number of
// steps written (at most kGenMaxSteps); 'wanted' receives how many hits the song would need.
uint8_t gen_euclid(const EuclidParams& e, uint16_t songTicks, GenStep* out, uint16_t* wanted);

#endif
