# cyberyak firmware for Drumboy Pro

Custom firmware for the Randomwaves Drumboy Pro, built on the official open-source firmware. With every plugin switched off it behaves exactly like stock (apart from a version label in the File menu).

What it adds:

| Feature | How to open | Docs |
|---|---|---|
| Plugin host (hooks, event queue, frames) | always on; plugins switched on in `Plugins/plugins.txt` | [Plugin development guide](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/README.md) |
| Plugin & Firmware Manager | File menu → press **FILE** again | [Manager.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Manager.md) |
| Show mode (a plugin owns the track area) | Manager → select plugin → **SELECT** | [Manager.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Manager.md#show-mode) |
| Pattern generator | hold **FILL** + press **STYLE** | [Generator.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Generator.md) |
| CPU Meter plugin | `cpu_meter = on` | [CpuMeter.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/CpuMeter.md) |
| Hook Monitor plugin | `hook_monitor = on` | [HookMonitor.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/HookMonitor.md) |
| Psychedelic visualizer | `psychedelic = on`, then show mode | [Psychedelic.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Psychedelic.md) |

Release history: [CHANGELOG.md](../CHANGELOG.md). Task board: Jira project DRUMB.

## Key names

The code uses internal key names; the device prints different ones on two keys. The docs use the **printed** names.

| Printed on the device | In the code (left keypad index) |
|---|---|
| SELECT (left of UP) | `ADD` (3) |
| GLOBAL (between LEFT and RIGHT) | `CENTER` (7) |

Other left-keypad indices: RESET 0, PLAY/STOP 1, REC 2, UP 4, CLEAR 5, LEFT 6, RIGHT 8, COPY 9, DOWN 10, PASTE 11, MUTE 12, FILL 13, STYLE 14, SONG 15. FILE is index 0 on the right keypad.

## Installing

1. Download `Drumboy_Pro_Fw_01_01_cyNN.bin` from a [GitHub release](https://github.com/cyberyak/Drumboy-Pro/releases) (or build it, see below).
2. Copy it to the **root** of the SD card. On a Mac use `cp -X` and delete any `._*` files.
3. Copy the release's `Plugins/` folder to the card as `Plugins/` (optional — without `plugins.txt` every plugin stays off).
4. Insert the card and power-cycle. The bootloader shows `UPDATING FIRMWARE`, checks the CRC, flashes and moves the file to `System/Firmware/`.

**Going back to stock:** copy the official `Drumboy_Pro_Fw_01_01.bin` (attached to every release, also in `02 Software/Firmware/`) to the SD root and power-cycle — or use the manager's Firmware page (see [Manager.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Manager.md#firmware-page)).

Notes:
- The bootloader only accepts file names up to 32 characters and empties `System/Firmware/` on every update — keep your firmware library in `Firmware/` instead.
- The bootloader is never modified; recovering a broken bootloader would need an ST-Link.
- Back up the SD card before trying a new build that writes to project files (1.01-cy.7 and later append data to Files on SAVE).

## SD card layout (additions)

```
Plugins/
  plugins.txt           which plugins run ("id = on|off"), rewritten by the manager
  README.md             on-device controls (copy of the user-facing docs)
Firmware/
  Drumboy_Pro_Fw_01_01.bin          stock, for the manager's Firmware page
  Drumboy_Pro_Fw_01_01_cy07.bin     cyberyak builds
  README.md
```

`plugins.txt`:

```
cpu_meter = off        # Audio and main-loop CPU load (green line, top-left)
hook_monitor = off     # Developer view of step/note/key/encoder/MIDI events (cyan line)
psychedelic = off      # Full-screen evolving psychedelic visuals (show mode: manager -> SELECT)
```

Changes apply after a restart. A missing file, or a plugin not listed, means off. The generator and the manager are part of the firmware and always available.

## Versioning

`<base>-cy.<build>`, e.g. `1.01-cy.7` = Randomwaves 1.01 + cyberyak build 7.

- **base** comes from `kVersionMajor` / `kVersionMinor` in `Core/Library/Global/Global.h` (Randomwaves' own version). The bootloader shows only this, so a cyberyak build never looks like a different official version.
- **build** is the only content of `02 Software/Drumboy-Pro-App-H723/VERSION` (`cy.7`). Every merged firmware change bumps it and adds a `CHANGELOG.md` entry.
- Package name: `Drumboy_Pro_Fw_01_01_cy07.bin`.
- On the device: `CYBERYAK 1.01-CY.7` in the top-right corner while the File menu is open.

## Building

Requirements: CMake and Ninja (the STM32 VS Code extension bundles them under `~/Library/Application Support/stm32cube/bundles/`) and the Arm GNU toolchain 14.2 (`~/Projects/DrumBoy/tools/arm-gnu-toolchain-14.2.rel1-darwin-arm64-arm-none-eabi`).

```sh
cd "02 Software/Drumboy-Pro-App-H723"
cmake --preset Debug          # add --fresh after changing option defaults
cmake --build --preset Debug
```

| Preset | What it builds |
|---|---|
| `Debug` | The release firmware (-Ofast, same flags as the official build) with plugins and the label |
| `Stock` | `DRUMBOY_PLUGINS=OFF`, `DRUMBOY_BRANDING=OFF` — byte-identical to the official firmware (used to check the hooks change nothing) |
| `Release` | -Os variant |

The post-build step (`cmake/make_firmware_package.py`) writes the SD-card package next to the `.elf`: header `RW_DRUMBOY_PRO_FIRMWARE ` + version + size + CRC32, then the image.

Build options:

| Option | Default | Effect |
|---|---|---|
| `DRUMBOY_PLUGINS` | ON | Plugin host, hooks, manager, generator, built-in plugins. Off: every hook macro compiles to nothing |
| `DRUMBOY_BRANDING` | ON | `CYBERYAK <version>` label in the File menu |

## Releasing

1. Merge the feature branch into `dev` (PR). `main` mirrors upstream Randomwaves untouched.
2. `git tag v1.01-cy.7 && git push origin v1.01-cy.7`.
3. CI (`.github/workflows/build.yml`) checks the tag matches the firmware version, builds Debug and Stock, and publishes a GitHub Release with the package, the stock package, the `Plugins/` files and the changelog section.

## Memory and CPU budget

| Resource | Notes |
|---|---|
| Flash | Application 896 KB (bootloader in sector 0) |
| RAM_D1 (320 KB) | ~92% used by stock — avoid new large buffers here |
| DTCM (128 KB) | Unused by stock; CPU-only (no DMA). Put plugin tables here with `__attribute__((section(".DTCMRAM")))` |
| SDRAM (16 MB) | Samples |
| Audio CPU | 29.9% avg / 32.2% peak measured with 10 tracks + effects |

## Extension block in Files (`.rwf`)

From 1.01-cy.7, File → SAVE appends a **CYBX block** after the stock `EOF` marker. Stock firmware reads fixed sections and stops at `EOF`, so it still loads these files (and a stock save drops the block but keeps the beats). Layout and contents: [Generator.md → File format](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Generator.md#file-format-cybx-block).

## Known upstream issue

`layerSong_resetAllBeats()` in `Controller.cpp` contains `(menu = LAYER_SONG_MENU)` — an assignment instead of a comparison — so clearing the selected track can switch the screen to the Layer Song menu. The generator avoids that function. (Jira DRUMB-24.)
