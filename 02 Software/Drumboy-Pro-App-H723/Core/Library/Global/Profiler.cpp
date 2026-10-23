#include "Profiler.h"

#ifdef DRUMBOY_CPU_METER

#include "Lcd.h"

// clang-format off

/* Settings ---------------------------------------------------------*/

const uint32_t kProfilerDrawInterval                = 500;              // ms between screen updates
const uint16_t kProfilerTextX                       = 20;               // Top-left strip, empty in all stock layouts
const uint16_t kProfilerTextY                       = 6;
const uint16_t kProfilerTextSize                    = 112;

// clang-format on

#ifndef DRUMBOY_VERSION_MAJOR
#define DRUMBOY_VERSION_MAJOR 0
#define DRUMBOY_VERSION_MINOR 0
#endif

/* Audio Statistics (written in I2S interrupt) ----------------------*/

static volatile uint32_t audioStart = 0;
static volatile uint32_t audioLastStart = 0;
static volatile uint32_t audioPeriod = 0;
static volatile uint64_t audioBusySum = 0;
static volatile uint64_t audioPeriodSum = 0;
static volatile uint32_t audioPeakPermille = 0;
static volatile uint32_t audioBlockCount = 0;

/* Main Loop Statistics (main context only) -------------------------*/

static uint32_t loopStart = 0;
static uint64_t loopBusySum = 0;
static uint32_t loopMax = 0;
static uint32_t loopCount = 0;
static uint32_t lastDrawTick = 0;

void profiler_audioBegin() {
    /// @brief Marks the start of an audio block; the distance to the previous start is the block period.

    uint32_t now = DWT_CYCCNT;
    if (audioLastStart != 0) audioPeriod = now - audioLastStart;
    audioLastStart = now;
    audioStart = now;
}

void profiler_audioEnd() {
    /// @brief Accumulates audio block processing time and tracks the worst block load.

    // 1. SKIP UNTIL A PERIOD IS KNOWN
    uint32_t period = audioPeriod;
    if (period == 0) return;

    // 2. ACCUMULATE
    uint32_t busy = DWT_CYCCNT - audioStart;
    audioBusySum += busy;
    audioPeriodSum += period;
    audioBlockCount++;

    // 3. PEAK LOAD (per mille of block period)
    uint32_t permille = (uint32_t)(((uint64_t)busy * 1000) / period);
    if (permille > audioPeakPermille) audioPeakPermille = permille;
}

void profiler_loopBegin() {
    loopStart = DWT_CYCCNT;
}

void profiler_loopEnd() {
    /// @brief Accumulates main loop iteration time (includes time spent in interrupts).

    uint32_t busy = DWT_CYCCNT - loopStart;
    loopBusySum += busy;
    if (busy > loopMax) loopMax = busy;
    loopCount++;
}

void profiler_draw(Lcd& lcd) {
    /// @brief Prints the meter every kProfilerDrawInterval ms, preserving the LCD drawing state.

    // 1. RATE LIMIT
    uint32_t tick = HAL_GetTick();
    if ((tick - lastDrawTick) < kProfilerDrawInterval) return;
    lastDrawTick = tick;

    // 2. SNAPSHOT + RESET AUDIO STATISTICS
    __disable_irq();
    uint64_t busySum = audioBusySum;
    uint64_t periodSum = audioPeriodSum;
    uint32_t peakPermille = audioPeakPermille;
    uint32_t blockCount = audioBlockCount;
    audioBusySum = 0;
    audioPeriodSum = 0;
    audioPeakPermille = 0;
    audioBlockCount = 0;
    __enable_irq();

    // 3. DERIVE VALUES (integer math: nano printf has no float support)
    uint32_t cyclesPerUs = SystemCoreClock / 1000000;
    uint32_t avgPermille = (periodSum > 0) ? (uint32_t)((busySum * 1000) / periodSum) : 0;
    uint32_t blockUs = (blockCount > 0) ? (uint32_t)(periodSum / blockCount / cyclesPerUs) : 0;
    uint32_t loopAvgUs = (loopCount > 0) ? (uint32_t)(loopBusySum / loopCount / cyclesPerUs) : 0;
    uint32_t loopMaxUs = loopMax / cyclesPerUs;
    uint32_t loopHz = (loopCount * 1000) / kProfilerDrawInterval;

    loopBusySum = 0;
    loopMax = 0;
    loopCount = 0;

    char text[kProfilerTextSize];
    int len = snprintf(text, sizeof(text), "V%u.%02u  AUDIO AVG %3lu.%lu%% PEAK %3lu.%lu%% BLK %4luUS   LOOP AVG %6luUS MAX %6luUS %6luHZ  ",
                       DRUMBOY_VERSION_MAJOR, DRUMBOY_VERSION_MINOR,
                       avgPermille / 10, avgPermille % 10, peakPermille / 10, peakPermille % 10, blockUs,
                       loopAvgUs, loopMaxUs, loopHz);
    if (len <= 0) return;
    if (len >= (int)sizeof(text)) len = sizeof(text) - 1;

    // 4. DRAW WITH SAVED LCD STATE
    LcdFont font = lcd.getFont();
    uint16_t spacing = lcd.getSpacing();
    LcdAlign align = lcd.getAlignment();
    RGB16Color fore = lcd.getForeColor();
    RGB16Color back = lcd.getBackColor();

    lcd.setFont(FONT_05x07);
    lcd.setAlignment(LEFT);
    lcd.setForeColor((peakPermille > 800) ? RED : GREEN);
    lcd.setBackColor(BLACK);
    lcd.drawText(text, len, kProfilerTextX, kProfilerTextY);

    lcd.setFont(font);
    lcd.setSpacing(spacing);
    lcd.setAlignment(align);
    lcd.setForeColor(fore);
    lcd.setBackColor(back);
}

#endif
