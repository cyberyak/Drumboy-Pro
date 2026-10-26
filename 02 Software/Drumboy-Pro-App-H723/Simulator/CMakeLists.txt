# Drumboy Pro simulator: the firmware compiled for macOS (preset "Simulator").
#
# The application (Core/Library, Core/Plugin, main.cpp), the CubeMX init files
# (Core/Src) and FatFs compile unchanged against the real ST headers. Replaced:
#   - CMSIS core header (Simulator/hal/core_cm7.h): intrinsics -> sim runtime
#   - HAL driver sources (src/sim_hal.cpp)
#   - SD card driver (src/sim_diskio.cpp): FatFs on a card image
#
# Built for x86_64 and run with Rosetta on Apple silicon: the firmware keeps
# MCU addresses (SDRAM 0xC0000000, RAM_D3 0x38000000, registers) in uint32_t,
# so they are mapped at the same addresses, below 4 GB. arm64 macOS reserves
# the whole low 4 GB (__PAGEZERO); x86_64 allows a small one.

if(NOT APPLE)
    message(FATAL_ERROR "The simulator currently builds on macOS only")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
    message(FATAL_ERROR "The simulator must be built with CMAKE_OSX_ARCHITECTURES=x86_64 (use the Simulator preset)")
endif()

set(FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

file(GLOB_RECURSE SIM_FIRMWARE_SOURCES CONFIGURE_DEPENDS
    ${FW_DIR}/Core/Library/*.cpp
    ${FW_DIR}/Core/Plugin/*.cpp
    ${FW_DIR}/Middlewares/Third_Party/FatFs/src/*.c
)
list(APPEND SIM_FIRMWARE_SOURCES
    ${FW_DIR}/Core/Src/main.cpp
    ${FW_DIR}/Core/Src/bdma.c
    ${FW_DIR}/Core/Src/dma.c
    ${FW_DIR}/Core/Src/fmc.c
    ${FW_DIR}/Core/Src/gpio.c
    ${FW_DIR}/Core/Src/i2c.c
    ${FW_DIR}/Core/Src/i2s.c
    ${FW_DIR}/Core/Src/sdmmc.c
    ${FW_DIR}/Core/Src/stm32h7xx_hal_msp.c
    ${FW_DIR}/Core/Src/tim.c
    ${FW_DIR}/Core/Src/usart.c
    ${FW_DIR}/FATFS/App/fatfs.c
)

set(SIM_SOURCES
    src/sim_cpu.cpp
    src/sim_diskio.cpp
    src/sim_hal.cpp
    src/sim_main.cpp
    src/sim_memory.cpp
)

add_executable(DrumboySim ${SIM_FIRMWARE_SOURCES} ${SIM_SOURCES})

target_include_directories(DrumboySim PRIVATE
    hal                                         # before the CMSIS headers: replaces core_cm7.h
    src
    ${FW_DIR}/Core/Inc
    ${FW_DIR}/FATFS/Target
    ${FW_DIR}/FATFS/App
    ${FW_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${FW_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${FW_DIR}/Middlewares/Third_Party/FatFs/src
    ${FW_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${FW_DIR}/Core/Library/Global
    ${FW_DIR}/Core/Library/Lcd
    ${FW_DIR}/Core/Library/Dac
    ${FW_DIR}/Core/Library/Controller
    ${FW_DIR}/Core/Plugin
)

target_compile_definitions(DrumboySim PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H723xx
    DRUMBOY_SIMULATOR
    $<$<BOOL:${DRUMBOY_PLUGINS}>:DRUMBOY_PLUGINS>
    $<$<BOOL:${DRUMBOY_BRANDING}>:DRUMBOY_BRANDING>
    DRUMBOY_BUILD_NUMBER=${FW_BUILD_NUMBER}
)

target_compile_options(DrumboySim PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/hal/sim_prelude.h
    -O2 -g -ffast-math
    -fno-strict-aliasing                        # register/buffer type punning as on the MCU
    -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-unused-but-set-variable
)
set_source_files_properties(${SIM_FIRMWARE_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties(${FW_DIR}/Core/Src/main.cpp PROPERTIES COMPILE_DEFINITIONS "main=firmware_main")

target_link_options(DrumboySim PRIVATE -Wl,-pagezero_size,0x1000)
