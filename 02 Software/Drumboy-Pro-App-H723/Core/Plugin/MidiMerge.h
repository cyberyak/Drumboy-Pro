#ifndef __MIDI_MERGE_H
#define __MIDI_MERGE_H

#include <stdint.h>

/* MIDI Out Merge ---------------------------------------------------*/
// Pure logic for merging a plugin's MIDI stream with the Drumboy's own MIDI
// Out. Runs in interrupts and in host tests.

/* Drumboy Channel Remap --------------------------------------------*/
// The plugin's channels always win. A Drumboy channel message on one of them
// is moved to the target channel: AUTO = the highest channel the plugin does
// not own, MANUAL = a chosen channel (falls back to AUTO if the plugin owns
// it). System messages (clock, transport, SysEx) are never touched.

const uint8_t kMidiRemapNone = 0xFF;                                    // Every channel owned: drop

class MidiRemap {
   public:
    void setOwnedMask(uint16_t mask_) { mask = mask_; update(); }       // Bit n = MIDI channel n+1
    void setManual(uint8_t channel_) { manual = channel_; update(); }   // 0-15, or kMidiRemapNone = AUTO
    uint8_t getTarget() const { return target; }                        // 0-15, or kMidiRemapNone
    bool isAuto() const { return manual == kMidiRemapNone; }

    // Returns true to drop. Otherwise `data` points at the message to send:
    // unchanged, or a remapped copy in `buffer` (len bytes, at most 8).
    bool filter(const uint8_t*& data, uint8_t len, uint8_t* buffer) const {
        uint8_t status = data[0];
        if ((status < 0x80) || (status >= 0xF0)) return false;
        if (!((mask >> (status & 0x0F)) & 1)) return false;
        if (target == kMidiRemapNone) return true;

        for (uint8_t i = 0; i < len; i++) buffer[i] = data[i];
        buffer[0] = (uint8_t)((status & 0xF0) | target);
        data = buffer;
        return false;
    }

   private:
    void update() {
        if ((manual < 16) && !((mask >> manual) & 1)) {
            target = manual;
            return;
        }
        target = kMidiRemapNone;
        for (int8_t ch = 15; ch >= 0; ch--) {
            if (!((mask >> ch) & 1)) {
                target = (uint8_t)ch;
                return;
            }
        }
    }

    uint16_t mask = 0;
    uint8_t manual = kMidiRemapNone;
    uint8_t target = 15;
};

/* Note Rate Limiter ------------------------------------------------*/
// Token bucket for note-ons the plugin generates, so ratchets and repeats
// can't flood the DIN port (31250 baud = about 1000 3-byte messages/s, shared
// with the Drumboy). Note-offs and other messages are never limited.

class NoteRateLimiter {
   public:
    void configure(uint16_t notesPerSecond_, uint16_t burst_) {
        rate = notesPerSecond_;
        burstMilli = (uint32_t)burst_ * 1000;
        tokensMilli = burstMilli;
    }

    bool allow(uint32_t nowMs) {
        uint32_t elapsed = nowMs - lastMs;
        lastMs = nowMs;
        uint32_t refill = (elapsed > 60000) ? burstMilli : elapsed * rate;
        tokensMilli = (tokensMilli + refill > burstMilli) ? burstMilli : tokensMilli + refill;
        if (tokensMilli < 1000) return false;
        tokensMilli -= 1000;
        return true;
    }

   private:
    uint16_t rate = 500;
    uint32_t burstMilli = 64000;
    uint32_t tokensMilli = 64000;
    uint32_t lastMs = 0;
};

static inline bool midiIsNoteOn(const uint8_t* data, uint8_t len) {
    return (len == 3) && ((data[0] & 0xF0) == 0x90) && (data[2] > 0);
}

#endif
