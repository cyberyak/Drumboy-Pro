#ifndef __PROFILER_H
#define __PROFILER_H

#include <stdint.h>

// clang-format off

/* CPU Meter --------------------------------------------------------*/
// Development tool, enabled with -DDRUMBOY_CPU_METER (CMake preset "CpuMeter").
// Measures audio callback load against the I2S block period and main loop
// iteration time using the DWT cycle counter, and prints the result in the
// unused strip at the top of the screen. Compiles to nothing when disabled.

#ifdef DRUMBOY_CPU_METER

class Lcd;

void profiler_audioBegin();
void profiler_audioEnd();
void profiler_loopBegin();
void profiler_loopEnd();
void profiler_draw(Lcd& lcd);

#define PROFILE_AUDIO_BEGIN()                       profiler_audioBegin()
#define PROFILE_AUDIO_END()                         profiler_audioEnd()
#define PROFILE_LOOP_BEGIN()                        profiler_loopBegin()
#define PROFILE_LOOP_END()                          profiler_loopEnd()
#define PROFILE_DRAW(lcd_)                          profiler_draw(lcd_)

#else

#define PROFILE_AUDIO_BEGIN()                       ((void)0)
#define PROFILE_AUDIO_END()                         ((void)0)
#define PROFILE_LOOP_BEGIN()                        ((void)0)
#define PROFILE_LOOP_END()                          ((void)0)
#define PROFILE_DRAW(lcd_)                          ((void)0)

#endif

// clang-format on

#endif
