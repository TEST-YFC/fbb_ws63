/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 * Description: BLE Hello GATT client interface.
 */

#ifndef BLE_UART_BRIDGE_CLIENT_H
#define BLE_UART_BRIDGE_CLIENT_H

#include <stdint.h>
#include "errcode.h"

errcode_t ble_uart_bridge_client_init(void);
errcode_t ble_uart_bridge_client_send_write(const uint8_t *data, uint16_t length);

#endif
