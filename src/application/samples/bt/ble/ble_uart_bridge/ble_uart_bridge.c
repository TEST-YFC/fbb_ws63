/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 * Description: BLE UART bridge sample entry and UART worker.
 */

#include "app_init.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"

#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
#include "ble_uart_bridge_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_CLIENT_SAMPLE)
#include "ble_uart_bridge_client.h"
#endif

#define BLE_UART_BRIDGE_TASK_PRIO 26
#define BLE_UART_BRIDGE_TASK_STACK_SIZE 0x2000
#define BLE_UART_BRIDGE_UART_BUS UART_BUS_1
#define BLE_UART_BRIDGE_UART_TX_PIN 17
#define BLE_UART_BRIDGE_UART_RX_PIN 18
#define BLE_UART_BRIDGE_UART_TX_MODE PIN_MODE_1
#define BLE_UART_BRIDGE_UART_RX_MODE PIN_MODE_1
#define BLE_UART_BRIDGE_UART_BAUDRATE 115200
#define BLE_UART_BRIDGE_UART_BUFFER_SIZE 244
#define BLE_UART_BRIDGE_WORKER_DELAY_MS 5

static uint8_t g_uart_driver_buffer[BLE_UART_BRIDGE_UART_BUFFER_SIZE];
static uint8_t g_uart_pending_buffer[BLE_UART_BRIDGE_UART_BUFFER_SIZE];
static volatile uint16_t g_uart_pending_length;

static void ble_uart_bridge_uart_rx_cb(const void *buffer, uint16_t length, bool error)
{
    const uint8_t *source = (const uint8_t *)buffer;
    uint16_t index;

    if (error || source == NULL || length == 0 || length > sizeof(g_uart_pending_buffer) ||
        g_uart_pending_length != 0) {
        return;
    }
    for (index = 0; index < length; index++) {
        g_uart_pending_buffer[index] = source[index];
    }
    g_uart_pending_length = length;
}

static errcode_t ble_uart_bridge_uart_init(void)
{
    uart_attr_t attr = {.baud_rate = BLE_UART_BRIDGE_UART_BAUDRATE,
                        .data_bits = UART_DATA_BIT_8,
                        .stop_bits = UART_STOP_BIT_1,
                        .parity = UART_PARITY_NONE};
    uart_pin_config_t pins = {.tx_pin = BLE_UART_BRIDGE_UART_TX_PIN,
                              .rx_pin = BLE_UART_BRIDGE_UART_RX_PIN,
                              .cts_pin = PIN_NONE,
                              .rts_pin = PIN_NONE};
    uart_buffer_config_t buffer = {.rx_buffer = g_uart_driver_buffer, .rx_buffer_size = sizeof(g_uart_driver_buffer)};
    errcode_t ret;

    ret = uapi_pin_set_mode(BLE_UART_BRIDGE_UART_TX_PIN, BLE_UART_BRIDGE_UART_TX_MODE);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = uapi_pin_set_mode(BLE_UART_BRIDGE_UART_RX_PIN, BLE_UART_BRIDGE_UART_RX_MODE);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    uapi_uart_deinit(BLE_UART_BRIDGE_UART_BUS);
    ret = uapi_uart_init(BLE_UART_BRIDGE_UART_BUS, &pins, &attr, NULL, &buffer);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = uapi_uart_register_rx_callback(BLE_UART_BRIDGE_UART_BUS, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1,
                                         ble_uart_bridge_uart_rx_cb);
    if (ret == ERRCODE_SUCC) {
        osal_printk("[ble uart bridge] UART1 ready: TX=GPIO17 RX=GPIO18 115200 8N1\r\n");
    }
    return ret;
}

static int ble_uart_bridge_task(const char *arg)
{
    uint8_t data[BLE_UART_BRIDGE_UART_BUFFER_SIZE];
    uint16_t length;
    uint16_t index;
    errcode_t ret;

    (void)arg;
    ret = ble_uart_bridge_uart_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("[ble uart bridge] UART init failed: 0x%x\r\n", ret);
        return (int)ret;
    }
#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
    ret = ble_uart_bridge_server_init();
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_CLIENT_SAMPLE)
    ret = ble_uart_bridge_client_init();
#else
    return 0;
#endif
    if (ret != ERRCODE_SUCC) {
        return (int)ret;
    }

    while (1) {
        length = g_uart_pending_length;
        if (length == 0) {
            osal_msleep(BLE_UART_BRIDGE_WORKER_DELAY_MS);
            continue;
        }
        for (index = 0; index < length; index++) {
            data[index] = g_uart_pending_buffer[index];
        }
        g_uart_pending_length = 0;
#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
        ret = ble_uart_bridge_server_send_notification(data, length);
#else
        ret = ble_uart_bridge_client_send_write(data, length);
#endif
        osal_printk("[ble uart bridge] UART RX -> BLE, bytes=%u, ret=0x%x\r\n", length, ret);
    }
}

static void ble_uart_bridge_entry(void)
{
    osal_task *task_handle = NULL;

    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ble_uart_bridge_task, NULL, "ble_uart_bridge",
                                      BLE_UART_BRIDGE_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, BLE_UART_BRIDGE_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

app_run(ble_uart_bridge_entry);
