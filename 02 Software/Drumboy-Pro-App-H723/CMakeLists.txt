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

# Firmware version: single source of truth is the VERSION file ("MAJOR.MINOR", e.g. 1.50).
# The bootloader shows it during an SD-card update and accepts major 0-9, minor 00-99.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" FW_VERSION LIMIT_COUNT 1)
if(NOT FW_VERSION MATCHES "^([0-9])\\.([0-9][0-9])$")
    message(FATAL_ERROR "VERSION must look like 1.50 (major 0-9, minor 00-99), got '${FW_VERSION}'")
endif()
set(FW_VERSION_MAJOR ${CMAKE_MATCH_1})
math(EXPR FW_VERSION_MINOR "${CMAKE_MATCH_2}")
set(FW_PACKAGE "Drumboy_Pro_Fw_0${FW_VERSION_MAJOR}_${CMAKE_MATCH_2}.bin")
message(STATUS "Drumboy Pro firmware version ${FW_VERSION} -> ${FW_PACKAGE}")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MCU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)

option(DRUMBOY_CPU_METER "Show audio/main-loop CPU load at the top of the screen" OFF)
option(DRUMBOY_PLUGINS "Enable the plugin host and hooks (Core/Plugin)" OFF)

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
    Core/Plugin
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_PWR_LDO_SUPPLY
    USE_HAL_DRIVER
    STM32H723xx
    $<$<CONFIG:Debug>:DEBUG>
    $<$<BOOL:${DRUMBOY_CPU_METER}>:DRUMBOY_CPU_METER>
    $<$<BOOL:${DRUMBOY_PLUGINS}>:DRUMBOY_PLUGINS>
    DRUMBOY_VERSION_MAJOR=${FW_VERSION_MAJOR}
    DRUMBOY_VERSION_MINOR=${FW_VERSION_MINOR}
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

find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}>
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/make_firmware_package.py
            ${PROJECT_NAME}.bin ${FW_PACKAGE} --major ${FW_VERSION_MAJOR} --minor ${FW_VERSION_MINOR}
    COMMENT "Creating ${PROJECT_NAME}.bin and SD-card package ${FW_PACKAGE}"
)
