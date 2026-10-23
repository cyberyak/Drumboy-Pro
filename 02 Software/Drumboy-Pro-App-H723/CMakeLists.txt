# CMake build for the Drumboy Pro application firmware.
# Mirrors the STM32CubeIDE settings in .cproject so the project can be built
# from VS Code / the command line without CubeIDE.
#
#   Debug   : -Ofast -g3 -ffast-math, DEBUG defined   (CubeIDE "Debug")
#   Release : -Os -g0                                  (CubeIDE "Release")

cmake_minimum_required(VERSION 3.22)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

project(Drumboy-Pro-App-H723 C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MCU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)

option(DRUMBOY_CPU_METER "Show audio/main-loop CPU load at the top of the screen" OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
    Core/*.c Core/*.cpp Core/*.s
    FATFS/*.c
    Middlewares/*.c
    Drivers/*.c
)

add_executable(${PROJECT_NAME} ${SOURCES})
set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    FATFS/Target
    FATFS/App
    Drivers/STM32H7xx_HAL_Driver/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    Middlewares/Third_Party/FatFs/src
    Drivers/CMSIS/Device/ST/STM32H7xx/Include
    Drivers/CMSIS/Include
    Core/Library/Global
    Core/Library/Lcd
    Core/Library/Dac
    Core/Library/Controller
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H723xx
    $<$<CONFIG:Debug>:DEBUG>
    $<$<BOOL:${DRUMBOY_CPU_METER}>:DRUMBOY_CPU_METER>
)

target_compile_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    -ffunction-sections -fdata-sections -Wall -fstack-usage
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti -fno-use-cxa-atexit>
    $<$<COMPILE_LANGUAGE:ASM>:-x assembler-with-cpp>
    $<$<CONFIG:Debug>:-Ofast -g3 -ffast-math>
    $<$<CONFIG:Release>:-Os -g0>
)

target_link_options(${PROJECT_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/STM32H723ZGTX_FLASH.ld
    --specs=nosys.specs --specs=nano.specs
    -Wl,-Map=${PROJECT_NAME}.map -Wl,--gc-sections -Wl,--print-memory-usage
    -static
)
target_link_libraries(${PROJECT_NAME} PRIVATE -Wl,--start-group c m stdc++ supc++ -Wl,--end-group)

# Version shown by the bootloader during an SD-card update (major < 10, minor < 100).
set(FW_VERSION_MAJOR 1 CACHE STRING "Firmware package version major")
set(FW_VERSION_MINOR 1 CACHE STRING "Firmware package version minor")
math(EXPR _FW_MINOR_PADDED "${FW_VERSION_MINOR}" OUTPUT_FORMAT DECIMAL)
if(FW_VERSION_MINOR LESS 10)
    set(_FW_MINOR_PADDED "0${FW_VERSION_MINOR}")
endif()
set(FW_PACKAGE "Drumboy_Pro_Fw_0${FW_VERSION_MAJOR}_${_FW_MINOR_PADDED}.bin")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/make_firmware_package.py
            ${PROJECT_NAME}.bin ${FW_PACKAGE} --major ${FW_VERSION_MAJOR} --minor ${FW_VERSION_MINOR}
    COMMENT "Creating ${PROJECT_NAME}.bin and SD-card package ${FW_PACKAGE}"
)
