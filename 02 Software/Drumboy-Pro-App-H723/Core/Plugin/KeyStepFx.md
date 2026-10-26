# KeyStep FX (`keystep_fx`)

MIDI processor for an external sequencer, such as an Arturia KeyStep Pro, wired into the Drumboy's MIDI In. The plugin takes the sequencer's channels out of the incoming MIDI stream, processes them, and merges them into the Drumboy's MIDI Out. The Drumboy's own song, MIDI and sync keep working as before.

Epic: DRUMB-49. Source: `KeyStepFx.cpp` (plugin + hook), `MidiSplit.h` (parser, host-testable).

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
| MIDI-out merge, Drumboy channel remap, rate cap | DRUMB-51 | To do |
| Clock-locked scheduler | DRUMB-52 | To do |
| Settings page (channels, LEARN, Drumboy out channel) | DRUMB-53 | To do |

## How the split works

`PLUGIN_MIDI_RX(byte)` runs at the start of `Controller::receiveMidiCommand()` in the UART RX interrupt, before the stock MIDI-IN mode check, so it works whatever that mode is.

- Channel messages on owned channels are consumed, including running-status data bytes. Complete messages go to the TX ring (`Controller::enqueueMidiTx`).
- Bytes of all other channels, real-time bytes (clock, start/stop) and SysEx go to the stock parser unchanged.
- Notes on owned channels can never trigger Drumboy voices, even when MIDI-IN is set to `NOTE` on one of those channels.

Plugin off (default) = stock behaviour. The hook compiles to `(false)` in the `Stock` build, which stays byte-identical.

## Known limits (until the remaining phase 1 tickets land)

- The owned channels are fixed at 1–5 (the settings page is DRUMB-53).
- If the Drumboy's MIDI-OUT channel is one of 1–5, both streams share it (the remap is DRUMB-51).
- The TX ring is still 32 messages; overflows are counted but not shown yet.
