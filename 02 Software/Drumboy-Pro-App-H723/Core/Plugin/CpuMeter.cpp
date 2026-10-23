#include "Plugin.h"

#ifdef DRUMBOY_PLUGINS

#include "Lcd.h"
#include "Profiler.h"

// clang-format off

const uint32_t kCpuMeterInterval                    = 500;              // ms between updates
const uint16_t kCpuMeterTextX                       = 20;               // Top-left strip, empty in all stock layouts
const uint16_t kCpuMeterTextY                       = 6;
const uint16_t kCpuMeterTextSize                    = 96;
const uint16_t kCpuMeterWarnPermille                = 800;              // Peak above 80% is drawn in red

// clang-format on

/* CPU Meter --------------------------------------------------------*/
// Shows audio load (average and worst block) and main loop timing.

class CpuMeter : public Plugin {
   public:
    const char* id() const override { return "cpu_meter"; }
    const char* name() const override { return "CPU Meter"; }

    void onFrame(Lcd& lcd) override {
        uint32_t tick = HAL_GetTick();
        if ((tick - lastTick) < kCpuMeterInterval) return;
        lastTick = tick;

        ProfilerStats s;
        profiler_snapshot(s);

        char text[kCpuMeterTextSize];
        int len = snprintf(text, sizeof(text), "AUDIO AVG %3lu.%lu%% PEAK %3lu.%lu%% BLK %4luUS   LOOP AVG %6luUS MAX %6luUS %6luHZ  ",
                           s.audioAvgPermille / 10, s.audioAvgPermille % 10, s.audioPeakPermille / 10, s.audioPeakPermille % 10,
                           s.blockUs, s.loopAvgUs, s.loopMaxUs, s.loopHz);
        if (len <= 0) return;
        if (len >= (int)sizeof(text)) len = sizeof(text) - 1;

        lcd.setFont(FONT_05x07);
        lcd.setAlignment(LEFT);
        lcd.setForeColor((s.audioPeakPermille > kCpuMeterWarnPermille) ? RED : GREEN);
        lcd.setBackColor(BLACK);
        lcd.drawText(text, len, kCpuMeterTextX, kCpuMeterTextY);
    }

   private:
    uint32_t lastTick = 0;
};

static CpuMeter cpuMeter;

Plugin* builtin_cpuMeter() {
    return &cpuMeter;
}

#endif
