# KeyStep FX (`keystep_fx`)

MIDI processor for an external sequencer, such as an Arturia KeyStep Pro, wired into the Drumboy's MIDI In. The plugin takes the sequencer's channels out of the incoming MIDI stream, processes them, and merges them into the Drumboy's MIDI Out. The Drumboy's own song, MIDI and sync keep working as before.

Epic: DRUMB-49. Source: `KeyStepFx.cpp` (plugin, hooks, scheduler timer), `MidiSplit.h` (input parser), `MidiMerge.h` (channel remap, rate limiter) and `MidiScheduler.h` (event queue, clock tracker, held notes); the headers are pure logic and host-testable.

## Wiring

```
KeyStep Pro MIDI Out ──► Drumboy MIDI In ──► KeyStep FX ──► Drumboy MIDI Out ──► MIDI hub ──► synths
```

- The KeyStep Pro is the clock master. Set the Drumboy's **MIDI-IN** to `SYNC` so it follows, and **MIDI-OUT** to `NOTE+SYNC` so the hub gets one clock.
- The plugin never forwards the KeyStep Pro's clock or transport; the Drumboy re-sends its own.

## Status (phase 1, in progress)

| Part | Ticket | State |
|---|---|---|
| MIDI-in split + parser | DRUMB-50 | Done: channels **1–5** are owned and passed straight to MIDI Out |
| MIDI-out merge, Drumboy channel remap, rate cap | DRUMB-51 | Done: 128-message TX ring, AUTO remap to the highest free channel, 500 note-ons/s cap |
| Clock-locked scheduler | DRUMB-52 | Done: TIM2 event queue locked to MIDI clock, 1/256-tick positions, stuck-note guard on Start/Stop/Reset |
| Settings page (channels, LEARN, Drumboy out channel) | DRUMB-53 | To do |

## How the split works

`PLUGIN_MIDI_RX(byte)` runs at the start of `Controller::receiveMidiCommand()` in the UART RX interrupt, before the stock MIDI-IN mode check, so it works whatever that mode is.

- Channel messages on owned channels are consumed, including running-status data bytes. Complete messages go to the TX ring (`Controller::enqueueMidiTx`).
- Bytes of all other channels, real-time bytes (clock, start/stop) and SysEx go to the stock parser unchanged.
- Notes on owned channels can never trigger Drumboy voices, even when MIDI-IN is set to `NOTE` on one of those channels.

Plugin off (default) = stock behaviour. The hook compiles to `(false)` in the `Stock` build, which stays byte-identical.

## How the merge works

Both streams share the TX ring (`Controller::enqueueMidiTx`), which holds 128 messages in plugin builds (32 in `Stock`).

- **Drumboy messages:** `PLUGIN_MIDI_TX_DROP(data, len)` runs inside `enqueueMidiTx`'s critical section. A Drumboy channel message on an owned channel is moved to the **target channel**: AUTO = the highest channel the plugin doesn't own (16 with the default 1–5), MANUAL = a chosen channel (DRUMB-53). The saved MIDI-OUT channel is never changed, so switching the plugin off restores it. Clock, transport and SysEx are never touched. If all 16 channels are owned, Drumboy channel messages are dropped.
- **Plugin messages** go out through `ksfxSend()`, which bypasses the filter. Note-ons the plugin sends are capped at 500 per second (burst 64) so later effects can't flood the DIN port, which carries about 1,000 messages per second. Note-offs and other messages are never capped.
- The plugin sends even when the Drumboy's MIDI-OUT mode is `OFF`: the UART transmitter is always on, and only the Drumboy's own clock and notes are gated by that mode.
- Counters (shown on the page in DRUMB-53): messages in per channel, TX ring full, notes over the rate cap, Drumboy messages remapped.

## How the scheduler works

Effects (phase 2: ratchets, swing, repeats) send future MIDI through a time-ordered queue of 64 events instead of the main loop.

- **Time base:** TIM2, a 32-bit timer the stock firmware doesn't use, counts microseconds. Its compare channel fires at the earliest queued event and `TIM2_IRQHandler` sends everything that is due through `ksfxSend()` (so the rate cap and held-note tracking apply). It is set up in `onStart`, so the `Stock` build never touches it.
- **Positions:** `ksfxPosition()` returns the clock position in 1/256 of a MIDI clock tick (quarter = 6144, 1/16 = 1536, 1/64 = 384). `ksfxScheduleAt(position, data, len)` queues a message for a position; past positions go out at once. Both are safe from any context.
- **Clock lock:** every MIDI clock byte from MIDI In is timestamped. The period is the average over the last beat (24 ticks), and positions between ticks are interpolated from it, so divisions finer than 1/24 work. The position never runs past the next tick before that tick arrives. MIDI Start makes the next tick position 0; Continue keeps counting.
- **No external clock** (never received, or none for 3 periods and at least 250 ms): positions follow the Drumboy's own tempo (the MIDI clock out timer, TIM4). When the clock comes back, counting continues from the nearest tick.
- **Stuck-note guard:** the plugin tracks every note it has sounding (pass-through and scheduled). MIDI Start, Stop and System Reset clear the queue and send a note-off for each of them.
- Counter: scheduler queue full (shown on the page in DRUMB-53).

**Device test** (until effects exist): set `kKsfxDebugEcho` in `KeyStepFx.cpp` to `1536` and build. Every note on channels 1–5 is repeated a 1/16 later, an octave up, and should stay locked when the KeyStep Pro's tempo changes. Set it back to `0` afterwards.

## Known limits (until the remaining phase 1 tickets land)

- The owned channels are fixed at 1–5 (the settings page is DRUMB-53).
- The remap target is always AUTO (channel 16) until the page lets you pick one.
- The counters are kept but not shown yet.
- Nothing schedules events yet except the debug echo; the effects come in phase 2.
