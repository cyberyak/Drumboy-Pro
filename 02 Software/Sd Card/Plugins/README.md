# Drumboy Pro plugins

Copy this folder to the SD card as `Plugins/`. Enable plugins in `plugins.txt` (`name = on`) and power-cycle the Drumboy.

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
