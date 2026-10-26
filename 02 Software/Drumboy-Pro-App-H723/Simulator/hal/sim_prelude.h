// Force-included into every firmware source of the simulator build (-include).

#ifndef __SIM_PRELUDE_H
#define __SIM_PRELUDE_H

// The firmware places buffers in MCU RAM regions with
// __attribute__((section(".RAM_D3"))) etc. Mach-O needs "segment,section"
// names, and on the host these regions are ordinary memory, so the attribute
// is turned into a harmless one.
#define section(name) aligned(1)

// Apple clang predefines __weak (Objective-C), so stm32h7xx_hal_def.h keeps
// it and the HAL/FatFs weak defaults (callbacks, get_fattime) would clash
// with the firmware's definitions.
#undef __weak
#define __weak __attribute__((weak))

// FatFs integer types (replaces Middlewares/.../FatFs/src/integer.h, which is
// skipped through its include guard). FatFs needs 32-bit DWORD/LONG; the
// embedded definitions use long, which is 64-bit on the host. As on the
// device, DWORD and uint32_t are then the same type.
#define _FF_INTEGER
typedef int INT;
typedef unsigned int UINT;
typedef unsigned char BYTE;
typedef short SHORT;
typedef unsigned short WORD;
typedef unsigned short WCHAR;
typedef int LONG;
typedef unsigned int DWORD;
typedef unsigned long long QWORD;

// sprintf into fixed buffers: the firmware has off-by-one overflows (a format
// one byte longer than the buffer, e.g. "       %03d" into char[10]). On the
// device the extra NUL lands in a neighbouring stack slot and goes unnoticed;
// on x86_64 it can hit a saved register. sim_sprintf keeps the bytes that fit
// (the same buffer contents as on the device), drops the rest and reports the
// call site once. Buffers of unknown size are written normally.
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C"
#endif
int sim_sprintf(char* buffer, size_t size, const char* file, int line, const char* format, ...) __attribute__((format(printf, 5, 6)));
#undef sprintf
#define sprintf(buffer, ...) sim_sprintf((buffer), __builtin_object_size((buffer), 1), __FILE__, __LINE__, __VA_ARGS__)

#endif
