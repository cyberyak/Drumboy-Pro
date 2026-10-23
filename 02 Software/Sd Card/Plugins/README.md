# Drumboy Pro plugins

Copy this folder to the SD card as `Plugins/`. Enable plugins in `plugins.txt` (`name = on`) and power-cycle the Drumboy.

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
| `on_frame()` | About 30 times per second; the only place `screen.*` works |

### API

| Call | Does |
|---|---|
| `screen.color(r, g, b)` | Drawing colour, 0–255 each (default white) |
| `screen.font(n)` | Font 1–4 (5×7, 7×9, 10×14, 14×18; default 1) |
| `screen.text(x, y, text [, "left" \| "center" \| "right"])` | Text on a black background |
| `screen.rect(x, y, w, h [, fill])` | Rectangle outline or filled |
| `screen.clear(x, y, w, h)` | Fill with black |
| `screen.line(x0, y0, x1, y1)` / `screen.pixel(x, y)` | Line / pixel |
| `screen.circle(x, y, r [, fill])` | Circle (drawn only if fully on screen) |
| `sys.millis()` | Milliseconds since boot |
| `sys.memory()` | Bytes this script uses |
| `print(...)` | Shows a line in the top strip for 6 s |

The screen is 960×540. The stock UI leaves a strip at the top (y 0–25) free; drawing elsewhere is allowed but the stock UI may draw over it.

### Limits and sandbox

- Libraries: `string`, `table`, `math`, `coroutine`, `utf8` and the basics. No `io`, `os`, `require`, `load`, `dofile` or bytecode.
- Numbers are 32-bit (integers up to ±2,147,483,647; floats have ~7 digits).
- Memory: 48 KB per script, 96 KB shared by all scripts.
- Time: `on_load` 300 ms, each event 2 ms, each `on_frame` 10 ms.
- Any error, or going over a limit, stops that script and shows `LUA ERROR <name>: ...` in red. The drums keep playing; fix the file and power-cycle.
