#ifndef __PLUGIN_HOOKS_H
#define __PLUGIN_HOOKS_H

#include <stdint.h>

// clang-format off

/* Plugin Hooks -----------------------------------------------------*/
// Entry points from the stock firmware into the plugin host (Core/Plugin).
// Enabled with -DDRUMBOY_PLUGINS (CMake preset "PluginDev"); every hook
// compiles to nothing when disabled, so the stock build is unchanged.
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

class Lcd;

void pluginHost_initialize();
void pluginHost_update(Lcd& lcd);
void pluginHost_push(PluginEventType type, uint8_t a, uint8_t b, uint8_t c);
void pluginHost_pushKeypad(uint8_t keypad, const uint8_t* changeState, uint8_t count);

#define PLUGIN_INIT()                               pluginHost_initialize()
#define PLUGIN_UPDATE(lcd_)                         pluginHost_update(lcd_)
#define PLUGIN_EVENT(type_, a_, b_, c_)             pluginHost_push((type_), (uint8_t)(a_), (uint8_t)(b_), (uint8_t)(c_))
#define PLUGIN_KEYPAD(keypad_, states_, count_)     pluginHost_pushKeypad((keypad_), (const uint8_t*)(states_), (count_))

#else

#define PLUGIN_INIT()                               ((void)0)
#define PLUGIN_UPDATE(lcd_)                         ((void)0)
#define PLUGIN_EVENT(type_, a_, b_, c_)             ((void)0)
#define PLUGIN_KEYPAD(keypad_, states_, count_)     ((void)0)

#endif

// clang-format on

#endif
