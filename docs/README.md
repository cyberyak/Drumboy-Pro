# Documentation

Documentation for the cyberyak firmware for the Randomwaves Drumboy Pro. The [main README](../README.md) gives the short version.

| Page | For | What's in it |
|---|---|---|
| [Getting started](getting-started.md) | Players | Install, update, go back to stock, the SD card layout, switching plugins on, key names |
| [Features](features.md) | Players | A tour with screenshots: the pattern generator, Plugin & Firmware Manager, visualizer, CPU meter, fixes |
| [Simulator](simulator.md) | Players and developers | Run the firmware on a Mac, with a front panel window or headless in scripted tests |
| [Development](development.md) | Developers | Building, presets and options, versioning, releasing, branches, memory budget, file format, known issues |
| [CHANGELOG](../CHANGELOG.md) | Everyone | What changed in each release |

Reference docs next to the code:

| Topic | Doc |
|---|---|
| Pattern generator (all four pages, file format) | [Generator.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Generator.md) |
| Plugin & Firmware Manager, show mode | [Manager.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Manager.md) |
| Writing a plugin (hooks, events, frames) | [Plugin development guide](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/README.md) |
| CPU Meter, Hook Monitor, Psychedelic plugins | [CpuMeter.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/CpuMeter.md), [HookMonitor.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/HookMonitor.md), [Psychedelic.md](../02%20Software/Drumboy-Pro-App-H723/Core/Plugin/Psychedelic.md) |
| Simulator internals and script commands | [Simulator/README.md](../02%20Software/Drumboy-Pro-App-H723/Simulator/README.md) |

All screenshots in [images/](images/) come from the simulator running the real firmware. To take them again, run `Simulator/tools/docs_screenshots.sh` (see [Simulator](simulator.md#screenshots-for-these-docs)).
