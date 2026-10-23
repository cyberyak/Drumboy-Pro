#include "Profiler.h"

#ifdef DRUMBOY_PLUGINS

#include "Global.h"

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
static uint32_t lastSnapshotTick = 0;

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

void profiler_snapshot(ProfilerStats& stats) {
    /// @brief Derives the statistics accumulated since the previous snapshot, then resets them.

    // 1. SNAPSHOT + RESET AUDIO STATISTICS
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

    // 2. DERIVE VALUES
    uint32_t tick = HAL_GetTick();
    uint32_t elapsedMs = tick - lastSnapshotTick;
    lastSnapshotTick = tick;

    uint32_t cyclesPerUs = SystemCoreClock / 1000000;
    stats.audioAvgPermille = (periodSum > 0) ? (uint32_t)((busySum * 1000) / periodSum) : 0;
    stats.audioPeakPermille = peakPermille;
    stats.blockUs = (blockCount > 0) ? (uint32_t)(periodSum / blockCount / cyclesPerUs) : 0;
    stats.loopAvgUs = (loopCount > 0) ? (uint32_t)(loopBusySum / loopCount / cyclesPerUs) : 0;
    stats.loopMaxUs = loopMax / cyclesPerUs;
    stats.loopHz = (elapsedMs > 0) ? (loopCount * 1000) / elapsedMs : 0;

    // 3. RESET LOOP STATISTICS
    loopBusySum = 0;
    loopMax = 0;
    loopCount = 0;
}

#endif
