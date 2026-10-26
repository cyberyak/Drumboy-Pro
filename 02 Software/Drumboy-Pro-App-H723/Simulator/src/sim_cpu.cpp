// CPU intrinsics declared in Simulator/hal/core_cm7.h.
//
// PRIMASK is a single flag for now; the interrupt model (timer/audio threads
// sharing one lock with the masked sections) replaces it.

#include <atomic>
#include <thread>

#include "core_cm7.h"
#include "sim.h"

extern "C" {

uint32_t sim_fpscr = 0;

static std::atomic<uint32_t> primask(0);

void __enable_irq(void) { primask = 0; }
void __disable_irq(void) { primask = 1; }
uint32_t __get_PRIMASK(void) { return primask; }
void __set_PRIMASK(uint32_t priMask) { primask = priMask & 1U; }

void __WFI(void) { std::this_thread::yield(); }
void __WFE(void) { std::this_thread::yield(); }

void NVIC_SystemReset(void) { sim::restart(); }

}  // extern "C"
