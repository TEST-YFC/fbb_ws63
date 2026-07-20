/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE UART Client Source. \n
 *
 * History: \n
 * 2024-05-18, Create file. \n
 */
#include "common_def.h"
#include "soc_osal.h"
#include "securec.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_uart_client.h"

#define SLE_SEEK_INTERVAL_DEFAULT       100
#define SLE_SEEK_WINDOW_DEFAULT         100
#define SLE_UART_WAIT_SLE_CORE_READY_MS 5000

#ifndef SLE_UART_SERVER_NAME
#define SLE_UART_SERVER_NAME           "uart_server"
#endif

#define SLE_UART_CLIENT_LOG            "[sle uart client]"

/* 全局连接句柄，入口文件透传发送任务中引用 */
uint16_t g_conn_id = 0;

static ssapc_find_service_result_t g_find_service_result = { 0 };
static sle_announce_seek_callbacks_t g_seek_cbk = { 0 };
static sle_connection_callbacks_t g_connect_cbk = { 0 };
static ssapc_callbacks_t g_ssapc_cbk = { 0 };
static sle_addr_t g_remote_addr = { 0 };

/* 保存用户回调 */
static ssapc_notification_callback g_saved_notification_cb = NULL;
static ssapc_write_cfm_callback g_saved_write_cfm_cb = NULL;

/* 写参数: 服务发现阶段保存 handle 和 type */
extern ssapc_write_param_t g_write_param;

static bool g_connected = false;

uint16_t sle_uart_client_is_connected(void)
{
    return g_connected ? 1 : 0;
}

void sle_uart_client_start_scan(void)
{
    sle_seek_param_t param = { 0 };
    param.own_addr_type = 0;
    param.filter_duplicates = 0;
    param.seek_filter_policy = 0;
    param.seek_phys = 1;
    param.seek_type[0] = 1;
    param.seek_interval[0] = SLE_SEEK_INTERVAL_DEFAULT;
    param.seek_window[0] = SLE_SEEK_WINDOW_DEFAULT;
    sle_set_seek_param(&param);
    sle_start_seek();
    osal_printk("%s start seek...\r\n", SLE_UART_CLIENT_LOG);
}

static void sle_uart_client_seek_cbk_register(void);
static void sle_uart_client_connect_cbk_register(void);
static void sle_uart_client_ssapc_cbk_register(ssapc_notification_callback notification_cb,
                                                ssapc_write_cfm_callback write_cfm_cb);

static void sle_uart_client_sle_enable_cbk(errcode_t status)
{
    osal_printk("%s sle enable status: %d.\r\n", SLE_UART_CLIENT_LOG, status);
    sle_uart_client_seek_cbk_register();
    sle_uart_client_connect_cbk_register();
    sle_uart_client_ssapc_cbk_register(g_saved_notification_cb, g_saved_write_cfm_cb);
    sle_uart_client_start_scan();
}

static void sle_uart_client_seek_enable_cbk(errcode_t status)
{
    if (status != 0) {
        osal_printk("%s seek enable error, status=%x\r\n", SLE_UART_CLIENT_LOG, status);
    }
}

static void sle_uart_client_seek_result_info_cbk(sle_seek_result_info_t *seek_result_data)
{
    if (seek_result_data == NULL) {
        osal_printk("%s seek result data is NULL\r\n", SLE_UART_CLIENT_LOG);
        return;
    }
    osal_printk("%s scan data: %s\r\n", SLE_UART_CLIENT_LOG, seek_result_data->data);

    if (strstr((const char *)seek_result_data->data, SLE_UART_SERVER_NAME) != NULL) {
        osal_printk("%s found uart_server, stopping seek...\r\n", SLE_UART_CLIENT_LOG);
        memcpy_s(&g_remote_addr, sizeof(sle_addr_t), &seek_result_data->addr, sizeof(sle_addr_t));
        sle_stop_seek();
    }
}

static void sle_uart_client_seek_disable_cbk(errcode_t status)
{
    osal_printk("%s seek disable, status=%x\r\n", SLE_UART_CLIENT_LOG, status);
    if (status == 0) {
        sle_remove_paired_remote_device(&g_remote_addr);
        osal_printk("%s connecting to remote device...\r\n", SLE_UART_CLIENT_LOG);
        sle_connect_remote_device(&g_remote_addr);
    }
}

static void sle_uart_client_seek_cbk_register(void)
{
    g_seek_cbk.sle_enable_cb = sle_uart_client_sle_enable_cbk;
    g_seek_cbk.seek_enable_cb = sle_uart_client_seek_enable_cbk;
    g_seek_cbk.seek_result_cb = sle_uart_client_seek_result_info_cbk;
    g_seek_cbk.seek_disable_cb = sle_uart_client_seek_disable_cbk;
    sle_announce_seek_register_callbacks(&g_seek_cbk);
}

static void sle_uart_client_connect_state_changed_cbk(uint16_t conn_id, const sle_addr_t *addr,
                                                       sle_acb_state_t conn_state, sle_pair_state_t pair_state,
                                                       sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    osal_printk("%s conn state changed, conn_id:0x%02x, state:0x%x, disc_reason:0x%x\r\n",
                SLE_UART_CLIENT_LOG, conn_id, conn_state, disc_reason);

    g_conn_id = conn_id;

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_connected = true;
        osal_printk("%s connected, conn_id=0x%02x\r\n", SLE_UART_CLIENT_LOG, conn_id);
        if (pair_state == SLE_PAIR_NONE) {
            osal_printk("%s start pairing...\r\n", SLE_UART_CLIENT_LOG);
            sle_pair_remote_device(&g_remote_addr);
        }
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_conn_id = 0;
        g_connected = false;
        osal_printk("%s disconnected, re-scanning...\r\n", SLE_UART_CLIENT_LOG);
        sle_remove_paired_remote_device(&g_remote_addr);

        /* 清空消息队列残留数据 */
        extern unsigned long g_sle_uart_client_msgq_id;
        uint8_t dummy[CONFIG_SLE_UART_MSGQ_ITEM_SIZE];
        uint32_t len = CONFIG_SLE_UART_MSGQ_ITEM_SIZE;
        while (osal_msg_queue_read_copy(g_sle_uart_client_msgq_id, dummy,
                                        &len, 0) == OSAL_SUCCESS) {
            len = CONFIG_SLE_UART_MSGQ_ITEM_SIZE;
        }

        sle_uart_client_start_scan();
    }
}

static void sle_uart_client_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr, errcode_t status)
{
    osal_printk("%s pair complete conn_id:%d, addr:%02x***%02x%02x, status:%d\r\n",
                SLE_UART_CLIENT_LOG, conn_id,
                addr->addr[0], addr->addr[4], addr->addr[5], status);
    if (status == 0) {
        ssap_exchange_info_t info = {0};
        info.mtu_size = CONFIG_SLE_UART_MTU_SIZE;
        info.version = 1;
        ssapc_exchange_info_req(0, g_conn_id, &info);
        osal_printk("%s exchange info req sent.\r\n", SLE_UART_CLIENT_LOG);
    }
}

static void sle_uart_client_connect_cbk_register(void)
{
    g_connect_cbk.connect_state_changed_cb = sle_uart_client_connect_state_changed_cbk;
    g_connect_cbk.pair_complete_cb = sle_uart_client_pair_complete_cbk;
    sle_connection_register_callbacks(&g_connect_cbk);
}

static void sle_uart_client_exchange_info_cbk(uint8_t client_id, uint16_t conn_id,
                                               ssap_exchange_info_t *param, errcode_t status)
{
    osal_printk("%s exchange info cbk client_id:%d, conn_id:%d, status:%d\r\n",
                SLE_UART_CLIENT_LOG, client_id, conn_id, status);
    osal_printk("%s mtu size: %d, version: %d.\r\n",
                SLE_UART_CLIENT_LOG, param->mtu_size, param->version);

    /* 开始发现服务 */
    ssapc_find_structure_param_t find_param = { 0 };
    find_param.type = SSAP_FIND_TYPE_PROPERTY;
    find_param.start_hdl = 1;
    find_param.end_hdl = 0xFFFF;
    ssapc_find_structure(0, conn_id, &find_param);
    osal_printk("%s start find structure...\r\n", SLE_UART_CLIENT_LOG);
}

static void sle_uart_client_find_structure_cbk(uint8_t client_id, uint16_t conn_id,
                                                ssapc_find_service_result_t *service, errcode_t status)
{
    osal_printk("%s find structure cbk client:%d, conn_id:%d, status:%d\r\n",
                SLE_UART_CLIENT_LOG, client_id, conn_id, status);
    g_find_service_result.start_hdl = service->start_hdl;
    g_find_service_result.end_hdl = service->end_hdl;
    memcpy_s(&g_find_service_result.uuid, sizeof(sle_uuid_t), &service->uuid, sizeof(sle_uuid_t));
}

static void sle_uart_client_find_property_cbk(uint8_t client_id, uint16_t conn_id,
                                               ssapc_find_property_result_t *property, errcode_t status)
{
    osal_printk("%s find property cbk, client:%d, conn:%d, handle:%d, operate_ind:%d, status:%d\r\n",
                SLE_UART_CLIENT_LOG, client_id, conn_id, property->handle,
                property->operate_indication, status);

    /* 保存 handle 和 type, 供透传发送任务使用 */
    g_write_param.handle = property->handle;
    g_write_param.type = SSAP_PROPERTY_TYPE_VALUE;
}

static void sle_uart_client_find_structure_cmp_cbk(uint8_t client_id, uint16_t conn_id,
                                                    ssapc_find_structure_result_t *structure_result,
                                                    errcode_t status)
{
    unused(conn_id);
    osal_printk("%s find structure cmp cbk, client:%d, status:%d, type:%d\r\n",
                SLE_UART_CLIENT_LOG, client_id, status, structure_result->type);
    osal_printk("%s service discovery complete\r\n", SLE_UART_CLIENT_LOG);
    osal_printk("%s === bridge ready ===\r\n", SLE_UART_CLIENT_LOG);
}

static void sle_uart_client_ssapc_cbk_register(ssapc_notification_callback notification_cb,
                                                ssapc_write_cfm_callback write_cfm_cb)
{
    g_ssapc_cbk.exchange_info_cb = sle_uart_client_exchange_info_cbk;
    g_ssapc_cbk.find_structure_cb = sle_uart_client_find_structure_cbk;
    g_ssapc_cbk.ssapc_find_property_cbk = sle_uart_client_find_property_cbk;
    g_ssapc_cbk.find_structure_cmp_cb = sle_uart_client_find_structure_cmp_cbk;
    g_ssapc_cbk.notification_cb = notification_cb;
    g_ssapc_cbk.indication_cb = NULL;  /* 不使用 Indication */
    g_ssapc_cbk.write_cfm_cb = write_cfm_cb;
    ssapc_register_callbacks(&g_ssapc_cbk);
}

void sle_uart_client_init(ssapc_notification_callback notification_cb,
                          ssapc_write_cfm_callback write_cfm_cb)
{
    g_saved_notification_cb = notification_cb;
    g_saved_write_cfm_cb = write_cfm_cb;

    (void)osal_msleep(SLE_UART_WAIT_SLE_CORE_READY_MS);
    osal_printk("%s init...\r\n", SLE_UART_CLIENT_LOG);

    sle_uart_client_seek_cbk_register();
    sle_uart_client_connect_cbk_register();
    sle_uart_client_ssapc_cbk_register(notification_cb, write_cfm_cb);

    if (enable_sle() != ERRCODE_SUCC) {
        osal_printk("[SLE UART Client] sle enable fail!\r\n");
    }
}
