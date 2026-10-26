# Hook Monitor (`hook_monitor`)

Source: `HookMonitor.cpp`. Enable: `hook_monitor = on`.

Developer tool: shows the latest value of every plugin hook on one line in the top strip (y = 16), so each hook can be checked on the device.

```
STEP   12  NOTE L01 V127 #  345  KEY P1 K13 DN  ENC 2 +1   +14  MIDI IN 90 #  12 OUT 80 24 00  DROP 0
```

| Field | Meaning |
|---|---|
| STEP | Current 16th step (from `PLUGIN_EVENT_STEP`) |
| NOTE L / V / # | Last triggered layer (1–10), its velocity, total notes |
| KEY P / K / DN-UP | Keypad (0 layer, 1 left, 2 right), key index, press or release |
| ENC n / delta / sum | Last encoder, its last step and running total |
| MIDI IN | Last received byte (clock and active sensing filtered) and count |
| MIDI OUT | Last sent message (clock filtered) |
| DROP | Events lost because the queue was full — should stay 0 |

Key indices: see [docs/README.md → Key names](../../../../docs/README.md#key-names).

Status: verified on device for step, note, keys and encoders; MIDI not yet tested.

Use it as a template: it shows every event type and how to draw a line only when it changed.
