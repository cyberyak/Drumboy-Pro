# Drumboy Pro plugins

Copy this folder to the SD card as `Plugins/`. Enable plugins in `plugins.txt` (`name = on`) and power-cycle the Drumboy.

## Plugin & Firmware Manager (on the device)

Press **FILE** to open the File menu, then press **FILE** again. The manager appears in the song lanes:

| Control | Does |
|---|---|
| **UP / DOWN** or knob 1 | Move the cursor |
| **CENTER** | Plugins page: switch the selected plugin on/off. Firmware page: press twice to install (see `Firmware/README.md`) |
| **LEFT / RIGHT** | Switch between the PLUGINS and FIRMWARE pages |
| **ADD** | Plugins page: start the selected plugin's **show mode** (only plugins marked `ADD = SHOW`) |
| **FILE** | Close; plugin changes are saved to `plugins.txt` and apply after a restart. In show mode: return to the normal screen |

While the manager is open the sequencer keeps playing, but the other keys and knobs are paused. It does not open while recording.

## Lua plugins

Each `.lua` file in this folder is one plugin; its name is the file name without `.lua` (max 23 characters). Up to 8 Lua plugins are listed; enabled ones are loaded at boot.

### Callbacks (define the ones you need as global functions)

| Function | Called |
|---|---|
| `on_load()` | Once, after loading |
| `on_step(step)` | Every 16th note while playing (`step` counts from 0 through the loop) |
| `on_note(layer, velocity)` | A sound played: layer 1–10, velocity 0–127 (sequencer, keypad or MIDI) |
| `on_key(keypad, key, pressed)` | Key press/release: keypad 0 = layer keys, 1 = left, 2 = right; key 0–15 |
| `on_encoder(knob, delta)` | Knob 1–8 turned, delta ±1 |
| `on_midi_in(byte)` | Each received MIDI byte (clock `0xF8` and active sensing `0xFE` filtered) |
| `on_midi_out(status, d1, d2)` | Each sent MIDI message (clock filtered) |
| `on_frame()` | About 30 times per second; `screen.*` works here (whole screen) |
| `on_show_frame()` | About 30 times per second while the plugin is in show mode; `screen.*` works here, relative to the show canvas. Defining it makes the plugin show-capable |
| `on_show_start()` / `on_show_end()` | When show mode starts / ends |

### API

| Call | Does |
|---|---|
| `screen.color(r, g, b)` | Drawing colour, 0–255 each (default white) |
| `screen.font(n)` | Font 1–4 (5×7, 7×9, 10×14, 14×18; default 1) |
| `screen.text(x, y, text [, "left" \| "center" \| "right"])` | Text on a black background |
| `screen.rect(x, y, w, h [, fill])` | Rectangle outline or filled |
| `screen.clear([x, y, w, h])` | Fill with black (no arguments: the whole drawing area) |
| `screen.line(x0, y0, x1, y1)` / `screen.pixel(x, y)` | Line / pixel |
| `screen.circle(x, y, r [, fill])` | Circle (drawn only if fully inside the drawing area) |
| `screen.width()` / `screen.height()` | Size of the drawing area (960×540, or 920×310 in show mode) |
| `sys.millis()` | Milliseconds since boot |
| `sys.memory()` | Bytes this script uses |
| `audio.level()` / `audio.bass()` / `audio.treble()` | Output peak of the mix since the previous frame, 0–1 (full band / below ~150 Hz / above ~2 kHz) |
| `print(...)` | Shows a line in the top strip for 6 s |

Coordinates may be fractional (they are rounded) and are clipped to the drawing area. The screen is 960×540; the stock UI leaves a strip at the top (y 0–25) free, and draws over anything else in normal mode.

### Show mode

A plugin that defines `on_show_frame()` can take over the whole layer area (920×310 px, the 10 track rows) as its canvas: open the manager, select the plugin and press **ADD**. The canvas is cleared to black, and (0, 0) is its top-left corner. While showing, the sequencer keeps playing; keys and knobs go to the plugin's `on_key` / `on_encoder` instead of the stock UI; **FILE** ends the show and the track rows are redrawn. Example: `fractal.lua`.

### Limits and sandbox

- Libraries: `string`, `table`, `math`, `coroutine`, `utf8` and the basics. No `io`, `os`, `require`, `load`, `dofile` or bytecode.
- Numbers are 32-bit (integers up to ±2,147,483,647; floats have ~7 digits).
- Memory: 48 KB per script, 96 KB shared by all scripts.
- Time: `on_load` 300 ms, each event 2 ms, each `on_frame` 10 ms, each `on_show_frame` 25 ms.
- Any error, or going over a limit, stops that script and shows `LUA ERROR <name>: ...` in red. The drums keep playing; fix the file and power-cycle.
