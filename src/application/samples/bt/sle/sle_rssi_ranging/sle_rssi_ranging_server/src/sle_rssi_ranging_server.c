/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE RSSI ranging server implementation. \n
 */
#include "common_def.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_rssi_ranging_server_adv.h"
#include "sle_rssi_ranging_server.h"

#define SLE_RSSI_SERVER_LOG "[sle rssi server]"

static void sle_rssi_server_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        osal_printk("%s connected, conn_id=0x%02x\r\n", SLE_RSSI_SERVER_LOG, conn_id);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        osal_printk("%s disconnected, reason=0x%x, restart announce\r\n", SLE_RSSI_SERVER_LOG, disc_reason);
        (void)sle_start_announce(SLE_RSSI_RANGING_ADV_HANDLE);
    }
}

static errcode_t sle_rssi_server_register_connection_callbacks(void)
{
    sle_connection_callbacks_t callbacks = {0};
    callbacks.connect_state_changed_cb = sle_rssi_server_state_changed_cb;
    return sle_connection_register_callbacks(&callbacks);
}

errcode_t sle_rssi_ranging_server_init(void)
{
    errcode_t ret = enable_sle();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s enable SLE failed: 0x%x\r\n", SLE_RSSI_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_rssi_ranging_announce_register_callbacks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce callbacks failed: 0x%x\r\n", SLE_RSSI_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_rssi_server_register_connection_callbacks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register connection callbacks failed: 0x%x\r\n", SLE_RSSI_SERVER_LOG, ret);
        return ret;
    }
    return sle_rssi_ranging_server_announce_start();
}
