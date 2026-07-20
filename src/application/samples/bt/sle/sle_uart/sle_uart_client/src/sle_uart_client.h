/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE UART Client Config. \n
 *
 * History: \n
 * 2024-05-18, Create file. \n
 */

#ifndef SLE_UART_CLIENT_H
#define SLE_UART_CLIENT_H

#include <stdint.h>
#include "sle_ssap_client.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

void sle_uart_client_init(ssapc_notification_callback notification_cb,
                          ssapc_write_cfm_callback write_cfm_cb);

uint16_t sle_uart_client_is_connected(void);

void sle_uart_client_start_scan(void);

/* 连接句柄, 入口文件透传任务中引用 */
extern uint16_t g_conn_id;

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif
