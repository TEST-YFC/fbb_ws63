#===============================================================================
# @brief    CMake file for the BLE Hello sample.
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
#===============================================================================
if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_GATEWAY_SERVER_SAMPLE OR
   DEFINED CONFIG_SAMPLE_SUPPORT_BLE_GATEWAY_CLIENT_SAMPLE)
    set(SOURCES ${SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/ble_gateway.c)

    if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_GATEWAY_SERVER_SAMPLE)
        add_subdirectory_if_exist(ble_gateway_server)
    endif()

    if(DEFINED CONFIG_SAMPLE_SUPPORT_BLE_GATEWAY_CLIENT_SAMPLE)
        add_subdirectory_if_exist(ble_gateway_client)
    endif()
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER}" PARENT_SCOPE)
