# Toolchain file for the Arm GNU toolchain (arm-none-eabi).
# Looks in ~/Projects/DrumBoy/tools first, then falls back to PATH.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

file(GLOB _ARM_GCC_DIRS "$ENV{HOME}/Projects/DrumBoy/tools/arm-gnu-toolchain-*-arm-none-eabi/bin")
find_program(CMAKE_C_COMPILER arm-none-eabi-gcc HINTS ${_ARM_GCC_DIRS} REQUIRED)
get_filename_component(_ARM_BIN "${CMAKE_C_COMPILER}" DIRECTORY)

set(CMAKE_CXX_COMPILER "${_ARM_BIN}/arm-none-eabi-g++")
set(CMAKE_ASM_COMPILER "${_ARM_BIN}/arm-none-eabi-gcc")
set(CMAKE_OBJCOPY      "${_ARM_BIN}/arm-none-eabi-objcopy")
set(CMAKE_SIZE         "${_ARM_BIN}/arm-none-eabi-size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
