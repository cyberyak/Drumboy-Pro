#ifndef __PLUGIN_HOOKS_H
#define __PLUGIN_HOOKS_H

#include <stdint.h>

// clang-format off

/* Plugin Hooks -----------------------------------------------------*/
// Entry points from the stock firmware into the plugin host (Core/Plugin).
// Enabled with -DDRUMBOY_PLUGINS (on in all presets except "Stock"); every
// hook compiles to nothing when disabled, so the stock build is unchanged.
//
// Hooks only push small events into an interrupt-safe queue. Plugins are
// run later from the main loop, never inside the audio or timer interrupts.

enum PluginEventType : uint8_t {
    PLUGIN_EVENT_NONE                               = 0x00,
    PLUGIN_EVENT_STEP                               = 0x01,     // a = step (16th) low byte, b = step high byte
    PLUGIN_EVENT_NOTE                               = 0x02,     // a = layer, b = velocity 0-127
    PLUGIN_EVENT_KEY                                = 0x03,     // a = keypad (0 layer, 1 left, 2 right), b = key, c = 1 press / 0 release
    PLUGIN_EVENT_ENCODER                            = 0x04,     // a = encoder, b = delta (int8_t)
    PLUGIN_EVENT_MIDI_IN                            = 0x05,     // a = byte (clock/active-sensing filtered out)
    PLUGIN_EVENT_MIDI_OUT                           = 0x06      // a, b, c = message bytes (clock filtered out)
};

struct PluginEvent {
    PluginEventType type;
    uint8_t a;
    uint8_t b;
    uint8_t c;
    uint32_t tick;                                              // HAL_GetTick() at push time
};

#ifdef DRUMBOY_PLUGINS

#include "ff.h"

class Controller;
class Lcd;

void pluginHost_initialize(Controller& controller);
void pluginHost_update(Controller& controller);
void pluginHost_push(PluginEventType type, uint8_t a, uint8_t b, uint8_t c);
void pluginHost_pushKeypad(uint8_t keypad, const uint8_t* changeState, uint8_t count);
bool pluginHost_isCapturing();
bool pluginHost_isScreenCaptured();
bool pluginHost_keyPassesThrough(uint8_t keypad, uint8_t key);
void pluginHost_audioTap(const uint32_t* interleaved, uint16_t frames);
void pluginHost_beatGate(uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval, float& volume);
void pluginHost_beatColor(Lcd& lcd, uint8_t layer, uint8_t bank, uint8_t beatNum, uint16_t startInterval);
void pluginHost_fileSaved(FIL& file);
void pluginHost_fileLoaded(FIL& file, const char* path, bool songLoaded);
bool pluginHost_midiRx(uint8_t byte);
bool pluginHost_midiTx(const uint8_t*& data, uint8_t len);

#define PLUGIN_INIT(controller_)                    pluginHost_initialize(controller_)
#define PLUGIN_UPDATE(controller_)                  pluginHost_update(controller_)
#define PLUGIN_EVENT(type_, a_, b_, c_)             pluginHost_push((type_), (uint8_t)(a_), (uint8_t)(b_), (uint8_t)(c_))
#define PLUGIN_KEYPAD(keypad_, states_, count_)     pluginHost_pushKeypad((keypad_), (const uint8_t*)(states_), (count_))
// True while a plugin page (manager, show mode, generator) owns key presses and knobs.
#define PLUGIN_INPUT_CAPTURED()                     pluginHost_isCapturing()
// True while a plugin page also owns the screen area of the song lanes / track rows; lcd_update() pauses.
#define PLUGIN_SCREEN_CAPTURED()                    pluginHost_isScreenCaptured()
// Key presses that still reach the stock UI while input is captured (e.g. transport in the generator).
#define PLUGIN_KEY_PASSES(keypad_, key_)            pluginHost_keyPassesThrough((keypad_), (key_))
// Output level/bass/treble peaks for visual plugins; called in the I2S interrupt after each block (~200 flops).
#define PLUGIN_AUDIO_TAP(buffer_, frames_)          pluginHost_audioTap((buffer_), (frames_))
// Generated steps: per-step probability roll and velocity, just before a beat plays (play-timer interrupt).
#define PLUGIN_BEAT_GATE(layer_, bank_, beat_, start_, volume_) pluginHost_beatGate((layer_), (bank_), (beat_), (start_), (volume_))
// Generated steps: beat tick colour by probability (lcd_drawBeat, non-selected beats).
#define PLUGIN_BEAT_COLOR(lcd_, layer_, bank_, beat_, start_)   pluginHost_beatColor((lcd_), (layer_), (bank_), (beat_), (start_))
// Files: append / read the cyberyak extension block (generator data) after the stock EOF marker.
#define PLUGIN_FILE_SAVE(file_)                     pluginHost_fileSaved(file_)
#define PLUGIN_FILE_LOADED(file_, path_, ok_)       pluginHost_fileLoaded((file_), (path_), (ok_))
// MIDI-in split (UART RX interrupt, every byte): true = a plugin owns the byte and the stock parser skips it.
#define PLUGIN_MIDI_RX(byte_)                       pluginHost_midiRx(byte_)
// MIDI-out filter (inside enqueueMidiTx's critical section): may point `data_` at a remapped copy;
// true = drop the message. Messages a plugin sends itself bypass the filter.
#define PLUGIN_MIDI_TX_DROP(data_, len_)            pluginHost_midiTx((data_), (len_))

#else

#define PLUGIN_INIT(controller_)                    ((void)0)
#define PLUGIN_UPDATE(controller_)                  ((void)0)
#define PLUGIN_EVENT(type_, a_, b_, c_)             ((void)0)
#define PLUGIN_KEYPAD(keypad_, states_, count_)     ((void)0)
#define PLUGIN_INPUT_CAPTURED()                     (false)
#define PLUGIN_SCREEN_CAPTURED()                    (false)
#define PLUGIN_KEY_PASSES(keypad_, key_)            (true)
#define PLUGIN_AUDIO_TAP(buffer_, frames_)          ((void)0)
#define PLUGIN_BEAT_GATE(layer_, bank_, beat_, start_, volume_) ((void)0)
#define PLUGIN_BEAT_COLOR(lcd_, layer_, bank_, beat_, start_)   ((void)0)
#define PLUGIN_FILE_SAVE(file_)                     ((void)0)
#define PLUGIN_FILE_LOADED(file_, path_, ok_)       ((void)0)
#define PLUGIN_MIDI_RX(byte_)                       (false)
#define PLUGIN_MIDI_TX_DROP(data_, len_)            (false)

#endif

// clang-format on

#endif
