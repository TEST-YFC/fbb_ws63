/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 *
 * @if Eng
 * @brief Declares the BLE UART bridge GATT client interface.
 * @else
 * @brief 声明 BLE UART 透传 GATT 客户端接口。
 * @endif
 *
 * History: \n
 * 2026-07-25, Create file. \n
 */

#ifndef BLE_UART_BRIDGE_CLIENT_H
#define BLE_UART_BRIDGE_CLIENT_H

#include <stdint.h>
#include "errcode.h"

/**
 * @if Eng
 * @brief Initializes the BLE UART bridge client.
 * @else
 * @brief 初始化 BLE UART 透传客户端。
 * @endif
 */
errcode_t ble_uart_bridge_client_init(void);

/**
 * @if Eng
 * @brief Sends one queued UART fragment through a GATT write command.
 * @else
 * @brief 通过 GATT 写命令发送一段已排队的 UART 数据。
 * @endif
 */
errcode_t ble_uart_bridge_client_send_write(const uint8_t *data, uint16_t length);

#endif /* BLE_UART_BRIDGE_CLIENT_H */
