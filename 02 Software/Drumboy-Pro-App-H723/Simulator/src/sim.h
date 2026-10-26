// Simulator runtime shared between the sim sources (not seen by firmware code).

#ifndef __SIM_H
#define __SIM_H

#include <cstdint>
#include <string>

namespace sim {

struct Options {
    std::string cardImage;                                      // Raw SD card image (FAT32), empty = no card
    uint32_t runMs = 0;                                         // Headless: stop after this time, 0 = run forever
};

extern Options options;

// Memory map (sim_memory.cpp)
void mapMemory();                                               // Fixed-address MCU regions, runs before firmware globals
void resetPeripherals();                                        // Register reset values the firmware relies on
void startCycleCounter();                                       // Thread that advances DWT_CYCCNT at the core clock

// Process
[[noreturn]] void restart();                                    // NVIC_SystemReset: re-exec with the same arguments

// Time
uint64_t nowUs();                                               // Monotonic time since simulator start

// SD card (sim_diskio.cpp)
bool cardOpen(const std::string& path);
bool cardInserted();

}  // namespace sim

#endif
