# Plugin development guide

Plugins are native C++ classes compiled into the firmware. They are registered at boot, switched on per SD card (`Plugins/plugins.txt`), and run from the main loop — never inside an interrupt. Everything here is built only with `DRUMBOY_PLUGINS` (on in all presets except `Stock`).

User-facing overview: [docs/README.md](../../../../docs/README.md).

## Files in this folder

| File | What it is | Docs |
|---|---|---|
| `Plugin.h` | Plugin interface, show-canvas constants, host API | this page |
| `PluginHost.cpp` | Event queue, config, dispatch, frames, messages, audio tap | this page |
| `BuiltinPlugins.cpp` | Registration list | this page |
| `CpuMeter.cpp` | `cpu_meter` | [CpuMeter.md](CpuMeter.md) |
| `HookMonitor.cpp` | `hook_monitor` | [HookMonitor.md](HookMonitor.md) |
| `Psychedelic.cpp` | `psychedelic` (show mode) | [Psychedelic.md](Psychedelic.md) |
| `Manager.cpp/.h` | Plugin & Firmware Manager, show mode | [Manager.md](Manager.md) |
| `Generator.cpp/.h` | Pattern generator page, file block | [Generator.md](Generator.md) |
| `PatternGen.cpp/.h` | Pattern engine (pure logic, host-testable) | [Generator.md](Generator.md#engine-patterngen) |
| `KeyStepFx.cpp` | `keystep_fx`: MIDI processor for an external sequencer | [KeyStepFx.md](KeyStepFx.md) |
| `MidiSplit.h` | MIDI-in split parser (pure logic, host-testable) | [KeyStepFx.md](KeyStepFx.md#how-the-split-works) |

Hook macros live in `Core/Library/Global/PluginHooks.h`; the profiler used by the CPU meter in `Core/Library/Global/Profiler.*`.

## How it fits together

```
 interrupts (audio, play timer, MIDI, keys)          main loop
 ─────────────────────────────────────────          ─────────────────────────────────────────
 PLUGIN_EVENT / PLUGIN_KEYPAD ──► queue (128) ──►  pluginHost_update()
                                                      1. up to 32 events:
                                                         generator → manager → every running plugin's onEvent()
                                                      2. every 33 ms (~30 fps):
                                                         save LCD state
                                                         generator page frame  OR  every plugin's onFrame() + message line
                                                         manager / show-mode frame
                                                         restore LCD state
```

- The queue is interrupt-safe; when it is full, new events are counted as dropped (`pluginHost_getDroppedEvents()`).
- The generator and manager are pages, not `Plugin`s: they see events first and swallow the keys and knobs they use.
- The host saves and restores the LCD font, spacing, alignment and colours around frames, so a plugin may change them freely.
- A plugin that is not switched on (or whose `onStart()` returned false) gets no calls at all.

## The interface

```cpp
class Plugin {
   public:
    virtual const char* id() const = 0;                 // Key in plugins.txt, e.g. "cpu_meter"
    virtual const char* name() const = 0;               // Shown in the manager
    virtual const char* kind() const;                   // "BUILT-IN" (manager column)

    virtual bool onStart();                             // Once at boot when enabled; false = stays off
    virtual bool isActive() const;                      // Return false after a runtime failure
    virtual void onEvent(const PluginEvent& event);     // Once per queued event
    virtual void onFrame(Lcd& lcd);                     // ~30 fps

    // Show mode (see Manager.md#show-mode)
    virtual bool canShow() const;                       // true = manager offers SELECT = SHOW
    virtual void onShowStart();
    virtual void onShowFrame(Lcd& lcd);                 // Instead of onFrame while showing
    virtual void onShowEnd();
};
```

## Events

`PluginEvent { type, a, b, c, tick }` — `tick` is `HAL_GetTick()` when the event was pushed.

| Type | a | b | c | Pushed from |
|---|---|---|---|---|
| `PLUGIN_EVENT_STEP` | step (16th) low byte | step high byte | – | play timer, each 16th |
| `PLUGIN_EVENT_NOTE` | layer 0-9 | velocity 0-127 | – | each triggered beat |
| `PLUGIN_EVENT_KEY` | keypad (0 layer, 1 left, 2 right) | key index | 1 press / 0 release | keypad scan |
| `PLUGIN_EVENT_ENCODER` | encoder 0-7 | delta (`int8_t`, ±1) | – | encoder scan |
| `PLUGIN_EVENT_MIDI_IN` | byte (clock / active sensing filtered) | – | – | UART RX |
| `PLUGIN_EVENT_MIDI_OUT` | status | data 1 | data 2 | MIDI send (clock filtered) |

Key indices: see [docs/README.md → Key names](../../../../docs/README.md#key-names).

## Hooks into the stock firmware

All are macros in `PluginHooks.h` that compile to nothing (or to a constant) in the `Stock` build.

| Macro | Where | Purpose |
|---|---|---|
| `PLUGIN_INIT(controller)` | `main.cpp`, after `controller.initialize()` | Register, read `plugins.txt`, start plugins |
| `PLUGIN_UPDATE(controller)` | main loop | Events and frames |
| `PLUGIN_EVENT(type, a, b, c)` | Controller | Push an event |
| `PLUGIN_KEYPAD(keypad, states, count)` | Controller | Push key press/release events |
| `PLUGIN_INPUT_CAPTURED()` | Controller key/encoder dispatch | A page owns key **presses** and knobs (releases still pass, so held modifiers don't stick) |
| `PLUGIN_SCREEN_CAPTURED()` | `lcd_update()` call | Manager/show mode own the lane area; stock redraws pause |
| `PLUGIN_KEY_PASSES(keypad, key)` | capture filter | Keys still allowed through (PLAY/STOP and RESET in the generator) |
| `PLUGIN_AUDIO_TAP(buffer, frames)` | I2S callbacks | Level/bass/treble peaks (~200 flops per block) |
| `PLUGIN_BEAT_GATE(layer, bank, beat, start, volume)` | play timer, before a beat plays | Generated steps: probability roll and velocity |
| `PLUGIN_BEAT_COLOR(lcd, layer, bank, beat, start)` | `lcd_drawBeat` | Generated steps: tick colour by probability |
| `PLUGIN_FILE_SAVE(file)` | `sd_saveFile`, after `EOF` | Append the CYBX block |
| `PLUGIN_FILE_LOADED(file, path, ok)` | `sd_loadFile` | Read the CYBX block |
| `PLUGIN_MIDI_RX(byte)` | `receiveMidiCommand`, UART RX interrupt, before the MIDI-IN mode check | KeyStep FX: true = plugin owns the byte, stock parser skips it |

Adding a hook: add the macro in both branches of `PluginHooks.h`, call it from the stock code, then rebuild the `Stock` preset and check the binary is still identical to the official one.

## Host API (`Plugin.h`)

| Function | Use |
|---|---|
| `pluginHost_getAudio()` | `{level, bass, treble}` peaks 0..1 since the last frame |
| `pluginHost_message(text, error)` | One-line message in the top strip (6 s, errors red for 15 s) |
| `pluginHost_getDroppedEvents()` | Queue overflow counter |
| `kShowX/kShowY/kShowWidth/kShowHeight` | Show canvas: 20, 131, 920×310 |
| `Lcd::beginStream(x0,y0,x1,y1)` / `streamPixel(c)` / `endStream()` | Fast pixel streaming into a rectangle |

Plugins that need the Controller's internals use `struct PluginAccess` (declared a `friend` of `Controller`); see `Generator.cpp`. Keep such access in one place.

## Writing a plugin

1. Create `MyPlugin.cpp` in this folder (the build globs `Core/Plugin/*.cpp`):

```cpp
#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Lcd.h"

class StepFlash : public Plugin {
   public:
    const char* id() const override { return "step_flash"; }
    const char* name() const override { return "Step Flash"; }

    void onEvent(const PluginEvent& e) override {
        if (e.type == PLUGIN_EVENT_STEP) step = e.a | (e.b << 8);
    }

    void onFrame(Lcd& lcd) override {
        if (step == drawn) return;                      // Draw only on change: the LCD bus is slow
        drawn = step;
        lcd.setForeColor(((step & 3) == 0) ? RED : GRAY_50);
        lcd.fillRect(930, 6, 8, 8);
    }

   private:
    uint16_t step = 0, drawn = 0xFFFF;
};

static StepFlash stepFlash;

Plugin* builtin_stepFlash() {
    return &stepFlash;
}

#endif
```

2. Register it in `BuiltinPlugins.cpp` (registration order = drawing order; at most 8 plugins, `kPluginMaxCount`):

```cpp
Plugin* builtin_stepFlash();
...
pluginHost_register(builtin_stepFlash());
```

3. Add `step_flash = off  # what it does` to `02 Software/Sd Card/Plugins/plugins.txt`, a `StepFlash.md` beside the source, and a CHANGELOG entry; bump `VERSION`.
4. Build, copy the package and `plugins.txt` to the card, switch it on (in the file or the manager), restart.

## Rules of thumb

- **Where to draw:** the top strip (y 0–20) is empty in every stock layout: line y=6 (CPU meter) and y=16 (hook monitor, messages). Elsewhere you will fight the stock UI's redraws — use show mode instead.
- **Draw only what changed.** The LCD is a 16-bit bit-banged bus; full-screen redraws take tens of ms.
- **Time per frame:** the main loop also runs the stock UI. Keep `onFrame` to a few ms; show mode may use more (Psychedelic adapts its block size to stay smooth).
- **Memory:** RAM_D1 is ~92% full. Put tables and buffers in DTCM: `static float table[N] __attribute__((section(".DTCMRAM")));` (zero at boot is not guaranteed — initialise in `onStart`). DTCM is CPU-only: never hand it to DMA (FatFs, LCD DMA).
- **SD card:** the host borrows `controller.sd.file`; don't add another `FIL` (4 KB each). Never access the card from `onEvent` during playback-critical paths if avoidable.
- **Names:** stock `Global.h` defines many globals (e.g. `sinTable`); prefix yours.
- **Interrupts:** never call plugin code from a hook directly; push an event. Hooks that run in interrupts (`AUDIO_TAP`, `BEAT_GATE`, `MIDI_RX`) must be tiny and allocation-free.
- **Stock check:** after touching stock files, build `Stock` and compare against the official binary.
