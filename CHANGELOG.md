# Changelog

Custom Drumboy Pro firmware (fork of [Randomwaves/Drumboy-Pro](https://github.com/Randomwaves/Drumboy-Pro)).

**Versioning**
- The version lives in `02 Software/Drumboy-Pro-App-H723/VERSION` (`MAJOR.MINOR`). The bootloader shows it during an SD-card update and accepts major 0–9, minor 00–99.
- Stock Randomwaves firmware is 1.0x. Custom releases start at **1.50**.
- Every merged PR that changes the firmware bumps the minor version and adds an entry below.
- Releasing: merge to `dev`, then `git tag vX.YY && git push origin vX.YY`. CI checks the tag matches `VERSION` and publishes a GitHub Release with the SD-card package and this version's notes.

## [Unreleased]

## [1.50]

First custom release. Stock behaviour is unchanged; the extras below are only in the development builds.

### Added
- CMake/Ninja build and VS Code setup (no STM32CubeIDE needed); `Debug` preset matches the official firmware.
- SD-card package script (byte-identical to the official 1.01 package format).
- GitHub Actions build: artifact on every push/PR, GitHub Release on version tags.
- `CpuMeter` preset: on-screen audio and main-loop load. Measured 29.9% average / 32.2% peak with 10 tracks and effects.
- `PluginDev` preset: plugin hooks (step, note, key, encoder, MIDI in/out), interrupt-safe event queue, plugin host and the Hook Monitor test plugin.
- `VERSION` file as the single source of the firmware version; shown in the CPU meter line.
