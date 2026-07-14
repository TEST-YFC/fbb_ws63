#===============================================================================
# @brief    Shared C++ build configuration for the Arduino component (ws63).
#           Included by utils/arduino/CMakeLists.txt (and, when present, by the
#           arduino_adapt test components). Chip-specific: arch + stdlib live
#           here so the chip-agnostic CMakeLists stays portable.
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
#===============================================================================

# REWRITE_CCFLAGS=true: use only COMPONENT_CCFLAGS, discard global CCFLAGS.
# This prevents C-only flags (-std=gnu99, -Wstrict-prototypes, etc.) from being
# passed to the C++ compiler (cc1plus), which would otherwise cause errors.
set(REWRITE_CCFLAGS
    true
)

set(COMPONENT_CCFLAGS
    -mabi=ilp32f
    -march=rv32imfc
    -Wall
    -Werror
    -Wextra
    -Winit-self
    -Wpointer-arith
    -Wno-type-limits
    -fno-strict-aliasing
    -Os
    -fno-unwind-tables
    -Wno-sign-compare
    -fno-builtin-printf
    # C++-only flags wrapped in generator expressions — safe for mixed C/C++ components
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:-nostdinc++>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

# Link C++ standard library (required for C++ sources using new/delete, virtual functions, etc.)
set(LIBS
    supc++
)
