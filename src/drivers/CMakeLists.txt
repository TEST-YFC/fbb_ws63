#===============================================================================
# @brief    cmake make file
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2022-2022. All rights reserved.
#===============================================================================

add_subdirectory_if_exist(drivers)
add_subdirectory_if_exist(boards)
add_subdirectory_if_exist(chips)
add_subdirectory_if_exist(adapter)

# External components consume SDK driver APIs through stable public targets.
# The SDK's component helper keeps its header closure private by default, so
# republish the exact include set already used to compile the implementation.
if(TARGET gpio)
    get_target_property(_fbb_gpio_includes gpio INCLUDE_DIRECTORIES)
    if(_fbb_gpio_includes)
        target_include_directories(gpio INTERFACE ${_fbb_gpio_includes})
    endif()
endif()

# Keep public SDK component names stable when an implementation is supplied
# by the ROM closure rather than by a separately linked RAM library.
if(NOT TARGET pinctrl AND TARGET pinctrl_rom AND
        "pinctrl_rom" IN_LIST ROM_COMPONENT)
    add_library(pinctrl INTERFACE)
    target_link_libraries(pinctrl INTERFACE pinctrl_rom)
    get_target_property(
        _fbb_pinctrl_includes pinctrl_rom INCLUDE_DIRECTORIES)
    if(_fbb_pinctrl_includes)
        target_include_directories(
            pinctrl INTERFACE ${_fbb_pinctrl_includes})
    endif()
endif()
