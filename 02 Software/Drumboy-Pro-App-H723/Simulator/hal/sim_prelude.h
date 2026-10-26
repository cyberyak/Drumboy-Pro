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

#endif
