/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 * Description: BLE Hello sample entry.
 */

#include "app_init.h"
#include "soc_osal.h"
#include "errcode.h"

#if defined(CONFIG_SAMPLE_SUPPORT_BLE_SENSOR_REPORT_SERVER_SAMPLE)
#include "ble_sensor_report_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_SENSOR_REPORT_CLIENT_SAMPLE)
#include "ble_sensor_report_client.h"
#endif

#define BLE_SENSOR_REPORT_TASK_PRIO 26
#define BLE_SENSOR_REPORT_TASK_STACK_SIZE 0x2000

static int ble_sensor_report_task(const char *arg)
{
    (void)arg;
#if defined(CONFIG_SAMPLE_SUPPORT_BLE_SENSOR_REPORT_SERVER_SAMPLE)
    errcode_t ret = ble_sensor_report_server_init();
    if (ret != ERRCODE_SUCC) {
        return (int)ret;
    }
    ble_sensor_report_server_report_loop();
    return 0;
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_SENSOR_REPORT_CLIENT_SAMPLE)
    return (int)ble_sensor_report_client_init();
#else
    return 0;
#endif
}

static void ble_sensor_report_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ble_sensor_report_task, NULL, "ble_sensor_report",
                                      BLE_SENSOR_REPORT_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, BLE_SENSOR_REPORT_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

app_run(ble_sensor_report_entry);
