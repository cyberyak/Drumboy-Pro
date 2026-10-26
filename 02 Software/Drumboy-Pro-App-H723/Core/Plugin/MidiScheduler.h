#ifndef __MIDI_SCHEDULER_H
#define __MIDI_SCHEDULER_H

#include <stdint.h>

/* MIDI Scheduler ---------------------------------------------------*/
// Pure logic for sending MIDI in the future, locked to a MIDI clock. Times are
// microseconds on a free-running 32-bit counter (wraps after ~71 minutes;
// compares are wrap-safe within ±35 minutes). Runs in interrupts and in host
// tests; the caller provides the time and the critical sections.

// Clock positions are in 1/256 of a MIDI clock tick (24 PPQN):
// quarter = 6144, 1/16 = 1536, 1/32 = 768, 1/64 = 384.
const uint32_t kMidiSubticks = 256;

static inline bool midiTimeBefore(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

/* Event Queue ------------------------------------------------------*/
// Fixed-size, time-ordered. Stored latest first so the earliest event pops
// from the end; events with the same time pop in the order they were pushed.

struct MidiScheduledEvent {
    uint32_t timeUs;
    uint8_t data[3];
    uint8_t len;
};

template <uint8_t N>
class MidiEventQueue {
   public:
    void clear() { count = 0; }
    bool isEmpty() const { return count == 0; }
    uint8_t size() const { return count; }
    uint32_t nextTime() const { return events[count - 1].timeUs; }     // Only when not empty

    // False = full (or a bad length); the event is not queued.
    bool push(uint32_t timeUs, const uint8_t* data, uint8_t len) {
        if ((count >= N) || (len == 0) || (len > 3)) return false;
        uint8_t i = count;
        while ((i > 0) && !midiTimeBefore(timeUs, events[i - 1].timeUs)) {
            events[i] = events[i - 1];
            i--;
        }
        events[i].timeUs = timeUs;
        for (uint8_t b = 0; b < len; b++) events[i].data[b] = data[b];
        events[i].len = len;
        count++;
        return true;
    }

    // Pops the earliest event if it is due at `nowUs`.
    bool popDue(uint32_t nowUs, MidiScheduledEvent& out) {
        if ((count == 0) || midiTimeBefore(nowUs, events[count - 1].timeUs)) return false;
        out = events[--count];
        return true;
    }

   private:
    MidiScheduledEvent events[N];
    uint8_t count = 0;
};

/* Held Notes -------------------------------------------------------*/
// Which notes are sounding per channel, from the messages actually sent.
// Used to send note-offs on stop/reset (stuck-note guard).

class MidiHeldNotes {
   public:
    void clear() {
        for (uint8_t ch = 0; ch < 16; ch++)
            for (uint8_t w = 0; w < 4; w++) bits[ch][w] = 0;
    }

    void track(const uint8_t* data, uint8_t len) {
        if (len != 3) return;
        uint8_t type = data[0] & 0xF0;
        if ((type != 0x80) && (type != 0x90)) return;
        uint8_t ch = data[0] & 0x0F;
        uint8_t note = data[1] & 0x7F;
        uint32_t bit = 1UL << (note & 31);
        if ((type == 0x90) && (data[2] > 0)) bits[ch][note >> 5] |= bit;
        else bits[ch][note >> 5] &= ~bit;
    }

    bool isHeld(uint8_t channel, uint8_t note) const { return (bits[channel & 0x0F][(note >> 5) & 3] >> (note & 31)) & 1; }

    // Takes one held note out of the set; false when none are left.
    bool popHeld(uint8_t& channel, uint8_t& note) {
        for (uint8_t ch = 0; ch < 16; ch++) {
            for (uint8_t w = 0; w < 4; w++) {
                if (bits[ch][w] == 0) continue;
                uint8_t b = (uint8_t)__builtin_ctz(bits[ch][w]);
                bits[ch][w] &= ~(1UL << b);
                channel = ch;
                note = (uint8_t)((w << 5) | b);
                return true;
            }
        }
        return false;
    }

   private:
    uint32_t bits[16][4] = {};
};

/* Clock Tracker ----------------------------------------------------*/
// Turns incoming MIDI clock bytes into a position that can be read at any time
// and converted to a send time. Between ticks the position is interpolated
// from the measured period (average over the last beat), so divisions finer
// than 1/24 work. With no external clock (never received, or none for 3
// periods) it runs on the fallback period, normally the Drumboy's own tempo.

class MidiClockTracker {
   public:
    static const uint8_t kWindow = 24;                                  // Ticks averaged for the period
    static const uint32_t kStaleMinUs = 250000;

    void reset() {
        haveTick = false;
        startPending = false;
        samples = 0;
    }

    // MIDI Start: the next clock tick is position 0.
    void start() { startPending = true; }

    // A MIDI clock byte (0xF8) arrived at `nowUs`.
    void tick(uint32_t nowUs, uint32_t fallbackPeriodUs) {
        bool fresh = isExternal(nowUs);
        uint32_t position;
        if (startPending) {
            position = 0;
            startPending = false;
        } else if (fresh) {
            position = anchorPos + kMidiSubticks;
        } else {
            // Clock (re)appears: continue from the extrapolated position, on a tick boundary.
            advance(nowUs, fallbackPeriodUs);
            position = ((anchorPos + kMidiSubticks / 2) / kMidiSubticks) * kMidiSubticks;
        }
        if (!fresh) samples = 0;                                        // New measurement window after a gap

        history[head] = nowUs;
        head = (uint8_t)((head + 1) % (kWindow + 1));
        if (samples <= kWindow) samples++;
        if (samples >= 2) {
            uint8_t oldest = (uint8_t)((head + (kWindow + 1) - samples) % (kWindow + 1));
            uint32_t measured = (nowUs - history[oldest]) / (samples - 1);
            if (measured > 0) period = measured;
        } else if (!haveTick || !fresh) {
            period = fallbackPeriodUs;
        }

        anchorPos = position;
        anchorUs = nowUs;
        lastTickUs = nowUs;
        haveTick = true;
    }

    // True while external clock ticks keep arriving: within 3 periods, and at
    // least 250 ms so a slow clock (30 BPM = 83 ms/tick) is caught on its 2nd tick.
    bool isExternal(uint32_t nowUs) const {
        uint32_t limit = (3 * period > kStaleMinUs) ? 3 * period : kStaleMinUs;
        return haveTick && ((nowUs - lastTickUs) <= limit);
    }

    uint32_t periodUs() const { return period; }

    // Without external clock, moves the anchor to `nowUs` and switches to the
    // fallback period (so internal tempo changes apply from now on). Call before
    // reading positions; harmless with external clock.
    void advance(uint32_t nowUs, uint32_t fallbackPeriodUs) {
        if (isExternal(nowUs)) return;
        anchorPos = positionAt(nowUs);
        anchorUs = nowUs;
        period = (fallbackPeriodUs > 0) ? fallbackPeriodUs : 1;
    }

    // Position at `nowUs`. With external clock it never runs past the next
    // tick before that tick arrives, so it stays monotonic.
    uint32_t positionAt(uint32_t nowUs) const {
        uint64_t sub = ((uint64_t)(nowUs - anchorUs) * kMidiSubticks) / period;
        if (isExternal(nowUs) && (sub >= kMidiSubticks)) sub = kMidiSubticks - 1;
        return anchorPos + (uint32_t)sub;
    }

    // Send time of a position (past positions give past times = send now).
    uint32_t timeOf(uint32_t position) const {
        int64_t delta = ((int64_t)(int32_t)(position - anchorPos) * period) / (int64_t)kMidiSubticks;
        return anchorUs + (uint32_t)delta;
    }

   private:
    uint32_t history[kWindow + 1] = {};
    uint8_t head = 0;
    uint8_t samples = 0;
    bool haveTick = false;
    bool startPending = false;
    uint32_t lastTickUs = 0;
    uint32_t anchorUs = 0;
    uint32_t anchorPos = 0;
    uint32_t period = 20833;                                            // 120 BPM until told otherwise
};

#endif
