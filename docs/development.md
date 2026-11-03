# Development

How to build, version and release the cyberyak firmware, and what to watch for when changing it. To write a plugin, see the [plugin development guide](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/README.md). To test without a device, see [Simulator](simulator.md).

## Building

Requirements:
- CMake on PATH.
- Ninja, on PATH or the copy the STM32 VS Code extension bundles under `~/Library/Application Support/stm32cube/bundles/` (`cmake/find_ninja.cmake` finds it).
- The [Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) 14.2 (`arm-none-eabi`).

`cmake/arm-none-eabi.cmake` looks for `arm-none-eabi-gcc` in this order:

1. `ARM_TOOLCHAIN_DIR` (`-DARM_TOOLCHAIN_DIR=…` or an environment variable): the unpacked toolchain folder that contains `bin/`.
2. `arm-gnu-toolchain-*-arm-none-eabi` in `tools/` inside the repo, or in `tools/` next to the checkout (handy with several worktrees side by side).
3. `PATH`.

In VS Code, `.vscode/settings.json` runs CMake through `cube-cmake`, the STM32 extension's wrapper for its bundled CMake. No tool versions or paths are written in the repo.

```sh
cd "02 Software/Drumboy-Pro-App-H723"
cmake --preset Debug          # add --fresh after changing option defaults
cmake --build --preset Debug
```

| Preset | What it builds |
|---|---|
| `Debug` | The release firmware (-Ofast, the same flags as the official build) with plugins and the label |
| `Stock` | `DRUMBOY_PLUGINS=OFF`, `DRUMBOY_BRANDING=OFF`: byte-identical to the official firmware, to check the hooks change nothing |
| `Release` | An -Os variant |
| `Simulator` | The macOS host build, see [Simulator](simulator.md) |

The post-build step (`cmake/make_firmware_package.py`) writes the SD-card package next to the `.elf`. Its header is `RW_DRUMBOY_PRO_FIRMWARE ` plus the version, size and CRC32, followed by the image.

| Option | Default | Effect |
|---|---|---|
| `DRUMBOY_PLUGINS` | ON | Plugin host, hooks, manager, generator and built-in plugins. When off, every hook macro compiles to nothing |
| `DRUMBOY_BRANDING` | ON | The `CYBERYAK <version>` label in the File menu |

The bootloader (`02 Software/Drumboy-Pro-Boot-H723`) has no CMake build yet. Build and flash it over SWD from STM32CubeIDE with an ST-Link wired to `SWDIO` (PA13), `SWCLK` (PA14) and `GND`.

## Versioning

`<base>-cy.<build>`, e.g. `1.01-cy.9` = Randomwaves 1.01 + cyberyak build 9.

- **base** comes from `kVersionMajor` / `kVersionMinor` in `Core/Library/Global/Global.h`, which is Randomwaves' own version. The bootloader shows only this, so a cyberyak build never looks like a different official version.
- **build** is the only content of `02 Software/Drumboy-Pro-App-H723/VERSION` (`cy.9`). Every merged firmware change bumps it and adds a `CHANGELOG.md` entry.
- Package name: `Drumboy_Pro_Fw_01_01_cy09.bin`.
- On the device: `CYBERYAK 1.01-CY.9` in the top-right corner while the File menu is open.

## Branches and releasing

| Branch | Holds |
|---|---|
| `main` | Upstream Randomwaves, untouched |
| `dev` | What gets released: everything merged and tested |
| `feature/*` | Work in progress, e.g. `feature/keystep-fx` (KeyStep Pro MIDI FX), kept out of releases until it's ready |
| `release/<version>` | The state a release was cut from |

To release:

1. Merge the feature branches into `dev` (PR), bump `VERSION` and add the changelog entry.
2. Run the simulator tests (`Simulator/tools/run_tests.sh`) and, ideally, try the package on a device.
3. `git tag v1.01-cy.9 && git push origin v1.01-cy.9`.
4. CI (`.github/workflows/build.yml`) checks the tag matches the firmware version, builds, and publishes a GitHub Release with the package, the stock package, the `Plugins/` files and the changelog section.

## Memory and CPU budget

| Resource | Notes |
|---|---|
| Flash | Application 896 KB (the bootloader is in sector 0) |
| RAM_D1 (320 KB) | ~92% used by stock. Avoid new large buffers here |
| DTCM (128 KB) | Unused by stock, CPU only (no DMA). Put plugin tables here with `__attribute__((section(".DTCMRAM")))` |
| SDRAM (16 MB) | Samples |
| Audio CPU | 29.9% average / 32.2% peak measured with 10 tracks and effects |

## Extension block in Files (`.rwf`)

Since 1.01-cy.7, File → SAVE appends a **CYBX block** after the stock `EOF` marker. Stock firmware reads fixed sections and stops at `EOF`, so it still loads these files. A stock save drops the block but keeps the beats. For the layout and contents, see [Generator.md → File format](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Generator.md#file-format-cybx-block).

## Known issues

- **Upstream:** `layerSong_resetAllBeats()` in `Controller.cpp` contains `(menu = LAYER_SONG_MENU)`, an assignment instead of a comparison, so clearing the selected track can switch the screen to the Layer Song menu. The generator avoids that function (Jira DRUMB-24).
- **Upstream:** samples at 48, 24, 16 and 8 kHz play about 8.8% slow (roughly 1.5 semitones flat), because the loader repeats or drops whole frames instead of resampling. 96 and 192 kHz files can alias.
- **Upstream:** the simulator reports several `sprintf` calls that write past the end of their buffers (`Controller.cpp`). On the device the extra bytes overwrite the next variable.

The task board is the Jira project DRUMB.
