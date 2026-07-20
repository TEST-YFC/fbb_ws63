/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE UART Server Config. \n
 *
 * History: \n
 * 2024-05-18, Create file. \n
 */

#ifndef SLE_UART_SERVER_H
#define SLE_UART_SERVER_H

#include <stdint.h>
#include "sle_ssap_server.h"
#include "errcode.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* Service UUID */
#define SLE_UART_SERVER_SERVICE        0x2222

/* Property UUID */
#define SLE_UART_SERVER_NTF_REPORT     0x2323

/* Property permissions: read + write */
#define SLE_UART_SRV_PROPERTIES        (SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE)

/* Operation indication: read + write + notify */
#define SLE_UART_SRV_OPERATION         (SSAP_OPERATE_INDICATION_BIT_READ | \
                                        SSAP_OPERATE_INDICATION_BIT_WRITE | \
                                        SSAP_OPERATE_INDICATION_BIT_NOTIFY)

/* Descriptor permissions */
#define SLE_UART_SRV_DESCRIPTOR        (SSAP_PERMISSION_READ)

errcode_t sle_uart_server_init(ssaps_read_request_callback read_cb,
                               ssaps_write_request_callback write_cb);

errcode_t sle_uart_server_send_notification(const uint8_t *data, uint16_t len);

uint16_t sle_uart_server_is_connected(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif
