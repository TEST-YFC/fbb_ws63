#===============================================================================
# @brief    cmake file — BLE WiFi provisioning sample
# Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
#===============================================================================
set(SOURCES "${SOURCES}"
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_wifi_cfg_sample.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_wifi_prov_nv.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_wifi_prov_led.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/ble_wifi_prov_btn.c"
    PARENT_SCOPE)

set(PRIVATE_HEADER
    ${ROOT_DIR}/samples/bt/ble/ble_wifi_cfg_server/inc
    ${ROOT_DIR}/samples/bt/ble/ble_wifi_cfg_client/inc
)