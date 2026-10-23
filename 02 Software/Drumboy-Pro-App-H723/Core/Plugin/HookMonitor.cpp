#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Lcd.h"

// clang-format off

const uint16_t kHookMonitorTextX                    = 20;
const uint16_t kHookMonitorTextY                    = 16;               // Below the CPU meter line, above the menu headers
const uint16_t kHookMonitorTextSize                 = 128;

// clang-format on

/* Hook Monitor -----------------------------------------------------*/
// Test plugin: shows the latest value of every hook in the top strip so each
// hook can be verified on the device (step, note, keys, encoders, MIDI).

class HookMonitor : public Plugin {
   public:
    const char* name() const override { return "Hook Monitor"; }

    void onEvent(const PluginEvent& e) override {
        switch (e.type) {
            case PLUGIN_EVENT_STEP:
                step = e.a | (e.b << 8);
                break;

            case PLUGIN_EVENT_NOTE:
                noteLayer = e.a;
                noteVelocity = e.b;
                noteCount++;
                break;

            case PLUGIN_EVENT_KEY:
                keyPad = e.a;
                keyNum = e.b;
                keyDown = e.c;
                break;

            case PLUGIN_EVENT_ENCODER:
                encNum = e.a;
                encDelta = (int8_t)e.b;
                encSum += encDelta;
                break;

            case PLUGIN_EVENT_MIDI_IN:
                midiIn = e.a;
                midiInCount++;
                break;

            case PLUGIN_EVENT_MIDI_OUT:
                midiOut[0] = e.a;
                midiOut[1] = e.b;
                midiOut[2] = e.c;
                break;

            default:
                break;
        }
        dirty = true;
    }

    void onFrame(Lcd& lcd) override {
        if (!dirty) return;
        dirty = false;

        char text[kHookMonitorTextSize];
        int len = snprintf(text, sizeof(text),
                           "STEP %4u  NOTE L%02u V%3u #%5lu  KEY P%u K%02u %s  ENC %u %+d %+5ld  MIDI IN %02X #%4lu OUT %02X %02X %02X  DROP %lu  ",
                           step, noteLayer + 1, noteVelocity, noteCount, keyPad, keyNum, keyDown ? "DN" : "UP",
                           encNum + 1, encDelta, encSum, midiIn, midiInCount, midiOut[0], midiOut[1], midiOut[2],
                           pluginHost_getDroppedEvents());
        if (len <= 0) return;
        if (len >= (int)sizeof(text)) len = sizeof(text) - 1;

        lcd.setFont(FONT_05x07);
        lcd.setAlignment(LEFT);
        lcd.setForeColor(CYAN);
        lcd.setBackColor(BLACK);
        lcd.drawText(text, len, kHookMonitorTextX, kHookMonitorTextY);
    }

   private:
    bool dirty = true;
    uint16_t step = 0;
    uint8_t noteLayer = 0, noteVelocity = 0;
    uint32_t noteCount = 0;
    uint8_t keyPad = 0, keyNum = 0, keyDown = 0;
    uint8_t encNum = 0;
    int8_t encDelta = 0;
    int32_t encSum = 0;
    uint8_t midiIn = 0;
    uint32_t midiInCount = 0;
    uint8_t midiOut[3] = {0, 0, 0};
};

static HookMonitor hookMonitor;

void registerBuiltinPlugins() {
    pluginHost_register(&hookMonitor);
}

#endif
