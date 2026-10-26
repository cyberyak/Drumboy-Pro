# Changelog

Custom Drumboy Pro firmware by cyberyak (fork of [Randomwaves/Drumboy-Pro](https://github.com/Randomwaves/Drumboy-Pro)).

**Versioning: `<base>-cy.<build>`**, e.g. `1.01-cy.1` = stock Randomwaves 1.01 + cyberyak build 1.
- **base** comes from Randomwaves' own `kVersionMajor`/`kVersionMinor` in `Global.h`, so it follows upstream automatically. It is what the bootloader shows during an SD-card update (`V1.01`), so it never collides with an official version.
- **build** is the only content of `02 Software/Drumboy-Pro-App-H723/VERSION` (`cy.1`). Every merged PR that changes the firmware bumps it and adds an entry below. Merging a new upstream base keeps counting (e.g. `1.02-cy.7`).
- Update file: `Drumboy_Pro_Fw_01_01_cy01.bin`. On the device, the label `CYBERYAK 1.01-CY.1` appears in the top-right corner while the File menu is open.
- Releasing: merge to `dev`, then `git tag v1.01-cy.1 && git push origin v1.01-cy.1`. CI checks the tag matches the firmware version and publishes a GitHub Release with the SD-card package and this version's notes.

## [Unreleased]

## [1.01-cy.4]

Show mode and an audio-reactive fractal visualizer.

### Added
- **Show mode**: a plugin can take over the whole layer area (920×310 px, the 10 track rows). Start it from the Plugin & Firmware Manager with **SELECT** on a plugin marked `SELECT = SHOW`; **FILE** returns and the rows are redrawn with the stock row-drawing code. The sequencer keeps playing; keys and knobs go to the show plugin.
- **`fractal.lua`**: audio-reactive fractal tree. Each branch level belongs to a track (level 1 = track 1, …) and lights up in that track's colour on hits; the kick and the mix's bass make it grow, treble twists it, tracks 9–10 make it sway. All 8 knobs control it while showing: lean, size, spread, shape, detail (4–9 levels), reaction, glow time, colour; the turned knob's name and value appear briefly. It shows its frame time and lowers the detail automatically if frames get slower than 30 ms. Enable with `fractal = on`.
- **`psychedelic`** (built-in, native C++): full-screen, endlessly evolving psychedelic visuals in show mode — morphing Julia fractal with orbit-trap colouring and mirror tiling, flowing plasma, wave-warped fractal and a kaleidoscope, plus AUTO cycling; cosine colour palettes with continuous colour cycling. Kick/bass pulse zoom and brightness, snare jumps colours, treble shimmers. 8 knobs: mode, speed, zoom, colour speed, detail, reaction, symmetry, palette. Computed in square cells and streamed to the LCD a band at a time; block size adapts to keep it smooth. Enable with `psychedelic = on`.
- LCD driver: public `beginStream` / `streamPixel` / `endStream` (same bus sequence as the stock fill routine).
- **Audio tap**: output level, bass (<150 Hz) and treble (>2 kHz) peaks measured after every audio block (~200 float operations per 0.73 ms block), available to Lua as `audio.level()`, `audio.bass()`, `audio.treble()`.
- Lua: `on_show_frame()` / `on_show_start()` / `on_show_end()` callbacks, `screen.width()` / `screen.height()`, `screen.clear()` without arguments clears the drawing area; 50 ms budget per show frame (LCD lines are drawn pixel by pixel over the parallel bus); errors stay on screen for 15 s.

### Changed
- Lua `screen.*` coordinates and colours accept fractional numbers (rounded) and are clipped to the current drawing area (whole screen in `on_frame`, show canvas in `on_show_frame`).

## [1.01-cy.3]

Plugin & Firmware Manager.

### Added
- **Plugin & Firmware Manager**: in the File menu, press FILE again. It draws in the song lanes (restored on close) and owns keys and knobs while open; the sequencer keeps playing. Not available while recording.
- **Plugins page**: every built-in and Lua plugin with ON/OFF and RUNNING state; CENTER toggles; changes are written to `Plugins/plugins.txt` on close and apply after a restart.
- **Firmware page**: lists valid update files in a `Firmware/` folder on the SD card (e.g. stock 1.01 and cyberyak builds). CENTER twice copies the selected file to the SD root with a CRC check and restarts, so the bootloader installs it; the library copy is kept.
- Releases also attach the official stock package, so the `Firmware/` library can be filled from one release.

### Changed
- Plugin host takes the Controller (instead of its FIL) and separates "configured" from "running" plugins.
- Controller gains two read-only getters (`getActiveSongBank`, `isRecording`); stock keyboard/encoder dispatch and `lcd_update()` pause while the manager is open.

## [1.01-cy.2]

Lua script plugins.

### Added
- **Lua 5.4 plugins**: every `Plugins/*.lua` file on the SD card is a plugin, enabled in `plugins.txt` like the built-ins. Callbacks `on_load`, `on_step`, `on_note`, `on_key`, `on_encoder`, `on_midi_in`, `on_midi_out`, `on_frame`; API `screen.*`, `sys.millis/memory`, `print`. Reference: `Plugins/README.md`.
- Sandbox: no `io`/`os`/`require`/`load`/bytecode; per-script limits of 48 KB memory and 2 ms per event / 10 ms per frame / 300 ms to load. Errors stop only that script and are shown in red in the top strip; `print()` output is shown there too.
- Lua heap: 96 KB in DTCM (memory the stock firmware never used); about 20 KB per typical script.
- Example plugins `hello.lua` (template with every callback) and `step_dots.lua` (16 step dots in the top-right corner).
- `DRUMBOY_LUA` build option (on by default).

Cost versus 1.01-cy.1: +136 KB flash (Lua engine and float formatting), +2 KB main RAM.

## [1.01-cy.1]

First custom release, based on Randomwaves 1.01: a plugin-capable firmware with two built-in plugins. With no plugins enabled it behaves exactly like stock, apart from the build label.

### Added
- **Plugin host**: hooks (step, note, key, encoder, MIDI in/out), interrupt-safe event queue, plugins run in the main loop at ~30 fps with the stock UI's drawing state protected.
- **Plugins are switched on in `Plugins/plugins.txt`** on the SD card (`id = on|off`); a missing file means all plugins off.
- Built-in plugin **`cpu_meter`**: audio load (average/peak per block) and main-loop timing in the top-left strip. Measured 29.9% average / 32.2% peak with 10 tracks and effects.
- Built-in plugin **`hook_monitor`**: developer view of the latest events from every hook. Verified on device (MIDI not yet tested).
- Build label `CYBERYAK 1.01-CY.1` in the File menu (top-right corner).
- CMake/Ninja build and VS Code setup (no STM32CubeIDE needed). Presets: `Debug` (release build), `Stock` (identical to official firmware), `Release` (-Os).
- SD-card package script (byte-identical to the official package format).
- GitHub Actions build: artifacts on every push/PR, GitHub Release on version tags.

Cost versus stock: +3.0 KB flash, +1.7 KB RAM.
