# Pattern generator

Source: `Generator.cpp/.h` (device page, writing beats, file block) and `PatternGen.cpp/.h` (engine). Part of the firmware — always available, not listed in `plugins.txt`. Since 1.01-cy.6 (saving, history, EVOLVE since cy.7; GRIDS, EUCLID, STEP pages and trig conditions since cy.8).

## Using it

Hold **FILL** and press **STYLE** (from any menu). The parameters appear in the top strip; the song lanes stay visible and update as you generate. PLAY/STOP and RESET keep working.

The generator has four pages; **STYLE** (alone) goes to the next one, **FILL + STYLE** or **FILE** closes:

| Page | What it does |
|---|---|
| **GEN** | Style-based generator for all tracks (below) |
| **GRIDS** | Kick / snare / hats from an X/Y map, live ([GRIDS page](#grids-page)) |
| **EUCLID** | Euclidean rhythm for the selected track, live ([EUCLID page](#euclid-page)) |
| **STEP** | Trig condition, probability and level per beat ([STEP page](#step-page-and-trig-conditions)) |

On every page: **UP/DOWN** or a layer key selects a track, **SELECT** locks it, **COPY** changes its role, **CLEAR** undoes, **SONG** sets the phrase fill, **REC** sets EVOLVE. The bottom line shows the selected track (number, role, AUTO/SET, which engine made it, LOCK) and the page's keys.

### GEN page

Generating overwrites the **current song bank in memory**. Nothing touches the SD card until **File → SAVE**; to get the old pattern back, press **CLEAR** (undo) or reload the File.

| Control | Does |
|---|---|
| Knob 1 STYLE | TECHNO, ELECTRO, BREAKBEAT |
| Knob 2 DENSITY | How many steps play |
| Knob 3 VARIATION | How much each bar differs |
| Knob 4 SWING | 50 (straight) – 75 |
| Knob 5 HUMANIZE | Timing spread off the grid |
| Knob 6 DYNAMICS | Accents vs ghost notes |
| Knob 7 PROBABILITY | How many steps play less than 100% of the time |
| Knob 8 RATCHET | Chance of ratchets / rolls |
| **GLOBAL** | Generate all unlocked tracks with the current seed (same groove, new settings) |
| **PASTE** | New random seed, then generate |
| **MUTE** | Re-roll only the selected track |
| **LEFT / RIGHT** | History: back / forward through the last 8 generated patterns |
| **CLEAR** | Undo the last change (one level) |
| **UP / DOWN** or a layer key | Select a track (marker left of the row) |
| **SELECT** | Lock / unlock the selected track — locked tracks are never changed |
| **COPY** | Selected track's role: AUTO → KICK → SNARE → … |
| **SONG** | Phrase fill: off / every 2 / 4 / 8 bars |
| **REC** | EVOLVE: off / every 4 / 8 bars |
| **FILE** (or FILL + STYLE) | Close |

**Tick colours** show probability: track colour = always plays, green ≥ 70%, yellow ≥ 45%, orange ≥ 20%, red below.

**EVOLVE**: while playing, every 4 or 8 bars two unlocked GEN or GRIDS tracks re-roll their per-bar details (ghosts, ratchets, extra hits) — the base groove stays. It keeps running with the page closed.

### GRIDS page

Kick, snare and hat tracks from a map of drum patterns — the idea of Mutable Instruments' *Grids* and the OXI One drum generator, with our own pattern data (Grids' data is GPL and isn't used).

| Knob | Controls |
|---|---|
| 1 X | Complexity: minimal → standard → busy |
| 2 Y | Feel: straight (four-on-the-floor) → electro / funk → broken (half-time, breaks) |
| 3 KICK, 4 SNARE, 5 HATS | Density of each part |
| 6 CHAOS | Weaker hits vary per bar and become "maybe" hits, so every loop differs; strong hits (the backbone) stay |
| 7 SWING, 8 DYN | Shared with the GEN page |

Keys: **GLOBAL** generates the unlocked kick / snare / hat tracks from the map (they become GRIDS tracks), **PASTE** new seed, **MUTE** re-rolls the selected track, **LEFT/RIGHT** history. Once tracks are GRIDS tracks, turning a knob updates them **live**.

Roles that follow the map: KICK → kick line; SNARE, CLAP, RIM → snare line (rim sparser); HAT-C, RIDE, SHAKER → hat line (ride and shaker sparser); HAT-O → hat line on off-beats only. Other roles keep their GEN pattern.

### EUCLID page

The selected track plays E(pulses, steps): the pulses spread as evenly as possible over the steps, repeated from the song start and cut at the song end.

| Knob | Controls |
|---|---|
| 1 STEPS | 1–32 |
| 2 PULSES | 0–steps |
| 3 ROTATE | Shift the pattern |
| 4 RATE | 1/32, 1/16T, 1/16, 1/8T, 1/8, 1/4 |
| 5 VELO | Velocity of (accented) hits |
| 6 ACCENTS | How many hits are accented (spread evenly); the others play at 60% |
| 7 PROB | Probability of the unaccented hits |

Turning a knob writes the track **live** and makes it a EUCLID track, **locked** so GEN and GRIDS leave it alone. **LEFT/RIGHT** rotate, **PASTE** random pulses / rotation / accents, **GLOBAL** writes it again, **MUTE** releases the track (beats kept, unlocked, back to GEN). A pattern needing more than 64 hits (e.g. 1/32 over 4 bars) is cut at the stock limit and the page says so.

### STEP page and trig conditions

**LEFT/RIGHT** select a beat of the selected track; it is drawn **white** in its lane and the top line shows its position. Works on generated and hand-made beats.

| Knob | Controls |
|---|---|
| 1 COND | Trig condition (below) |
| 2 PROB | Probability 5–100% |
| 3 LEVEL | Volume 10–250% |

**GLOBAL** resets the step (no condition, 100%, recorded level).

| Condition | Plays when |
|---|---|
| `A:B` (1:2 … 8:8) | On loop A of every B loops — e.g. 1:2 every other loop, 4:4 every fourth loop |
| `FILL` / `NOT FILL` | While the **FILL** key is held / not held |
| `FIRST` / `NOT FIRST` | Only on / never on the first loop after PLAY or RESET |
| `PRE` / `NOT PRE` | The previous conditional beat on this track played / didn't |

Loops are counted per track from PLAY or RESET. A conditional beat is drawn **magenta**. The condition is checked first, then the probability.

## Track roles

Each track gets one of 16 roles: KICK, SNARE, CLAP, HAT-C, HAT-O, RIDE, CRASH, TOM, RIM, SHAKER, HAND-PERC, BELL, WOOD, PERC, BASS, FX. Detection (`gen_detectRole`), first match wins:

1. **Factory folder / instrument name**: Kick, Snare, Clap, Snap, Rim, HHClosed, HHOpen, Ride, Crash, Tom, Perc, Shaker, Tambourine, Conga, Bongo, Cowbell, Clave, Bass, Fx.
2. **Keywords** in folder, instrument or sample name (lower-cased): e.g. `ohh`, `openhat`, `kick`, `kik`, `bd_`, `snare`, `snr`, `clap`, `rim`, `hihat`, `chh`, `ride`, `crash`, `tom`, `shaker`, `conga`, `cowbell`, `clave`, `808`, `sub`, `perc`, `fx`, `vox` … (open hat is checked before closed hat, kick before bass)
3. **Track number**: the usual factory-kit layout (1 kick, 2 snare, 3 closed hat, 4 open hat, 5 perc, 6 clap, 7 fx, 8 wood, 9 bell, 10 perc).

**COPY** overrides it per track (saved with the File).

## Engine (`PatternGen`)

Pure C++ with no hardware access, so it can be compiled and printed as grids on the Mac. One call generates one track:

```cpp
uint8_t gen_generate(GenRole role, const GenParams& params, uint8_t stepsPerBar, uint8_t bars,
                     uint8_t trackIndex, const uint32_t* occupied, GenStep* out);
```

For each style × role there is a **recipe**: a 16-step weight map (0–100), a Euclidean overlay (k, n, rotate), a density bias, ghost chance, ratchet bias, humanize scale, a "complement" flag and a fill kind. For every 16th step of the song:

1. **Weight** = recipe weight, raised to ≥72 on Euclidean hits. Steps ≥95 are **anchors** (e.g. four-on-the-floor kick, backbeat). Others get a fixed per-seed offset (the groove) and a per-bar offset scaled by VARIATION; VARIATION also adds rare surprise hits. **Complement** roles (percussion) are weakened where kick/snare already play (`occupied` bitmap), so they fill the gaps.
2. **Phrase fills** in the last beat of a fill bar: ROLL (snare crescendo, ratchets at the end), RUN (tom run down), DROP (kick drops out), OPEN (open hat on the last step), CRASH (crash on the downbeat after a fill).
3. The step plays if weight ≥ threshold, where **threshold = 100 − (DENSITY + role bias)**.
4. **Velocity** from weight (accents on strong steps), spread by DYNAMICS; weak steps may become ghost notes.
5. **Probability**: anchors, fills and steps with weight ≥80 always play; the closer a step is to the threshold, the lower its chance (scaled by PROBABILITY, minimum 10%).
6. **Ratchet** (2/3/4 hits) by RATCHET × role bias; anchors rarely.
7. **Timing**: SWING delays every second 16th by up to +100 ticks; HUMANIZE adds a triangular random offset scaled per role.
8. **Budget**: trimmed to the stock limits — 64 beats and 32 fills per track per bank — dropping the lowest-weight steps first.

Randomness is a stateless hash of (seed, track, bar, step, stream), so the same seed always gives the same pattern. `barSeed = seed ^ mutation·0x9E3779B9` drives the per-bar streams: EVOLVE changes `mutation`, which changes details but not the per-seed groove. MUTE mixes a new per-track `salt` into that track's seed.

Kick, snare and clap tracks are generated first; their steps are passed as `occupied` to the other tracks.

## Writing into the stock sequencer

`writeTrack()` replaces the track's beats in the active bank directly (`layerSong_setBeat` would quantize away swing and humanize), with interrupts masked so the play timer never sees half a track:

| Generated | Stock beat |
|---|---|
| Normal step | FLAT beat (pattern 0) |
| Ratchet 2 / 3 / 4 | BASIC_2 / BASIC_3 / BASIC_4 fill (patterns 3 / 4 / 5) within one 16th; the track's **F** is switched on |
| Velocity < 60 | GHOST beat (pattern 1, stock plays it at 50%) |

Ticks: 800 per quarter note, 200 per 16th.

Probability and exact velocity don't fit the stock beat format, so they are kept in a side table `StepMeta { tick, probability, gain }` per track × bank × beat (in DTCM). `PLUGIN_BEAT_GATE` looks it up just before a beat plays: it rolls the probability and multiplies the volume by `gain`. An entry only applies while the beat still starts at the stored tick, so beats edited by hand afterwards play normally.

Undo keeps one copy of every track's bank and its meta (`undoBanks`, in DTCM), plus track sources, locks and Euclid settings. History keeps the last 8 settings + seeds + salts + track sources (not the beats) and regenerates; Euclid tracks are left out.

**Track source**: each track remembers which engine made it (`SOURCE_GEN`, `SOURCE_GRIDS`, `SOURCE_EUCLID`); re-roll, history, EVOLVE and live edits regenerate it with that engine (`regenerate()`).

**Gate decisions**: `PLUGIN_BEAT_GATE` passes the fill/ratchet repeat index. On the first hit (`micro` 0) the gate counts loops (a beat starting no later than the previous one means the track wrapped), checks the condition, rolls the probability and stores the decision; repeats of the same beat follow it.

## File format (CYBX block)

File → SAVE appends this after the stock `EOF` marker (`PLUGIN_FILE_SAVE`):

```
[payload][crc32 : u32][payload length : u32]["CYBX"]
                         \______ 12-byte trailer at the very end of the file ______/
```

Payload (version 2, little-endian; version 1 lacks the marked fields and has 7-byte records):

| Bytes | Field |
|---|---|
| 12 | header: version (2), style, density, variation, swing, humanize, dynamics, probability, ratchet, fill choice, evolve choice, reserved |
| 4 | seed |
| 4 × 10 | per-track re-roll salts |
| 2 | lock mask (bit per track) |
| 1 × 10 | role overrides (`int8`, −1 = AUTO) |
| 6 | **v2**: GRIDS x, y, kick / snare / hats density, chaos |
| 1 × 10 | **v2**: track sources (0 GEN, 1 GRIDS, 2 EUCLID) |
| 7 × 10 | **v2**: Euclid per track: steps, pulses, rotate, rate, velocity, accents, probability |
| 2 | record count |
| 8 × count | records: track, bank, beat, probability, gain ×100, tick (u16), condition (**v2**) |

Only beats with valid meta are written (generated beats and beats edited on the STEP page). A block from a newer firmware version is skipped with a message.

Loading (`PLUGIN_FILE_LOADED`): the meta table is cleared, then the trailer is looked for in the last 12 bytes. If the magic matches and the length is sane (≤ 32 KB), the CRC is checked over the payload first and only then parsed. A CRC mismatch shows `FILE: GENERATOR DATA DAMAGED - BEATS LOADED WITHOUT PROBABILITY` and the beats load as plain beats. An unknown version is ignored.

Compatibility: stock firmware reads its fixed sections up to `EOF` and never looks further, so it loads these files normally. Saving with stock firmware rewrites the file without the block (beats kept, probability lost).

## Status

Built and host-tested; device testing pending (Jira DRUMB-18, 19, 20; cy.8: DRUMB-40, 41, 42). Ideas: more styles (DRUMB-22), per-track density (DRUMB-23), more OXI-style features (epic DRUMB-39).
