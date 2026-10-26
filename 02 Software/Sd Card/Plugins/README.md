# Drumboy Pro plugins

Copy this folder to the SD card as `Plugins/`. Full documentation: `docs/README.md` in the [GitHub repository](https://github.com/cyberyak/Drumboy-Pro/tree/dev/docs). Enable plugins in `plugins.txt` (`name = on`) and power-cycle the Drumboy.

## Plugin & Firmware Manager (on the device)

Press **FILE** to open the File menu, then press **FILE** again. The manager appears in the song lanes:

| Control | Does |
|---|---|
| **UP / DOWN** or knob 1 | Move the cursor |
| **GLOBAL** (between LEFT and RIGHT) | Plugins page: switch the selected plugin on/off. Firmware page: press twice to install (see `Firmware/README.md`) |
| **LEFT / RIGHT** | Switch between the PLUGINS and FIRMWARE pages |
| **SELECT** (left of UP) | Plugins page: start the selected plugin's **show mode** (only plugins marked `SELECT = SHOW`) |
| **FILE** | Close; plugin changes are saved to `plugins.txt` and apply after a restart. In show mode: return to the normal screen |

While the manager is open the sequencer keeps playing, but the other keys and knobs are paused. It does not open while recording.

## Pattern generator

Hold **FILL** and press **STYLE** (from any menu) to open the generator. It writes new drum patterns into the **current song bank**; nothing is stored on the SD card until you use **File → SAVE** (reload the file, or press CLEAR here, to get the old pattern back). The song lanes stay visible and update as you generate; PLAY/STOP and RESET keep working.

Each track gets a role from its instrument folder (Kick, Snare, HHClosed, Cowbell, …), or keywords in user sample names (`kick`, `ohh`, `808`, `conga`, …), or the usual role of its track number.

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
| **GLOBAL** | Generate all unlocked tracks with the current seed (turn knobs, press again: same groove, new settings) |
| **PASTE** | New random seed, then generate — press repeatedly for new patterns |
| **MUTE** | Re-roll only the selected track |
| **LEFT / RIGHT** | History: step back / forward through the last 8 generated patterns |
| **CLEAR** | Undo the last change |
| **UP / DOWN** or a layer key | Select a track |
| **SELECT** | Lock / unlock the selected track (locked tracks are kept) |
| **COPY** | Change the selected track's role (AUTO → KICK → SNARE → …) |
| **SONG** | Phrase fill: off / every 2 / 4 / 8 bars |
| **REC** | EVOLVE: off / every 4 / 8 bars — while playing, a couple of unlocked tracks re-roll their details (ghosts, ratchets, extra hits) at that interval, keeping the groove; works with the generator closed too |
| **FILE** (or FILL + STYLE) | Close |

Beat ticks are coloured by probability: track colour = always, **green** ≥ 70%, **yellow** ≥ 45%, **orange** ≥ 20%, **red** below. Ratchets use the stock fills, so the generator switches a track's **F** on when it gets ratchets.

**Saved with the File:** File → SAVE also stores each generated step's probability and velocity and the generator settings (style, knobs, seed, locks, roles, fill, evolve) in an extension block after the stock data. Stock firmware still loads these files (it ignores the block); saving with stock firmware drops the block but keeps the beats.

## Built-in plugins

| Name | What it does |
|---|---|
| `cpu_meter` | Audio and main-loop CPU load (green line, top-left) |
| `hook_monitor` | Developer view of the latest step/note/key/encoder/MIDI events (cyan line) |
| `psychedelic` | Full-screen, endlessly evolving psychedelic visuals reacting to the music (show mode) |

### Psychedelic (show mode)

Open the manager, select `psychedelic` and press **SELECT**; **FILE** returns. It fills the whole track area and never repeats: every motion is driven by several slow oscillators at unrelated speeds. Kick and bass pulse the zoom and brightness, snare hits jump the colours, hats and treble shimmer the shape.

| Knob | Controls |
|---|---|
| 1 MODE | AUTO (new visual every 20 s), JULIA (morphing fractal), PLASMA (flowing colour fields), WARP (fractal bent by waves), KALEIDO (kaleidoscope) |
| 2 SPEED | How fast it evolves |
| 3 ZOOM | Zoom level |
| 4 COLOUR | Colour cycling speed |
| 5 DETAIL | AUTO (adapts block size to keep it smooth) or fixed 2/3/4/6-pixel blocks |
| 6 REACTION | How strongly it reacts to the music |
| 7 SYMMETRY | Number of kaleidoscope folds (KALEIDO mode) |
| 8 PALETTE | RAINBOW, CANDY, ACID, FIRE, NEON, DUSK |
