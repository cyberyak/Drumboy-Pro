#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Controller.h"
#include "MidiMerge.h"
#include "MidiScheduler.h"
#include "MidiSplit.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint16_t kKsfxDefaultChannels                 = 0x001F;           // MIDI channels 1-5 (settings page: DRUMB-53)
const uint16_t kKsfxNoteRate                        = 500;              // Plugin note-ons per second (DIN is ~1000 msgs/s)
const uint16_t kKsfxNoteBurst                       = 64;
const uint8_t  kKsfxQueueSize                       = 64;               // Scheduled events waiting to be sent
const uint32_t kKsfxDebugEcho                       = 0;                // Device test only: subticks (1536 = 1/16) to echo owned notes an octave up; 0 = off

// clang-format on

extern Controller controller;                                           // main.cpp

/* KeyStep FX -------------------------------------------------------*/
// MIDI processor for an external sequencer (KeyStep Pro) wired into the
// Drumboy's MIDI In. Its channels are taken out of the stock MIDI stream in the
// UART RX interrupt (PLUGIN_MIDI_RX), processed, and merged into MIDI Out
// (ksfxSend). The Drumboy's own messages on those channels are moved to a free
// channel (PLUGIN_MIDI_TX_DROP); its saved MIDI-OUT channel is never changed.
//
// Future events go through a time-ordered queue sent from the TIM2 compare
// interrupt, locked to the incoming MIDI clock (ksfxScheduleAt). Without
// external clock it follows the Drumboy's own tempo.
//
// Phase 1: split, pass-through, merge and scheduler. Effects come in later phases.

static MidiSplit ksfxSplit;
static MidiRemap ksfxRemap;
static NoteRateLimiter ksfxLimiter;
static MidiEventQueue<kKsfxQueueSize> ksfxQueue;
static MidiHeldNotes ksfxHeld;                                          // Notes the plugin has sounding (stuck-note guard)
static MidiClockTracker ksfxClock;
static volatile bool ksfxRunning = false;
static volatile bool ksfxSending = false;                               // Plugin output bypasses the remap filter
static volatile uint32_t ksfxInCount[16];                               // Complete messages per channel
static volatile uint32_t ksfxTxDropped = 0;                             // TX ring full
static volatile uint32_t ksfxRateDropped = 0;                           // Note-ons over the rate cap
static volatile uint32_t ksfxRemapped = 0;                              // Drumboy messages moved off owned channels
static volatile uint32_t ksfxQueueDropped = 0;                          // Scheduler queue full

static void ksfxTimerStart();

class KeyStepFx : public Plugin {
   public:
    const char* id() const override { return "keystep_fx"; }
    const char* name() const override { return "KeyStep FX"; }

    bool onStart() override {
        for (uint8_t i = 0; i < 16; i++) ksfxInCount[i] = 0;
        ksfxTxDropped = 0;
        ksfxRateDropped = 0;
        ksfxRemapped = 0;
        ksfxQueueDropped = 0;
        ksfxQueue.clear();
        ksfxHeld.clear();
        ksfxClock.reset();
        ksfxSplit.reset();
        ksfxSplit.setChannelMask(kKsfxDefaultChannels);
        ksfxRemap.setOwnedMask(kKsfxDefaultChannels);
        ksfxLimiter.configure(kKsfxNoteRate, kKsfxNoteBurst);
        ksfxTimerStart();
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
    if (queued) ksfxHeld.track(data, len);
    __set_PRIMASK(primask);

    if (!queued) ksfxTxDropped++;
}

/* Scheduler --------------------------------------------------------*/
// TIM2 (32-bit, free) counts microseconds; its compare channel 1 fires at the
// earliest queued event. Set up here so the stock build never touches it.

static inline uint32_t ksfxNowUs() {
    return TIM2->CNT;
}

// The Drumboy's own 24 PPQN period in microseconds (TIM4 = MIDI clock out timer, 1 MHz).
static inline uint32_t ksfxInternalPeriodUs() {
    return TIM4->ARR + 1;
}

static void ksfxTimerStart() {
    __HAL_RCC_TIM2_CLK_ENABLE();
    TIM2->CR1 = 0;
    TIM2->PSC = TIM4->PSC;                                              // Same APB1 timer clock: 1 MHz
    TIM2->ARR = 0xFFFFFFFF;
    TIM2->CCMR1 = 0;                                                    // Channel 1: output compare, frozen (no pin)
    TIM2->DIER = 0;
    TIM2->EGR = TIM_EGR_UG;                                             // Load the prescaler
    TIM2->SR = 0;
    TIM2->CR1 = TIM_CR1_CEN;
    HAL_NVIC_SetPriority(TIM2_IRQn, 1, 0);                              // Same as the MIDI clock out timer
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

// Interrupts off. Points the compare at the earliest event, or stops it when the queue is empty.
static void ksfxArm() {
    if (ksfxQueue.isEmpty()) {
        TIM2->DIER &= ~TIM_DIER_CC1IE;
        return;
    }
    uint32_t next = ksfxQueue.nextTime();
    TIM2->CCR1 = next;
    TIM2->SR = ~TIM_SR_CC1IF;
    TIM2->DIER |= TIM_DIER_CC1IE;
    if (!midiTimeBefore(TIM2->CNT, next)) TIM2->EGR = TIM_EGR_CC1G;     // Already due: fire now
}

extern "C" void TIM2_IRQHandler(void) {
    TIM2->SR = ~TIM_SR_CC1IF;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    MidiScheduledEvent event;
    while (ksfxQueue.popDue(ksfxNowUs(), event)) ksfxSend(event.data, event.len);
    ksfxArm();
    __set_PRIMASK(primask);
}

// Current clock position (1/256 tick, see MidiScheduler.h). Safe from any context.
uint32_t ksfxPosition() {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t now = ksfxNowUs();
    ksfxClock.advance(now, ksfxInternalPeriodUs());
    uint32_t position = ksfxClock.positionAt(now);
    __set_PRIMASK(primask);
    return position;
}

// Queues a message to go out at a clock position (past positions go out now).
// False = queue full. Safe from any context.
bool ksfxScheduleAt(uint32_t position, const uint8_t* data, uint8_t len) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    ksfxClock.advance(ksfxNowUs(), ksfxInternalPeriodUs());
    bool queued = ksfxQueue.push(ksfxClock.timeOf(position), data, len);
    if (queued) ksfxArm();
    else ksfxQueueDropped++;
    __set_PRIMASK(primask);
    return queued;
}

// Stop/reset: drops queued events and sends note-offs for every note the plugin left sounding.
static void ksfxFlush() {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    ksfxQueue.clear();
    ksfxArm();
    uint8_t channel, note;
    while (ksfxHeld.popHeld(channel, note)) {
        uint8_t noteOff[3] = {(uint8_t)(0x80 | channel), note, 0};
        ksfxSend(noteOff, 3);
    }
    __set_PRIMASK(primask);
}

// Clock and transport from MIDI In (they also go on to the stock parser).
static void ksfxRealtime(uint8_t byte) {
    switch (byte) {
        case 0xF8: {
            uint32_t primask = __get_PRIMASK();
            __disable_irq();
            ksfxClock.tick(ksfxNowUs(), ksfxInternalPeriodUs());
            __set_PRIMASK(primask);
            break;
        }
        case 0xFA:                                                      // Start: next tick is position 0
            ksfxFlush();
            ksfxClock.start();
            break;
        case 0xFC:                                                      // Stop
        case 0xFF:                                                      // Reset
            ksfxFlush();
            break;
    }
}

// Device test of the scheduler (kKsfxDebugEcho): repeats each owned note an octave up, later.
static void ksfxDebugEcho(const MidiMessage& msg) {
    uint8_t type = msg.data[0] & 0xF0;
    if ((msg.len != 3) || ((type != 0x80) && (type != 0x90)) || (msg.data[1] > 115)) return;
    uint8_t echo[3] = {msg.data[0], (uint8_t)(msg.data[1] + 12), msg.data[2]};
    ksfxScheduleAt(ksfxPosition() + kKsfxDebugEcho, echo, 3);
}

/* MIDI-In Split Hook (UART RX interrupt) ---------------------------*/
// Keep this short: it runs for every received byte.

bool pluginHost_midiRx(uint8_t byte) {
    if (!ksfxRunning) return false;
    if (byte >= 0xF8) ksfxRealtime(byte);

    MidiMessage msg;
    MidiSplitResult result = ksfxSplit.feed(byte, msg);
    if (result != MIDI_SPLIT_COMPLETE) return result != MIDI_SPLIT_PASS;

    ksfxInCount[msg.data[0] & 0x0F]++;
    ksfxSend(msg.data, msg.len);
    if (kKsfxDebugEcho) ksfxDebugEcho(msg);
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
