# Toolchain file for the Arm GNU toolchain (arm-none-eabi).
# Search order for arm-none-eabi-gcc:
#   1. ARM_TOOLCHAIN_DIR (CMake variable or environment variable): the toolchain
#      folder (the one containing bin/), required to contain the compiler
#   2. arm-gnu-toolchain-*-arm-none-eabi folders in tools/ inside the repo or in
#      tools/ next to the repo checkout
#   3. PATH

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if(NOT ARM_TOOLCHAIN_DIR AND DEFINED ENV{ARM_TOOLCHAIN_DIR})
    set(ARM_TOOLCHAIN_DIR "$ENV{ARM_TOOLCHAIN_DIR}")
endif()
# Passed on to try_compile projects, which re-read this file
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ARM_TOOLCHAIN_DIR)

if(ARM_TOOLCHAIN_DIR)
    find_program(CMAKE_C_COMPILER arm-none-eabi-gcc PATHS "${ARM_TOOLCHAIN_DIR}/bin" NO_DEFAULT_PATH)
    if(NOT CMAKE_C_COMPILER)
        message(FATAL_ERROR "arm-none-eabi-gcc not found in ARM_TOOLCHAIN_DIR/bin (${ARM_TOOLCHAIN_DIR}/bin)")
    endif()
else()
    get_filename_component(_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
    file(GLOB _ARM_GCC_DIRS
        "${_REPO_DIR}/tools/arm-gnu-toolchain-*-arm-none-eabi/bin"
        "${_REPO_DIR}/../tools/arm-gnu-toolchain-*-arm-none-eabi/bin")
    list(SORT _ARM_GCC_DIRS COMPARE NATURAL ORDER DESCENDING)
    find_program(CMAKE_C_COMPILER arm-none-eabi-gcc HINTS ${_ARM_GCC_DIRS})
    if(NOT CMAKE_C_COMPILER)
        message(FATAL_ERROR "arm-none-eabi-gcc not found. Put the Arm GNU toolchain on PATH, "
            "set ARM_TOOLCHAIN_DIR (e.g. -DARM_TOOLCHAIN_DIR=/path/to/arm-gnu-toolchain-14.2.rel1-...-arm-none-eabi), "
            "or unpack it into tools/ next to the repo checkout")
    endif()
endif()
get_filename_component(_ARM_BIN "${CMAKE_C_COMPILER}" DIRECTORY)

set(CMAKE_CXX_COMPILER "${_ARM_BIN}/arm-none-eabi-g++")
set(CMAKE_ASM_COMPILER "${_ARM_BIN}/arm-none-eabi-gcc")
set(CMAKE_OBJCOPY      "${_ARM_BIN}/arm-none-eabi-objcopy")
set(CMAKE_SIZE         "${_ARM_BIN}/arm-none-eabi-size")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
