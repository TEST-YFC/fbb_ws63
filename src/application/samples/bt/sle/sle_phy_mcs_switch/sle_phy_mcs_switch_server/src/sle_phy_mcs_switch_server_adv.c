/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE PHY/MCS dynamic switch server announce implementation. \n
 */
#include "securec.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_device_discovery.h"
#include "sle_errcode.h"
#include "sle_phy_mcs_switch_server_adv.h"

#define SLE_PHY_MCS_SERVER_LOG          "[sle phy mcs server]"
#define SLE_PHY_MCS_ADV_DATA_MAX_LEN    251
#define SLE_PHY_MCS_ADV_INTERVAL        0xC8
#define SLE_PHY_MCS_CONN_INTERVAL       50
#define SLE_PHY_MCS_CONN_TIMEOUT        500
#define SLE_PHY_MCS_ADV_TX_POWER        10
#define SLE_PHY_MCS_LOCAL_NAME          "sle_phy_mcs_server"

enum {
    SLE_PHY_MCS_ADV_CHANNEL_MAP_DEFAULT = 0x07,
    SLE_PHY_MCS_DATA_DISCOVERY_LEVEL = 0x01,
    SLE_PHY_MCS_DATA_ACCESS_MODE = 0x02,
    SLE_PHY_MCS_DATA_COMPLETE_LOCAL_NAME = 0x0B,
    SLE_PHY_MCS_DATA_TX_POWER_LEVEL = 0x0C,
};

static uint16_t sle_phy_mcs_set_local_name(uint8_t *data, uint16_t max_len)
{
    const uint8_t local_name[] = SLE_PHY_MCS_LOCAL_NAME;
    uint8_t local_name_len = sizeof(local_name) - 1;

    if (max_len < (uint16_t)(local_name_len + 2U)) {
        return 0;
    }
    data[0] = local_name_len + 1U;
    data[1] = SLE_PHY_MCS_DATA_COMPLETE_LOCAL_NAME;
    if (memcpy_s(&data[2], max_len - 2U, local_name, local_name_len) != EOK) {
        return 0;
    }
    return (uint16_t)(local_name_len + 2U);
}

static uint16_t sle_phy_mcs_set_announce_data(uint8_t *data)
{
    sle_phy_mcs_adv_common_value_t discovery = {
        .length = sizeof(discovery) - 1U,
        .type = SLE_PHY_MCS_DATA_DISCOVERY_LEVEL,
        .value = SLE_ANNOUNCE_LEVEL_NORMAL,
    };
    sle_phy_mcs_adv_common_value_t access_mode = {
        .length = sizeof(access_mode) - 1U,
        .type = SLE_PHY_MCS_DATA_ACCESS_MODE,
        .value = 0,
    };
    uint16_t index = 0;

    if (memcpy_s(&data[index], SLE_PHY_MCS_ADV_DATA_MAX_LEN - index,
        &discovery, sizeof(discovery)) != EOK) {
        return 0;
    }
    index += sizeof(discovery);
    if (memcpy_s(&data[index], SLE_PHY_MCS_ADV_DATA_MAX_LEN - index,
        &access_mode, sizeof(access_mode)) != EOK) {
        return 0;
    }
    return (uint16_t)(index + sizeof(access_mode));
}

static uint16_t sle_phy_mcs_set_seek_response(uint8_t *data)
{
    sle_phy_mcs_adv_common_value_t tx_power = {
        .length = sizeof(tx_power) - 1U,
        .type = SLE_PHY_MCS_DATA_TX_POWER_LEVEL,
        .value = SLE_PHY_MCS_ADV_TX_POWER,
    };
    uint16_t index = 0;
    uint16_t name_len;

    if (memcpy_s(data, SLE_PHY_MCS_ADV_DATA_MAX_LEN, &tx_power, sizeof(tx_power)) != EOK) {
        return 0;
    }
    index += sizeof(tx_power);
    name_len = sle_phy_mcs_set_local_name(&data[index], SLE_PHY_MCS_ADV_DATA_MAX_LEN - index);
    return (name_len == 0) ? 0 : (uint16_t)(index + name_len);
}

static errcode_t sle_phy_mcs_set_announce_param(void)
{
    sle_announce_param_t param = {0};
    const uint8_t local_addr[SLE_ADDR_LEN] = {0x31, 0x32, 0x33, 0x34, 0x35, 0x36};

    param.announce_mode = SLE_ANNOUNCE_MODE_CONNECTABLE_SCANABLE;
    param.announce_handle = SLE_PHY_MCS_ADV_HANDLE;
    param.announce_gt_role = SLE_ANNOUNCE_ROLE_T_CAN_NEGO;
    param.announce_level = SLE_ANNOUNCE_LEVEL_NORMAL;
    param.announce_channel_map = SLE_PHY_MCS_ADV_CHANNEL_MAP_DEFAULT;
    param.announce_interval_min = SLE_PHY_MCS_ADV_INTERVAL;
    param.announce_interval_max = SLE_PHY_MCS_ADV_INTERVAL;
    param.conn_interval_min = SLE_PHY_MCS_CONN_INTERVAL;
    param.conn_interval_max = SLE_PHY_MCS_CONN_INTERVAL;
    param.conn_max_latency = 0;
    param.conn_supervision_timeout = SLE_PHY_MCS_CONN_TIMEOUT;
    param.announce_tx_power = 18;
    param.own_addr.type = 0;
    if (memcpy_s(param.own_addr.addr, SLE_ADDR_LEN, local_addr, SLE_ADDR_LEN) != EOK) {
        return ERRCODE_SLE_FAIL;
    }
    return sle_set_announce_param(param.announce_handle, &param);
}

static errcode_t sle_phy_mcs_set_announce_payload(void)
{
    sle_announce_data_t payload = {0};
    uint8_t announce_data[SLE_PHY_MCS_ADV_DATA_MAX_LEN] = {0};
    uint8_t seek_rsp_data[SLE_PHY_MCS_ADV_DATA_MAX_LEN] = {0};

    payload.announce_data_len = sle_phy_mcs_set_announce_data(announce_data);
    payload.seek_rsp_data_len = sle_phy_mcs_set_seek_response(seek_rsp_data);
    if ((payload.announce_data_len == 0) || (payload.seek_rsp_data_len == 0)) {
        return ERRCODE_SLE_FAIL;
    }
    payload.announce_data = announce_data;
    payload.seek_rsp_data = seek_rsp_data;
    return sle_set_announce_data(SLE_PHY_MCS_ADV_HANDLE, &payload);
}

static void sle_phy_mcs_announce_enable_cb(uint32_t announce_id, errcode_t status)
{
    osal_printk("%s announce enabled, id=%u, status=0x%x\r\n",
        SLE_PHY_MCS_SERVER_LOG, announce_id, status);
}

static void sle_phy_mcs_announce_disable_cb(uint32_t announce_id, errcode_t status)
{
    osal_printk("%s announce disabled, id=%u, status=0x%x\r\n",
        SLE_PHY_MCS_SERVER_LOG, announce_id, status);
}

errcode_t sle_phy_mcs_switch_announce_register_callbacks(void)
{
    sle_announce_seek_callbacks_t callbacks = {0};
    callbacks.announce_enable_cb = sle_phy_mcs_announce_enable_cb;
    callbacks.announce_disable_cb = sle_phy_mcs_announce_disable_cb;
    return sle_announce_seek_register_callbacks(&callbacks);
}

errcode_t sle_phy_mcs_switch_server_announce_start(void)
{
    errcode_t ret = sle_phy_mcs_set_announce_param();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set announce param failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_phy_mcs_set_announce_payload();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s set announce data failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_start_announce(SLE_PHY_MCS_ADV_HANDLE);
    if (ret == ERRCODE_SLE_SUCCESS) {
        osal_printk("%s start announce, name=%s\r\n", SLE_PHY_MCS_SERVER_LOG, SLE_PHY_MCS_LOCAL_NAME);
    }
    return ret;
}
