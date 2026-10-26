#ifndef __MIDI_SPLIT_H
#define __MIDI_SPLIT_H

#include <stdint.h>

/* MIDI Split Parser ------------------------------------------------*/
// Byte-by-byte parser that decides, for every incoming MIDI byte, whether it
// belongs to a channel the plugin owns (consumed) or goes on to the stock
// parser (passed). Pure logic, no hardware: runs in the UART RX interrupt and
// in host tests.
//
// - Channel messages (0x80-0xEF) on owned channels are consumed, including
//   running-status data bytes. Complete messages are returned to the caller.
// - Real-time bytes (0xF8-0xFF) always pass and don't disturb a message.
// - System common and SysEx (0xF0-0xF7) always pass and clear running status.

struct MidiMessage {
    uint8_t data[3];
    uint8_t len;
};

enum MidiSplitResult : uint8_t {
    MIDI_SPLIT_PASS     = 0,                                            // Stock parser gets the byte
    MIDI_SPLIT_CONSUMED = 1,                                            // Owned, message not complete yet
    MIDI_SPLIT_COMPLETE = 2                                             // Owned, message complete in `out`
};

class MidiSplit {
   public:
    void setChannelMask(uint16_t mask_) { mask = mask_; }               // Bit n = MIDI channel n+1
    uint16_t getChannelMask() const { return mask; }
    void reset() { status = 0; count = 0; owned = false; }

    MidiSplitResult feed(uint8_t byte, MidiMessage& out) {
        // 1. REAL-TIME: may appear anywhere, never part of a message
        if (byte >= 0xF8) return MIDI_SPLIT_PASS;

        // 2. SYSTEM COMMON / SYSEX: clears running status
        if (byte >= 0xF0) {
            reset();
            return MIDI_SPLIT_PASS;
        }

        // 3. CHANNEL STATUS
        if (byte & 0x80) {
            status = byte;
            count = 0;
            needed = ((byte & 0xF0) == 0xC0 || (byte & 0xF0) == 0xD0) ? 1 : 2;
            owned = (mask >> (byte & 0x0F)) & 1;
            return owned ? MIDI_SPLIT_CONSUMED : MIDI_SPLIT_PASS;
        }

        // 4. DATA BYTE (running status keeps the last channel status)
        if (!owned || (status == 0)) return MIDI_SPLIT_PASS;
        data[count++] = byte;
        if (count < needed) return MIDI_SPLIT_CONSUMED;

        out.data[0] = status;
        out.data[1] = data[0];
        out.data[2] = (needed == 2) ? data[1] : 0;
        out.len = needed + 1;
        count = 0;
        return MIDI_SPLIT_COMPLETE;
    }

   private:
    volatile uint16_t mask = 0;
    uint8_t status = 0;
    uint8_t needed = 2;
    uint8_t count = 0;
    uint8_t data[2] = {};
    bool owned = false;
};

#endif
