/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2024. All rights reserved.
 *
 * Description: SLE Sensor Report Server core logic. \n
 *
 * History: \n
 * 2024-06-01, Create file. \n
 */

#include "common_def.h"
#include "securec.h"
#include "errcode.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_connection_manager.h"
#include "sle_ssap_server.h"
#include "sle_errcode.h"
#include "sle_sensor_report_server.h"
#include "sle_sensor_report_server_adv.h"
#include "stdlib.h"

#define SENSOR_SERVER_LOG "[sensor server]"

/* App UUID (16-bit) */
static char g_sensor_app_uuid[2] = {0x12, 0x34};

/* SLE 128-bit base UUID */
static uint8_t g_sensor_base_uuid[] = { 0x37, 0xBE, 0xA8, 0x80, 0xFC, 0x70, 0x11, 0xEA,
    0xB7, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define UUID_16BIT_LEN  2
#define UUID_128BIT_LEN 16
#define UUID_INDEX      14

/* ── UUID 工具 (参照 hello/uart) ── */

static void encode2byte_little(uint8_t *_ptr, uint16_t data)
{
    *(uint8_t *)((_ptr) + 1) = (uint8_t)((data) >> 0x8);
    *(uint8_t *)(_ptr) = (uint8_t)(data);
}

static void sle_uuid_set_base(sle_uuid_t *out)
{
    errcode_t ret;
    ret = memcpy_s(out->uuid, SLE_UUID_LEN, g_sensor_base_uuid, SLE_UUID_LEN);
    if (ret != EOK) {
        out->len = 0;
        return;
    }
    out->len = UUID_16BIT_LEN;
}

static void sle_uuid_setu2(uint16_t u2, sle_uuid_t *out)
{
    sle_uuid_set_base(out);
    out->len = UUID_16BIT_LEN;
    encode2byte_little(&out->uuid[UUID_INDEX], u2);
}

/* SSAP Server 全局状态 */
static uint8_t  g_server_id = 0;
static uint16_t g_service_handle = 0;
static uint16_t g_data_property_handle = 0;   /* 常规数据 Property */
static uint16_t g_alarm_property_handle = 0;  /* 告警数据 Property */
static uint16_t g_sle_conn_hdl = 0;
static bool     g_connected = false;

/* 定时器 */
static osal_timer g_sensor_report_timer = {0};

/* 生成模拟传感器数据的调用计数 */
static uint32_t g_sensor_call_count = 0;

/* ── 模拟数据生成 ── */

/* 简谐波表: 16 点正弦近似 (x100), 幅值 ±500 */
static const int16_t g_sine_table[16] = {
    0, 195, 383, 500, 500, 383, 195, 0,
    0, -195, -383, -500, -500, -383, -195, 0
};

static int16_t get_simulated_temperature(void)
{
    g_sensor_call_count++;
    /* 基准 25.00C + 逐步升温 (每秒 +1.0C) + 简谐波 ±5C, 约 1 分钟触发告警 */
    int16_t drift = (int16_t)(g_sensor_call_count * 100);
    int16_t sine  = g_sine_table[g_sensor_call_count & 0xF];
    int16_t noise = (int16_t)(rand() % 31 - 15);  /* ±0.15℃ 随机抖动 */
    int16_t temp  = 2500 + drift + sine + noise;
    /* 撞顶后降温回落至 ~65C，约 15 秒后重新爬入告警区，形成 ON/OFF 循环 */
    if (temp > 8500) {
        g_sensor_call_count = 40;
        temp = 8500;
    }
    if (temp < 2000) {
        temp = 2000;
    }
    return temp;
}

static uint8_t get_simulated_humidity(void)
{
    int16_t val = 60 + (rand() % 11 - 5);  /* 60% +- 5% */
    if (val < 45) {
        val = 45;
    }
    if (val > 75) {
        val = 75;
    }
    return (uint8_t)val;
}

static uint16_t get_simulated_light(void)
{
    int32_t val = 1200 + (rand() % 401 - 200);  /* 1200 +- 200 lux */
    if (val < 500) {
        val = 500;
    }
    if (val > 2000) {
        val = 2000;
    }
    return (uint16_t)val;
}

/* ── 定时器回调：数据打包与发送 ── */

static void sensor_report_timer_cb(unsigned long arg)
{
    unused(arg);

    if (!g_connected) {
        return;
    }

    sensor_data_frame_t frame;
    (void)memset_s(&frame, sizeof(frame), 0, sizeof(frame));

    /* 生成模拟数据 */
    frame.temperature = get_simulated_temperature();
    frame.humidity    = get_simulated_humidity();
    frame.light       = get_simulated_light();
    frame.sensor_count = 3;

    /* 获取时间戳 */
    osal_timeval tv;
    osal_gettimeofday(&tv);
    frame.timestamp = (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);

    /* 判断告警，选择对应 Property 通道 */
    uint16_t prop_handle;
    bool is_alarm = (frame.temperature > TEMP_ALARM_HIGH || frame.temperature < TEMP_ALARM_LOW);

    if (is_alarm) {
        frame.frame_type = SENSOR_FRAME_TYPE_ALARM;
        prop_handle = g_alarm_property_handle;
        osal_printk("%s ** ALARM ** temp=%d.%02dC, using IND Indicate\r\n",
                    SENSOR_SERVER_LOG,
                    frame.temperature / 100,
                    (frame.temperature >= 0) ? (frame.temperature % 100) : (-frame.temperature % 100));
    } else {
        frame.frame_type = SENSOR_FRAME_TYPE_PERIODIC;
        prop_handle = g_data_property_handle;
    }

    uint8_t send_buf[sizeof(sensor_data_frame_t)];
    (void)memcpy_s(send_buf, sizeof(send_buf), &frame, sizeof(frame));

    ssaps_ntf_ind_t param = {0};
    param.handle = prop_handle;
    param.type = SSAP_PROPERTY_TYPE_VALUE;
    param.value = send_buf;
    param.value_len = sizeof(send_buf);

    (void)ssaps_notify_indicate(g_server_id, g_sle_conn_hdl, &param);

    /* osal_timer 为单次触发，回调中重新启动以实现周期定时 */
    (void)osal_timer_start(&g_sensor_report_timer);
}

/* ── SSAPS 回调 ── */

static void ssaps_add_service_cbk(uint8_t server_id, sle_uuid_t *uuid,
                                  uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(uuid);
    unused(handle);
    osal_printk("%s add service cbk, status: 0x%x\r\n", SENSOR_SERVER_LOG, status);
}

static void ssaps_add_property_cbk(uint8_t server_id, sle_uuid_t *uuid,
                                   uint16_t service_handle, uint16_t handle, errcode_t status)
{
    unused(server_id);
    unused(uuid);
    unused(service_handle);
    osal_printk("%s add property cbk, handle: 0x%x, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, handle, status);
}

static void ssaps_add_descriptor_cbk(uint8_t server_id, sle_uuid_t *uuid,
                                     uint16_t service_handle, uint16_t property_handle,
                                     errcode_t status)
{
    unused(server_id);
    unused(uuid);
    unused(service_handle);
    osal_printk("%s add descriptor cbk, property_handle: 0x%x, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, property_handle, status);
}

static void ssaps_start_service_cbk(uint8_t server_id, uint16_t handle, errcode_t status)
{
    unused(server_id);
    osal_printk("%s start service cbk, handle: 0x%x, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, handle, status);
}

static void ssaps_delete_all_service_cbk(uint8_t server_id, errcode_t status)
{
    osal_printk("%s delete all service cbk, server_id: %u, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, server_id, status);
}

static void ssaps_mtu_changed_cbk(uint8_t server_id, uint16_t conn_id,
                                  ssap_exchange_info_t *info, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    osal_printk("%s mtu changed cbk, mtu: %u, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, (info != NULL) ? info->mtu_size : 0, status);
}

static void ssaps_read_request_cb(uint8_t server_id, uint16_t conn_id,
                                  ssaps_req_read_cb_t *read_cb_para, errcode_t status)
{
    unused(server_id);
    unused(conn_id);
    unused(read_cb_para);
    unused(status);
    /* 传感器上报场景不处理 Client 读请求 */
}

static void ssaps_write_request_cb(uint8_t server_id, uint16_t conn_id,
                                   ssaps_req_write_cb_t *write_cb_para, errcode_t status)
{
    unused(conn_id);
    unused(status);

    /* CCCD 写入由协议栈内置处理，应用层不应拦截回复 */
    if (write_cb_para != NULL && write_cb_para->need_rsp &&
        write_cb_para->type != SSAP_DESCRIPTOR_CLIENT_CONFIGURATION) {
        ssaps_send_rsp_t rsp = {0};
        rsp.request_id = write_cb_para->request_id;
        rsp.status = ERRCODE_SLE_SUCCESS;
        (void)ssaps_send_response(server_id, conn_id, &rsp);
    }
}

static void ssaps_indicate_cfm_cb(uint8_t server_id, uint16_t conn_id,
                                  sle_indication_cfm_result_t cfm_result, errcode_t status)
{
    unused(server_id);
    osal_printk("%s indicate cfm cbk, conn_id: %u, result: %u, status: 0x%x\r\n",
                SENSOR_SERVER_LOG, conn_id, cfm_result, status);
}

static errcode_t sle_sensor_report_ssaps_register_cbks(void)
{
    ssaps_callbacks_t ssaps_cbk = {0};
    ssaps_cbk.add_service_cb      = ssaps_add_service_cbk;
    ssaps_cbk.add_property_cb     = ssaps_add_property_cbk;
    ssaps_cbk.add_descriptor_cb   = ssaps_add_descriptor_cbk;
    ssaps_cbk.start_service_cb    = ssaps_start_service_cbk;
    ssaps_cbk.delete_all_service_cb = ssaps_delete_all_service_cbk;
    ssaps_cbk.mtu_changed_cb      = ssaps_mtu_changed_cbk;
    ssaps_cbk.read_request_cb     = ssaps_read_request_cb;
    ssaps_cbk.write_request_cb    = ssaps_write_request_cb;
    ssaps_cbk.indicate_cfm_cb     = ssaps_indicate_cfm_cb;

    errcode_t ret = ssaps_register_callbacks(&ssaps_cbk);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register ssaps callbacks fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

/* ── SSAP 服务注册 ── */

static errcode_t sle_sensor_report_add_service(void)
{
    sle_uuid_t service_uuid = {0};
    sle_uuid_setu2(SENSOR_SERVICE_UUID, &service_uuid);
    errcode_t ret = ssaps_add_service_sync(g_server_id, &service_uuid, true, &g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add service fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_sensor_report_add_data_property(void)
{
    errcode_t ret;
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};
    uint8_t ntf_value[] = {0x01, 0x0};

    property.permissions = SENSOR_DATA_PROPERTY_PERMISSIONS;
    property.operate_indication = SENSOR_DATA_PROPERTY_OP_INDICATION;
    sle_uuid_setu2(SENSOR_DATA_PROPERTY_UUID, &property.uuid);
    property.value = (uint8_t *)osal_vmalloc(sizeof(sensor_data_frame_t));
    if (property.value == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    property.value_len = sizeof(sensor_data_frame_t);

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_data_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add data property fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }

    descriptor.permissions = SSAP_PERMISSION_READ;
    descriptor.type = SSAP_DESCRIPTOR_USER_DESCRIPTION;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ;
    descriptor.value = ntf_value;
    descriptor.value_len = sizeof(ntf_value);
    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_data_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add data descriptor fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }
    osal_vfree(property.value);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_sensor_report_add_alarm_property(void)
{
    errcode_t ret;
    ssaps_property_info_t property = {0};
    ssaps_desc_info_t descriptor = {0};

    property.permissions = SENSOR_PROPERTY_PERMISSIONS;
    property.operate_indication = SENSOR_ALARM_PROPERTY_OP_INDICATION;
    sle_uuid_setu2(SENSOR_ALARM_PROPERTY_UUID, &property.uuid);
    property.value = (uint8_t *)osal_vmalloc(sizeof(sensor_data_frame_t));
    if (property.value == NULL) {
        return ERRCODE_SLE_FAIL;
    }
    property.value_len = sizeof(sensor_data_frame_t);

    ret = ssaps_add_property_sync(g_server_id, g_service_handle, &property, &g_alarm_property_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add alarm property fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }

    /* CCCD — 初始值设为 0x0002 预开启 Indication (参照 data property 的 ntf_value 模式) */
    uint8_t ind_value[] = {0x02, 0x00};
    descriptor.permissions = SSAP_PERMISSION_READ | SSAP_PERMISSION_WRITE;
    descriptor.type = SSAP_DESCRIPTOR_CLIENT_CONFIGURATION;
    descriptor.operate_indication = SSAP_OPERATE_INDICATION_BIT_READ | SSAP_OPERATE_INDICATION_BIT_WRITE;
    descriptor.value = ind_value;
    descriptor.value_len = sizeof(ind_value);

    ret = ssaps_add_descriptor_sync(g_server_id, g_service_handle, g_alarm_property_handle, &descriptor);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s add alarm CCCD fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        osal_vfree(property.value);
        return ERRCODE_SLE_FAIL;
    }
    osal_vfree(property.value);
    return ERRCODE_SLE_SUCCESS;
}

static errcode_t sle_sensor_report_server_add(void)
{
    errcode_t ret;
    sle_uuid_t app_uuid = {0};

    app_uuid.len = sizeof(g_sensor_app_uuid);
    if (memcpy_s(app_uuid.uuid, app_uuid.len, g_sensor_app_uuid, sizeof(g_sensor_app_uuid)) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    ssaps_register_server(&app_uuid, &g_server_id);

    if (sle_sensor_report_add_service() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }
    if (sle_sensor_report_add_data_property() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }
    if (sle_sensor_report_add_alarm_property() != ERRCODE_SLE_SUCCESS) {
        ssaps_unregister_server(g_server_id);
        return ERRCODE_SLE_FAIL;
    }
    osal_printk("%s add service ok, server_id:%x, svc_hdl:%x, data_hdl:%x, alarm_hdl:%x\r\n",
                SENSOR_SERVER_LOG, g_server_id, g_service_handle,
                g_data_property_handle, g_alarm_property_handle);

    ret = ssaps_start_service(g_server_id, g_service_handle);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start service fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ERRCODE_SLE_FAIL;
    }
    osal_printk("%s service added successfully.\r\n", SENSOR_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}

/* ── 连接回调 ── */

static void sle_sensor_report_connect_state_changed_cbk(uint16_t conn_id,
                                                         const sle_addr_t *addr,
                                                         sle_acb_state_t conn_state,
                                                         sle_pair_state_t pair_state,
                                                         sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);
    unused(disc_reason);

    switch (conn_state) {
        case SLE_ACB_STATE_CONNECTED:
            g_sle_conn_hdl = conn_id;
            g_connected = true;
            osal_printk("%s connected, conn_id: 0x%x\r\n", SENSOR_SERVER_LOG, conn_id);
            break;

        case SLE_ACB_STATE_DISCONNECTED:
            osal_printk("%s disconnected, conn_id: 0x%x\r\n", SENSOR_SERVER_LOG, conn_id);
            /* 停止定时器 */
            (void)osal_timer_stop(&g_sensor_report_timer);
            g_sle_conn_hdl = 0;
            g_connected = false;
            /* 重新启动广播 */
            (void)sle_start_announce(1);
            break;

        default:
            break;
    }
}

static void sle_sensor_report_pair_complete_cbk(uint16_t conn_id, const sle_addr_t *addr,
                                                 errcode_t status)
{
    unused(addr);

    if (status != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s pair failed, conn_id: 0x%x, status: 0x%x\r\n",
                    SENSOR_SERVER_LOG, conn_id, status);
        return;
    }

    osal_printk("%s pair complete, conn_id: 0x%x\r\n", SENSOR_SERVER_LOG, conn_id);

    /* 设置 MTU = 520 */
    ssap_exchange_info_t info = { .mtu_size = 520, .version = 1 };
    (void)ssaps_set_info(g_server_id, &info);

    /* 启动 1 秒周期定时器 */
    if (g_sensor_report_timer.timer == NULL) {
        g_sensor_report_timer.handler = sensor_report_timer_cb;
        g_sensor_report_timer.data = 0;
        g_sensor_report_timer.interval = SENSOR_REPORT_INTERVAL_MS;
        int timer_ret = osal_timer_init(&g_sensor_report_timer);
        if (timer_ret != 0) {
            osal_printk("%s osal_timer_init fail: %d\r\n", SENSOR_SERVER_LOG, timer_ret);
            return;
        }
    }
    int timer_ret = osal_timer_start(&g_sensor_report_timer);
    if (timer_ret != 0) {
        osal_printk("%s osal_timer_start fail: %d\r\n", SENSOR_SERVER_LOG, timer_ret);
        return;
    }
    osal_printk("%s 1s periodic timer started.\r\n", SENSOR_SERVER_LOG);
}

static void sle_sensor_report_read_rssi_cb(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    unused(conn_id);
    unused(rssi);
    unused(status);
}

static errcode_t sle_sensor_report_conn_register_cbks(void)
{
    sle_connection_callbacks_t conn_cbks = {0};
    conn_cbks.connect_state_changed_cb = sle_sensor_report_connect_state_changed_cbk;
    conn_cbks.pair_complete_cb         = sle_sensor_report_pair_complete_cbk;
    conn_cbks.read_rssi_cb             = sle_sensor_report_read_rssi_cb;

    errcode_t ret = sle_connection_register_callbacks(&conn_cbks);
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register connection callbacks fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }
    return ERRCODE_SLE_SUCCESS;
}

/* ── Public API ── */

errcode_t sle_sensor_report_server_init(void)
{
    errcode_t ret;

    ret = enable_sle();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s enable_sle fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_sensor_report_announce_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce cbks fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_sensor_report_conn_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register conn cbks fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_sensor_report_ssaps_register_cbks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register ssaps cbks fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_sensor_report_server_add();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s server add fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    ret = sle_sensor_report_server_adv_init();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s adv init fail: 0x%x\r\n", SENSOR_SERVER_LOG, ret);
        return ret;
    }

    osal_printk("%s init complete.\r\n", SENSOR_SERVER_LOG);
    return ERRCODE_SLE_SUCCESS;
}

uint16_t sle_sensor_report_server_is_connected(void)
{
    return (uint16_t)g_connected;
}
