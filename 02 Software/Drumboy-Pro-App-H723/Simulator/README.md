# Drumboy Pro simulator

Runs the real firmware on macOS. Work in progress (Jira epic DRUMB-29). For now it runs headless; the window, audio and MIDI come later.

## Build and run

```sh
cmake --preset Simulator
cmake --build --preset Simulator

# Card image from a folder with the SD card contents (once)
Simulator/tools/make_card_image.sh ~/Projects/DrumBoy/sd-backup/2026-10-23 ~/Projects/DrumBoy/sim-card/drumboy.img

build/Simulator/Simulator/DrumboySim --card ~/Projects/DrumBoy/sim-card/drumboy.img --run-ms 15000
```

`--run-ms` stops the simulator after that many milliseconds. It exits with 0 if the firmware reached its main loop.

## How it works

The application (`Core/Library`, `Core/Plugin`, `main.cpp`), the CubeMX init files (`Core/Src`) and FatFs compile unchanged against the real ST headers. The simulator replaces only the hardware layer:

| Device | Simulator |
|---|---|
| CMSIS core (`core_cm7.h`, intrinsics) | `hal/core_cm7.h`, `src/sim_cpu.cpp` |
| HAL driver sources | `src/sim_hal.cpp` |
| SDRAM, RAM_D3, peripheral and core registers | memory mapped at the same addresses (`src/sim_memory.cpp`) |
| SD card (`sd_diskio.c`) | FatFs on a raw card image (`src/sim_diskio.cpp`) |
| `main()` | compiled as `firmware_main()` and run on its own thread (`src/sim_main.cpp`) |

`hal/sim_prelude.h` is force-included into every source. It turns the MCU `section` attributes into no-ops, gives FatFs 32-bit `DWORD`s, and restores `__weak`.

**Why x86_64:** the firmware keeps MCU addresses in `uint32_t` (SDRAM `0xC0000000`, icons at `0x38000000`, `DWT_CYCCNT`). The simulator therefore maps memory at those exact addresses, below 4 GB. arm64 macOS reserves the whole low 4 GB, while an x86_64 binary linked with a small `__PAGEZERO` may use it. On Apple silicon it runs under Rosetta.

The stock and Debug firmware builds don't change: simulator code lives only in `Simulator/` and behind the `DRUMBOY_SIMULATOR` CMake option.
