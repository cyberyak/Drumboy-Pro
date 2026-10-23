#ifndef __PROFILER_H
#define __PROFILER_H

#include <stdint.h>

// clang-format off

/* CPU Load Measurement ---------------------------------------------*/
// Measures audio callback load against the I2S block period and main loop
// iteration time with the DWT cycle counter (a few dozen cycles per audio
// block). Compiled with the plugin host (-DDRUMBOY_PLUGINS); the CPU Meter
// plugin displays the result. Compiles to nothing when disabled.

struct ProfilerStats {
    uint32_t audioAvgPermille;                                  // Average audio load, per mille of the block period
    uint32_t audioPeakPermille;                                 // Worst single block
    uint32_t blockUs;                                           // Measured I2S block period
    uint32_t loopAvgUs;                                         // Main loop iteration time (includes interrupts)
    uint32_t loopMaxUs;
    uint32_t loopHz;                                            // Main loop iterations per second
};

#ifdef DRUMBOY_PLUGINS

void profiler_audioBegin();
void profiler_audioEnd();
void profiler_loopBegin();
void profiler_loopEnd();
void profiler_snapshot(ProfilerStats& stats);                   // Returns stats since the last snapshot and resets them

#define PROFILE_AUDIO_BEGIN()                       profiler_audioBegin()
#define PROFILE_AUDIO_END()                         profiler_audioEnd()
#define PROFILE_LOOP_BEGIN()                        profiler_loopBegin()
#define PROFILE_LOOP_END()                          profiler_loopEnd()

#else

#define PROFILE_AUDIO_BEGIN()                       ((void)0)
#define PROFILE_AUDIO_END()                         ((void)0)
#define PROFILE_LOOP_BEGIN()                        ((void)0)
#define PROFILE_LOOP_END()                          ((void)0)

#endif

// clang-format on

#endif
