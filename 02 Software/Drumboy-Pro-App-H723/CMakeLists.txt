# CMake build for the Drumboy Pro application firmware.
# Mirrors the STM32CubeIDE settings in .cproject so the project can be built
# from VS Code / the command line without CubeIDE.
#
#   Debug   : -Ofast -g3 -ffast-math, DEBUG defined   (CubeIDE "Debug")
#   Release : -Os -g0                                  (CubeIDE "Release")

cmake_minimum_required(VERSION 3.22)

option(DRUMBOY_SIMULATOR "Build the desktop simulator (Simulator/) instead of the firmware" OFF)

if(NOT CMAKE_TOOLCHAIN_FILE AND NOT DRUMBOY_SIMULATOR)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/arm-none-eabi.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

project(Drumboy-Pro-App-H723 C CXX ASM)

# Firmware version "<base>-cy.<build>", e.g. 1.01-cy.1:
#   base  = stock Randomwaves version (kVersionMajor/kVersionMinor in Global.h, owned upstream);
#           shown by the bootloader during an SD-card update (major 0-9, minor 00-99)
#   build = cyberyak build number, the only content of the VERSION file ("cy.1")
set(_GLOBAL_H "${CMAKE_CURRENT_SOURCE_DIR}/Core/Library/Global/Global.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" "${_GLOBAL_H}")

file(STRINGS "${_GLOBAL_H}" _VERSION_LINES REGEX "kVersion(Major|Minor) +=")
if(NOT _VERSION_LINES MATCHES "kVersionMajor += 0x([0-9A-Fa-f]+)")
    message(FATAL_ERROR "Could not read kVersionMajor from Global.h")
endif()
math(EXPR FW_VERSION_MAJOR "0x${CMAKE_MATCH_1}")
if(NOT _VERSION_LINES MATCHES "kVersionMinor += 0x([0-9A-Fa-f]+)")
    message(FATAL_ERROR "Could not read kVersionMinor from Global.h")
endif()
math(EXPR FW_VERSION_MINOR "0x${CMAKE_MATCH_1}")
if((FW_VERSION_MAJOR GREATER 9) OR (FW_VERSION_MINOR GREATER 99))
    message(FATAL_ERROR "Base version ${FW_VERSION_MAJOR}.${FW_VERSION_MINOR} exceeds the bootloader limits (0-9 / 00-99)")
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _BUILD LIMIT_COUNT 1)
if(NOT _BUILD MATCHES "^cy\\.([0-9]+)$")
    message(FATAL_ERROR "VERSION must look like cy.1, got '${_BUILD}'")
endif()
set(FW_BUILD_NUMBER ${CMAKE_MATCH_1})

math(EXPR _MINOR2 "${FW_VERSION_MINOR}" OUTPUT_FORMAT DECIMAL)
if(FW_VERSION_MINOR LESS 10)
    set(_MINOR2 "0${FW_VERSION_MINOR}")
endif()
set(_BUILD2 "${FW_BUILD_NUMBER}")
if(FW_BUILD_NUMBER LESS 10)
    set(_BUILD2 "0${FW_BUILD_NUMBER}")
endif()
set(FW_VERSION "${FW_VERSION_MAJOR}.${_MINOR2}-cy.${FW_BUILD_NUMBER}")
set(FW_PACKAGE "Drumboy_Pro_Fw_0${FW_VERSION_MAJOR}_${_MINOR2}_cy${_BUILD2}.bin")  # <= 32 chars (bootloader LFN limit)
file(WRITE "${CMAKE_BINARY_DIR}/version.txt" "${FW_VERSION}\n")
message(STATUS "Drumboy Pro firmware ${FW_VERSION} -> ${FW_PACKAGE}")

option(DRUMBOY_PLUGINS "Plugin host, hooks and built-in plugins (Core/Plugin), enabled via Plugins/plugins.txt" ON)
option(DRUMBOY_BRANDING "Show the custom build label (CYBERYAK <version>) on screen" ON)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(DRUMBOY_SIMULATOR)
    add_subdirectory(Simulator)
    return()
endif()

set(MCU_FLAGS -mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb)

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
    $<$<BOOL:${DRUMBOY_PLUGINS}>:DRUMBOY_PLUGINS>
    $<$<BOOL:${DRUMBOY_BRANDING}>:DRUMBOY_BRANDING>
    DRUMBOY_BUILD_NUMBER=${FW_BUILD_NUMBER}
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
