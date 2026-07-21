/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE PHY/MCS dynamic switch sample entry. \n
 */
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_PHY_MCS_SWITCH_SERVER_SAMPLE)
#include "sle_phy_mcs_switch_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_PHY_MCS_SWITCH_CLIENT_SAMPLE)
#include "sle_phy_mcs_switch_client.h"
#endif

#define SLE_PHY_MCS_TASK_PRIO       28
#define SLE_PHY_MCS_TASK_STACK_SIZE 0x1000

static void *sle_phy_mcs_switch_task(const char *arg)
{
    unused(arg);
#if defined(CONFIG_SAMPLE_SUPPORT_SLE_PHY_MCS_SWITCH_SERVER_SAMPLE)
    osal_printk("[sle phy mcs server] task start\r\n");
    (void)sle_phy_mcs_switch_server_init();
#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_PHY_MCS_SWITCH_CLIENT_SAMPLE)
    osal_printk("[sle phy mcs client] task start\r\n");
    (void)sle_phy_mcs_switch_client_init();
#endif
    return NULL;
}

static void sle_phy_mcs_switch_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)sle_phy_mcs_switch_task, 0,
        "SLEPhyMcs", SLE_PHY_MCS_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, SLE_PHY_MCS_TASK_PRIO);
    }
    osal_kthread_unlock();
}

app_run(sle_phy_mcs_switch_entry);
