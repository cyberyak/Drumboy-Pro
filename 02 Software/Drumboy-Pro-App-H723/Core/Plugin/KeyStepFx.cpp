#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Controller.h"
#include "MidiSplit.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kKsfxDefaultChannels                 = 0x001F;           // MIDI channels 1-5 (settings page: DRUMB-53)

// clang-format on

extern Controller controller;                                           // main.cpp

/* KeyStep FX -------------------------------------------------------*/
// MIDI processor for an external sequencer (KeyStep Pro) wired into the
// Drumboy's MIDI In. Its channels are taken out of the stock MIDI stream in the
// UART RX interrupt (PLUGIN_MIDI_RX), processed, and merged into MIDI Out; the
// Drumboy's own MIDI keeps working on the other channels.
//
// Phase 1 / DRUMB-50: split + pass-through. Effects come in later phases.

static MidiSplit ksfxSplit;
static volatile bool ksfxRunning = false;
static volatile uint32_t ksfxInCount[16];                               // Complete messages per channel
static volatile uint32_t ksfxTxDropped = 0;                             // TX ring full

class KeyStepFx : public Plugin {
   public:
    const char* id() const override { return "keystep_fx"; }
    const char* name() const override { return "KeyStep FX"; }

    bool onStart() override {
        for (uint8_t i = 0; i < 16; i++) ksfxInCount[i] = 0;
        ksfxTxDropped = 0;
        ksfxSplit.reset();
        ksfxSplit.setChannelMask(kKsfxDefaultChannels);
        ksfxRunning = true;
        return true;
    }
};

static KeyStepFx keyStepFx;

Plugin* builtin_keyStepFx() {
    return &keyStepFx;
}

/* MIDI-In Split Hook (UART RX interrupt) ---------------------------*/
// Keep this short: it runs for every received byte.

bool pluginHost_midiRx(uint8_t byte) {
    if (!ksfxRunning) return false;

    MidiMessage msg;
    MidiSplitResult result = ksfxSplit.feed(byte, msg);
    if (result != MIDI_SPLIT_COMPLETE) return result != MIDI_SPLIT_PASS;

    ksfxInCount[msg.data[0] & 0x0F]++;
    if (!controller.enqueueMidiTx(msg.data, msg.len)) ksfxTxDropped++;
    return true;
}

#endif
