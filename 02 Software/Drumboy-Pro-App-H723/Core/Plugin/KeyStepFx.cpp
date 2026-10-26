#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Controller.h"
#include "MidiMerge.h"
#include "MidiSplit.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kKsfxDefaultChannels                 = 0x001F;           // MIDI channels 1-5 (settings page: DRUMB-53)
const uint16_t kKsfxNoteRate                        = 500;              // Plugin note-ons per second (DIN is ~1000 msgs/s)
const uint16_t kKsfxNoteBurst                       = 64;

// clang-format on

extern Controller controller;                                           // main.cpp

/* KeyStep FX -------------------------------------------------------*/
// MIDI processor for an external sequencer (KeyStep Pro) wired into the
// Drumboy's MIDI In. Its channels are taken out of the stock MIDI stream in the
// UART RX interrupt (PLUGIN_MIDI_RX), processed, and merged into MIDI Out
// (ksfxSend). The Drumboy's own messages on those channels are moved to a free
// channel (PLUGIN_MIDI_TX_DROP); its saved MIDI-OUT channel is never changed.
//
// Phase 1: split, pass-through and merge. Effects come in later phases.

static MidiSplit ksfxSplit;
static MidiRemap ksfxRemap;
static NoteRateLimiter ksfxLimiter;
static volatile bool ksfxRunning = false;
static volatile bool ksfxSending = false;                               // Plugin output bypasses the remap filter
static volatile uint32_t ksfxInCount[16];                               // Complete messages per channel
static volatile uint32_t ksfxTxDropped = 0;                             // TX ring full
static volatile uint32_t ksfxRateDropped = 0;                           // Note-ons over the rate cap
static volatile uint32_t ksfxRemapped = 0;                              // Drumboy messages moved off owned channels

class KeyStepFx : public Plugin {
   public:
    const char* id() const override { return "keystep_fx"; }
    const char* name() const override { return "KeyStep FX"; }

    bool onStart() override {
        for (uint8_t i = 0; i < 16; i++) ksfxInCount[i] = 0;
        ksfxTxDropped = 0;
        ksfxRateDropped = 0;
        ksfxRemapped = 0;
        ksfxSplit.reset();
        ksfxSplit.setChannelMask(kKsfxDefaultChannels);
        ksfxRemap.setOwnedMask(kKsfxDefaultChannels);
        ksfxLimiter.configure(kKsfxNoteRate, kKsfxNoteBurst);
        ksfxRunning = true;
        return true;
    }
};

static KeyStepFx keyStepFx;

Plugin* builtin_keyStepFx() {
    return &keyStepFx;
}

/* Output -----------------------------------------------------------*/
// Safe from any context. Note-ons over the rate cap are dropped; note-offs never.

static void ksfxSend(const uint8_t* data, uint8_t len) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    if (midiIsNoteOn(data, len) && !ksfxLimiter.allow(HAL_GetTick())) {
        __set_PRIMASK(primask);
        ksfxRateDropped++;
        return;
    }

    ksfxSending = true;
    bool queued = controller.enqueueMidiTx(data, len);
    ksfxSending = false;
    __set_PRIMASK(primask);

    if (!queued) ksfxTxDropped++;
}

/* MIDI-In Split Hook (UART RX interrupt) ---------------------------*/
// Keep this short: it runs for every received byte.

bool pluginHost_midiRx(uint8_t byte) {
    if (!ksfxRunning) return false;

    MidiMessage msg;
    MidiSplitResult result = ksfxSplit.feed(byte, msg);
    if (result != MIDI_SPLIT_COMPLETE) return result != MIDI_SPLIT_PASS;

    ksfxInCount[msg.data[0] & 0x0F]++;
    ksfxSend(msg.data, msg.len);
    return true;
}

/* MIDI-Out Filter Hook (inside enqueueMidiTx, interrupts off) ------*/

bool pluginHost_midiTx(const uint8_t*& data, uint8_t len) {
    if (!ksfxRunning || ksfxSending) return false;

    static uint8_t remapped[8];
    const uint8_t* original = data;
    bool drop = ksfxRemap.filter(data, len, remapped);
    if (drop || (data != original)) ksfxRemapped++;
    return drop;
}

#endif
