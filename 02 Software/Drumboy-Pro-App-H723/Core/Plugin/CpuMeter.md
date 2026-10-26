# CPU Meter (`cpu_meter`)

Source: `CpuMeter.cpp` (uses `Core/Library/Global/Profiler.*`). Enable: `cpu_meter = on`.

Shows the processor load in the top-left strip (y = 6), updated twice a second:

```
AUDIO AVG  29.9% PEAK  32.2% BLK  725US   LOOP AVG    410US MAX   9120US   2400HZ
```

| Field | Meaning |
|---|---|
| AUDIO AVG | Average time spent in the audio interrupt, as % of the audio block period |
| AUDIO PEAK | Worst block since the last update. The line turns **red** above 80% — that's where clicks start |
| BLK | Audio block period in µs |
| LOOP AVG / MAX | Main-loop pass time (UI, LCD, SD, plugins) |
| HZ | Main-loop passes per second |

Measured on device (1.01-cy.1): 29.9% average / 32.2% peak with all 10 tracks and effects playing — about two thirds of the audio budget is free.

How it works: `PROFILE_AUDIO_BEGIN/END` around the I2S callbacks in `main.cpp` record cycle counts (DWT); `profiler_snapshot()` returns and resets the min/max/average.

Cost: a few cycles per audio block; the meter itself draws one text line every 500 ms.
