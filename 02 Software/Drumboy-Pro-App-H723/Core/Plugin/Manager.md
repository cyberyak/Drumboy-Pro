# Plugin & Firmware Manager

Source: `Manager.cpp`, `Manager.h`. Part of the firmware (always available, not listed in `plugins.txt`).

## Opening

Press **FILE** to open the File menu, then press **FILE** again. The manager draws inside the 10 song lanes (lane 0 title, lanes 1–8 items, lane 9 status) and restores them when it closes. The sequencer and audio keep running; other keys and knobs are paused. It does not open while recording.

## Controls

| Control | Plugins page | Firmware page |
|---|---|---|
| **UP / DOWN** or knob 1 | Move the cursor | Move the cursor |
| **LEFT / RIGHT** | Switch page | Switch page |
| **GLOBAL** | Switch the plugin on/off | Press twice to install the file |
| **SELECT** | Start the plugin's show mode | – |
| **FILE** | Close (saves `plugins.txt`) | Close |

## Plugins page

Lists every registered plugin with its state:

| Shown | Meaning |
|---|---|
| `RUNNING` | Started at boot |
| `RUNNING  SELECT = SHOW` | Running and has a show mode |
| `RUNNING - OFF AFTER RESTART` | Switched off here; stops after a restart |
| `ON AFTER RESTART` | Switched on here; starts after a restart |
| (empty) | Off |

On close, changes are written to `Plugins/plugins.txt` (the file is rewritten, comments are not kept) and apply after a restart.

## Firmware page

Lists up to 12 valid update files from the SD card's `Firmware/` folder (`Drumboy_Pro_Fw*.bin`, correct header, ≤ 896 KB). Selecting one and pressing **GLOBAL** twice:

1. checks the file (header + CRC32),
2. copies it to the SD root in 2 KB chunks (progress shown),
3. restarts — the bootloader installs it as a normal SD update.

The library copy is kept. If the check fails nothing is changed. Stock firmware has no manager; to come back from stock, copy a cyberyak package to the SD root by hand.

Don't keep the library in `System/Firmware/` — the bootloader empties it on every update.

## Show mode

A plugin whose `canShow()` returns true can take over the **show canvas**: the 10 track rows, `x 20–939, y 131–440` (920×310 px, `kShowX/kShowY/kShowWidth/kShowHeight`).

- Start: manager → select the plugin → **SELECT** (the plugin must be running).
- While showing, the plugin gets `onShowFrame()` every frame instead of `onFrame()`, and all keys and knobs (except FILE) as events. The canvas is cleared to black once at start.
- **FILE** ends it: `onShowEnd()`, then the rows are redrawn with the background image and the stock `lcd_drawLayerTab()`, and the manager closes.
- The sequencer keeps playing; `lcd_update()` is paused (`PLUGIN_SCREEN_CAPTURED`).

Implementing it: see [Psychedelic.md](Psychedelic.md) and the [plugin guide](README.md).

## Implementation notes

- Opening is detected in `manager_handleEvent`: a FILE press while the File menu was already open in the previous pass (`wasFileMenu`).
- Lanes are restored in `manager_endUpdate` with `lcd_drawSong()` per lane; the generator's EVOLVE skips redraws while the manager owns the screen, so the restore also picks up its changes.
