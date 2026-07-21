/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE connection parameter tuning sample entry. \n
 */
#include "common_def.h"
#include "soc_osal.h"
#include "app_init.h"

#if defined(CONFIG_SAMPLE_SUPPORT_SLE_CONN_PARAM_TUNING_SERVER_SAMPLE)
#include "sle_conn_param_tuning_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_CONN_PARAM_TUNING_CLIENT_SAMPLE)
#include "sle_conn_param_tuning_client.h"
#endif

#define SLE_CONN_PARAM_TASK_PRIO       28
#define SLE_CONN_PARAM_TASK_STACK_SIZE 0x1000

static void *sle_conn_param_tuning_task(const char *arg)
{
    unused(arg);
#if defined(CONFIG_SAMPLE_SUPPORT_SLE_CONN_PARAM_TUNING_SERVER_SAMPLE)
    osal_printk("[sle conn param server] task start\r\n");
    (void)sle_conn_param_tuning_server_init();
#elif defined(CONFIG_SAMPLE_SUPPORT_SLE_CONN_PARAM_TUNING_CLIENT_SAMPLE)
    osal_printk("[sle conn param client] task start\r\n");
    (void)sle_conn_param_tuning_client_init();
#endif
    return NULL;
}

static void sle_conn_param_tuning_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)sle_conn_param_tuning_task, 0,
        "SLEConnParam", SLE_CONN_PARAM_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, SLE_CONN_PARAM_TASK_PRIO);
    }
    osal_kthread_unlock();
}

app_run(sle_conn_param_tuning_entry);
