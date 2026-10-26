# Psychedelic (`psychedelic`)

Source: `Psychedelic.cpp`. Enable: `psychedelic = on`, restart.

Full-canvas, endlessly evolving, audio-reactive visuals in [show mode](Manager.md#show-mode).

## Using it

1. File menu → **FILE** again (manager) → select `psychedelic` → **SELECT**.
2. Turn the knobs; the knob's name and value appear briefly.
3. **FILE** returns to the normal screen. The sequencer keeps playing throughout.

| Knob | Controls |
|---|---|
| 1 MODE | AUTO (new visual every 20 s), JULIA, PLASMA, WARP, KALEIDO |
| 2 SPEED | How fast it evolves |
| 3 ZOOM | Zoom level |
| 4 COLOUR | Colour cycling speed |
| 5 DETAIL | AUTO (adaptive) or fixed 2 / 3 / 4 / 6-pixel blocks |
| 6 REACTION | How strongly it reacts to the music |
| 7 SYMMETRY | Kaleidoscope folds (KALEIDO) |
| 8 PALETTE | RAINBOW, CANDY, ACID, FIRE, NEON, DUSK |

| Mode | Picture |
|---|---|
| JULIA | Morphing Julia fractal with orbit-trap colouring and mirror tiling |
| PLASMA | Flowing sine colour fields |
| WARP | Fractal bent by travelling waves |
| KALEIDO | Kaleidoscope folded around the centre |

Reacting to the music:
- kick (track 1 notes) and the mix's **bass** pulse the zoom and brightness,
- snare (track 2 notes) jumps the colours,
- **treble** and tracks 3–4 (hats) shimmer the shape.

## How it works

- **Never repeats:** all motion comes from a slowly advancing time value fed through several oscillators at unrelated frequencies.
- **Colour:** cosine palettes (`a + b·cos(2π(c·t + d))`) baked into a 256-entry RGB565 table, rotated continuously.
- **Rendering:** the image is computed in square cells and streamed to the LCD one band of cells at a time with `Lcd::beginStream / streamPixel / endStream` (no frame buffer). In AUTO detail the block size grows or shrinks to stay around 25 fps or better.
- **Audio:** `pluginHost_getAudio()` (level / bass / treble peaks from the I2S tap) plus NOTE events for kick and snare.
- **Memory:** sine table, palette and row buffer live in DTCM (`psySin`, `psyPalette`, `psyRow`; `PSY_FAST_SECTION`, overridable for host-side test renders).

Status: renders verified on the Mac (host build); device performance still to be tuned (Jira DRUMB-17).
