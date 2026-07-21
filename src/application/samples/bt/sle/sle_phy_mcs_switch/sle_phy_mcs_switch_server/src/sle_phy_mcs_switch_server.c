/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE PHY/MCS dynamic switch server implementation. \n
 */
#include <stdbool.h>
#include "common_def.h"
#include "soc_osal.h"
#include "sle_common.h"
#include "sle_errcode.h"
#include "sle_connection_manager.h"
#include "sle_device_discovery.h"
#include "sle_phy_mcs_switch_server_adv.h"
#include "sle_phy_mcs_switch_server.h"

#define SLE_PHY_MCS_SERVER_LOG         "[sle phy mcs server]"
#define SLE_PHY_MCS_SAMPLE_INTERVAL_MS 1000
#define SLE_PHY_MCS_AVERAGE_COUNT      4
#define SLE_PHY_MCS_CONFIRM_WINDOWS    2
#define SLE_PHY_MCS_ROBUST_TO_BALANCED (-70)
#define SLE_PHY_MCS_BALANCED_TO_ROBUST (-78)
#define SLE_PHY_MCS_BALANCED_TO_FAST   (-50)
#define SLE_PHY_MCS_FAST_TO_BALANCED   (-62)
#define SLE_PHY_MCS_ADAPT_TASK_PRIO    27
#define SLE_PHY_MCS_ADAPT_STACK_SIZE   0x1000

typedef enum {
    SLE_PHY_MCS_PROFILE_INVALID = 0,
    SLE_PHY_MCS_PROFILE_ROBUST,
    SLE_PHY_MCS_PROFILE_BALANCED,
    SLE_PHY_MCS_PROFILE_FAST,
} sle_phy_mcs_profile_id_t;

typedef struct {
    const char *name;
    uint8_t phy;
    uint8_t mcs;
    uint8_t pilot_density;
} sle_phy_mcs_profile_t;

static const sle_phy_mcs_profile_t g_profiles[] = {
    {"invalid", SLE_PHY_1M, SLE_MCS_00, SLE_PHY_PILOT_DENSITY_16_TO_1},
    {"robust", SLE_PHY_1M, SLE_MCS_00, SLE_PHY_PILOT_DENSITY_16_TO_1},
    {"balanced", SLE_PHY_2M, SLE_MCS_04, SLE_PHY_PILOT_DENSITY_16_TO_1},
    {"fast", SLE_PHY_4M, SLE_MCS_10, SLE_PHY_PILOT_DENSITY_16_TO_1},
};

static volatile bool g_connected = false;
static volatile bool g_phy_update_pending = false;
static uint16_t g_conn_id = 0;
static sle_phy_mcs_profile_id_t g_current_profile = SLE_PHY_MCS_PROFILE_INVALID;
static sle_phy_mcs_profile_id_t g_target_profile = SLE_PHY_MCS_PROFILE_INVALID;
static sle_phy_mcs_profile_id_t g_candidate_profile = SLE_PHY_MCS_PROFILE_INVALID;
static uint8_t g_candidate_windows = 0;
static int32_t g_rssi_sum = 0;
static uint8_t g_rssi_count = 0;

static void sle_phy_mcs_reset_adaptation(void)
{
    g_phy_update_pending = false;
    g_current_profile = SLE_PHY_MCS_PROFILE_INVALID;
    g_target_profile = SLE_PHY_MCS_PROFILE_INVALID;
    g_candidate_profile = SLE_PHY_MCS_PROFILE_INVALID;
    g_candidate_windows = 0;
    g_rssi_sum = 0;
    g_rssi_count = 0;
}

static sle_phy_mcs_profile_id_t sle_phy_mcs_select_profile(int8_t average_rssi)
{
    switch (g_current_profile) {
        case SLE_PHY_MCS_PROFILE_ROBUST:
            return (average_rssi >= SLE_PHY_MCS_ROBUST_TO_BALANCED) ?
                SLE_PHY_MCS_PROFILE_BALANCED : SLE_PHY_MCS_PROFILE_ROBUST;
        case SLE_PHY_MCS_PROFILE_BALANCED:
            if (average_rssi >= SLE_PHY_MCS_BALANCED_TO_FAST) {
                return SLE_PHY_MCS_PROFILE_FAST;
            }
            if (average_rssi <= SLE_PHY_MCS_BALANCED_TO_ROBUST) {
                return SLE_PHY_MCS_PROFILE_ROBUST;
            }
            return SLE_PHY_MCS_PROFILE_BALANCED;
        case SLE_PHY_MCS_PROFILE_FAST:
            return (average_rssi <= SLE_PHY_MCS_FAST_TO_BALANCED) ?
                SLE_PHY_MCS_PROFILE_BALANCED : SLE_PHY_MCS_PROFILE_FAST;
        default:
            return SLE_PHY_MCS_PROFILE_ROBUST;
    }
}

static errcode_t sle_phy_mcs_request_profile(sle_phy_mcs_profile_id_t profile_id)
{
    const sle_phy_mcs_profile_t *profile = &g_profiles[profile_id];
    sle_set_phy_t phy_param = {
        .tx_format = SLE_RADIO_FRAME_2,
        .rx_format = SLE_RADIO_FRAME_2,
        .tx_phy = profile->phy,
        .rx_phy = profile->phy,
        .tx_pilot_density = profile->pilot_density,
        .rx_pilot_density = profile->pilot_density,
        .g_feedback = 0,
        .t_feedback = 0,
    };
    errcode_t ret;

    if (!g_connected || g_phy_update_pending || (profile_id == SLE_PHY_MCS_PROFILE_INVALID)) {
        return ERRCODE_INVALID_PARAM;
    }
    g_target_profile = profile_id;
    g_phy_update_pending = true;
    osal_printk("%s switch request: %s -> %s, phy=%uM, mcs=%u\r\n", SLE_PHY_MCS_SERVER_LOG,
        g_profiles[g_current_profile].name, profile->name, (uint8_t)(1U << profile->phy), profile->mcs);
    ret = sle_set_phy_param(g_conn_id, &phy_param);
    if (ret != ERRCODE_SLE_SUCCESS) {
        g_phy_update_pending = false;
        osal_printk("%s sle_set_phy_param failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
    }
    return ret;
}

static void sle_phy_mcs_state_changed_cb(uint16_t conn_id, const sle_addr_t *addr,
    sle_acb_state_t conn_state, sle_pair_state_t pair_state, sle_disc_reason_t disc_reason)
{
    unused(addr);
    unused(pair_state);

    if (conn_state == SLE_ACB_STATE_CONNECTED) {
        g_conn_id = conn_id;
        g_connected = true;
        sle_phy_mcs_reset_adaptation();
        osal_printk("%s connected, conn_id=0x%02x\r\n", SLE_PHY_MCS_SERVER_LOG, conn_id);
    } else if (conn_state == SLE_ACB_STATE_DISCONNECTED) {
        g_connected = false;
        sle_phy_mcs_reset_adaptation();
        osal_printk("%s disconnected, reason=0x%x, restart announce\r\n",
            SLE_PHY_MCS_SERVER_LOG, disc_reason);
        (void)sle_start_announce(SLE_PHY_MCS_ADV_HANDLE);
    }
}

static void sle_phy_mcs_set_phy_cb(uint16_t conn_id, errcode_t status, const sle_set_phy_t *param)
{
    const sle_phy_mcs_profile_t *target = &g_profiles[g_target_profile];
    errcode_t ret;

    if ((conn_id != g_conn_id) || !g_phy_update_pending || (param == NULL)) {
        return;
    }
    osal_printk("%s PHY complete: status=0x%x, tx_phy=%uM, rx_phy=%uM\r\n",
        SLE_PHY_MCS_SERVER_LOG, status, (uint8_t)(1U << param->tx_phy), (uint8_t)(1U << param->rx_phy));
    if (status != ERRCODE_SLE_SUCCESS) {
        g_phy_update_pending = false;
        g_candidate_windows = 0;
        return;
    }
    ret = sle_set_mcs(conn_id, target->mcs);
    if (ret == ERRCODE_SLE_SUCCESS) {
        g_current_profile = g_target_profile;
        osal_printk("%s switch complete: profile=%s, phy=%uM, mcs=%u, status=0x%x\r\n",
            SLE_PHY_MCS_SERVER_LOG, target->name, (uint8_t)(1U << target->phy), target->mcs, ret);
    } else {
        osal_printk("%s sle_set_mcs failed: profile=%s, status=0x%x\r\n",
            SLE_PHY_MCS_SERVER_LOG, target->name, ret);
    }
    g_phy_update_pending = false;
    g_candidate_windows = 0;
}

static void sle_phy_mcs_read_rssi_cb(uint16_t conn_id, int8_t rssi, errcode_t status)
{
    int8_t average_rssi;
    sle_phy_mcs_profile_id_t selected;

    if ((conn_id != g_conn_id) || (status != ERRCODE_SLE_SUCCESS) || !g_connected) {
        osal_printk("%s RSSI read failed: status=0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, status);
        return;
    }
    g_rssi_sum += rssi;
    g_rssi_count++;
    if (g_rssi_count < SLE_PHY_MCS_AVERAGE_COUNT) {
        return;
    }
    average_rssi = (int8_t)(g_rssi_sum / SLE_PHY_MCS_AVERAGE_COUNT);
    g_rssi_sum = 0;
    g_rssi_count = 0;
    selected = sle_phy_mcs_select_profile(average_rssi);
    osal_printk("%s RSSI window: average=%d dBm, current=%s, selected=%s\r\n",
        SLE_PHY_MCS_SERVER_LOG, average_rssi, g_profiles[g_current_profile].name, g_profiles[selected].name);

    if (selected == g_current_profile) {
        g_candidate_profile = selected;
        g_candidate_windows = 0;
        return;
    }
    if (selected != g_candidate_profile) {
        g_candidate_profile = selected;
        g_candidate_windows = 1;
    } else if (g_candidate_windows < SLE_PHY_MCS_CONFIRM_WINDOWS) {
        g_candidate_windows++;
    }
    osal_printk("%s candidate=%s, confirm=%u/%u\r\n", SLE_PHY_MCS_SERVER_LOG,
        g_profiles[g_candidate_profile].name, g_candidate_windows, SLE_PHY_MCS_CONFIRM_WINDOWS);
    if ((g_candidate_windows >= SLE_PHY_MCS_CONFIRM_WINDOWS) && !g_phy_update_pending) {
        (void)sle_phy_mcs_request_profile(g_candidate_profile);
    }
}

static errcode_t sle_phy_mcs_register_connection_callbacks(void)
{
    sle_connection_callbacks_t callbacks = {0};
    callbacks.connect_state_changed_cb = sle_phy_mcs_state_changed_cb;
    callbacks.read_rssi_cb = sle_phy_mcs_read_rssi_cb;
    callbacks.set_phy_cb = sle_phy_mcs_set_phy_cb;
    return sle_connection_register_callbacks(&callbacks);
}

static void *sle_phy_mcs_adapt_task(const char *arg)
{
    unused(arg);
    while (1) {
        if (g_connected && !g_phy_update_pending) {
            if (g_current_profile == SLE_PHY_MCS_PROFILE_INVALID) {
                (void)sle_phy_mcs_request_profile(SLE_PHY_MCS_PROFILE_ROBUST);
            } else {
                errcode_t ret = sle_read_remote_device_rssi(g_conn_id);
                if (ret != ERRCODE_SLE_SUCCESS) {
                    osal_printk("%s RSSI request failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
                }
            }
        }
        (void)osal_msleep(SLE_PHY_MCS_SAMPLE_INTERVAL_MS);
    }
    return NULL;
}

static errcode_t sle_phy_mcs_start_adapt_task(void)
{
    osal_task *task_handle;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)sle_phy_mcs_adapt_task, 0,
        "SLEPhyAdapt", SLE_PHY_MCS_ADAPT_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, SLE_PHY_MCS_ADAPT_TASK_PRIO);
    }
    osal_kthread_unlock();
    return (task_handle == NULL) ? ERRCODE_MALLOC : ERRCODE_SUCC;
}

errcode_t sle_phy_mcs_switch_server_init(void)
{
    errcode_t ret = enable_sle();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s enable SLE failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_phy_mcs_switch_announce_register_callbacks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register announce callbacks failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_phy_mcs_register_connection_callbacks();
    if (ret != ERRCODE_SLE_SUCCESS) {
        osal_printk("%s register connection callbacks failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    ret = sle_phy_mcs_start_adapt_task();
    if (ret != ERRCODE_SUCC) {
        osal_printk("%s create adaptation task failed: 0x%x\r\n", SLE_PHY_MCS_SERVER_LOG, ret);
        return ret;
    }
    osal_printk("%s policy: robust<-78/-70, balanced<-62/-50, 4 samples x 2 windows\r\n",
        SLE_PHY_MCS_SERVER_LOG);
    return sle_phy_mcs_switch_server_announce_start();
}
