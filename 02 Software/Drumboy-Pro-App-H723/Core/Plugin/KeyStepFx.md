# KeyStep FX (`keystep_fx`)

MIDI processor for an external sequencer, such as an Arturia KeyStep Pro, wired into the Drumboy's MIDI In. The plugin takes the sequencer's channels out of the incoming MIDI stream, processes them, and merges them into the Drumboy's MIDI Out. The Drumboy's own song, MIDI and sync keep working as before.

Epic: DRUMB-49. Source: `KeyStepFx.cpp` (plugin + hooks), `MidiSplit.h` (input parser) and `MidiMerge.h` (channel remap, rate limiter); both headers are pure logic and host-testable.

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
| Clock-locked scheduler | DRUMB-52 | To do |
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

## Known limits (until the remaining phase 1 tickets land)

- The owned channels are fixed at 1–5 (the settings page is DRUMB-53).
- The remap target is always AUTO (channel 16) until the page lets you pick one.
- The counters are kept but not shown yet.
