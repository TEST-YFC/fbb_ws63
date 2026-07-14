/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026. All rights reserved.
 *
 * Description: BLE HID Button sample — physical button → HID keyboard keycode.
 *              GPIO button drives a single HID keycode to a connected PC.
 *
 * Configuration (Kconfig):
 *   CONFIG_BLE_HID_BTN_PIN       - GPIO pin (default 13)
 *   CONFIG_BLE_HID_BTN_KEYCODE   - HID keycode (default 0x4E = Page Down)
 *   CONFIG_BLE_HID_BTN_LONGPRESS - 1=auto-repeat on hold, 0=single-fire
 */

#include <stdint.h>
#include <stdbool.h>
#include "pinctrl.h"
#include "gpio.h"
#include "cmsis_os2.h"
#include "soc_osal.h"
#include "osal_debug.h"
#include "app_init.h"
#include "../inc/ble_hid_btn.h"
#include "../inc/ble_hid_adv.h"

#define BLE_HID_SAMPLE_TAG "[ble_hid_btn_sample]"

/* ---- Kconfig defaults ---- */
#ifndef CONFIG_BLE_HID_BTN_PIN
#define CONFIG_BLE_HID_BTN_PIN     13
#endif
#ifndef CONFIG_BLE_HID_BTN_KEYCODE
#define CONFIG_BLE_HID_BTN_KEYCODE  0x4E   /* Page Down */
#endif
#ifndef CONFIG_BLE_HID_BTN_LONGPRESS
#define CONFIG_BLE_HID_BTN_LONGPRESS 1
#endif

/* ---- Timing ---- */
#define POLL_INTERVAL_MS    20     /* 50 Hz poll rate */
#define DEBOUNCE_CNT         2     /* 2 consecutive reads for debounce */
#define LONGPRESS_MS       500     /* hold threshold before auto-repeat */
#define REPEAT_INTERVAL_MS  80     /* repeat interval during hold */

/* ---- Report constants ---- */
static const hid_kb_report_t REPORT_RELEASE = { 0, 0, { 0, 0, 0, 0, 0, 0 } };

/* ==========================================================
 *  Button task
 * ========================================================== */

static int hid_btn_task(const char *arg)
{
    uint8_t  pin = (uint8_t)CONFIG_BLE_HID_BTN_PIN;
    uint8_t  keycode = (uint8_t)CONFIG_BLE_HID_BTN_KEYCODE;
    hid_kb_report_t report;
    bool     pressed = false;
    bool     last_stable = false;      /* true = LOW = pressed (debounced) */
    uint8_t  debounce = 0;
    uint32_t press_start_tick = 0;
    bool     sent_repeat = false;

    unused(arg);

    /* GPIO init: input with pull-up, button to GND */
    uapi_pin_set_mode(pin, HAL_PIO_FUNC_GPIO);
    uapi_gpio_set_dir(pin, GPIO_DIRECTION_INPUT);
    uapi_pin_set_pull(pin, PIN_PULL_TYPE_UP);

    osal_printk("%s task started, pin=%u keycode=0x%02X\r\n",
                BLE_HID_SAMPLE_TAG, pin, keycode);

    while (1) {
        osal_msleep(POLL_INTERVAL_MS);

        bool raw = (uapi_gpio_get_val(pin) == GPIO_LEVEL_LOW); /* active low */

        /* Debounce */
        if (raw == last_stable) {
            debounce = 0;
        } else {
            debounce++;
            if (debounce >= DEBOUNCE_CNT) {
                last_stable = raw;
                debounce = 0;
            }
        }

        if (last_stable && !pressed) {
            /* ---- Edge: released → pressed ---- */
            pressed = true;
            press_start_tick = (uint32_t)osKernelGetTickCount();
            sent_repeat = false;

            (void)memset_s(&report, sizeof(report), 0, sizeof(report));
            report.keys[0] = keycode;
            ble_hid_btn_send_report(&report);
            osal_printk("%s press key=0x%02X\r\n", BLE_HID_SAMPLE_TAG, keycode);

        } else if (last_stable && pressed) {
            /* ---- Held: auto-repeat (if enabled) ---- */
#if CONFIG_BLE_HID_BTN_LONGPRESS
            uint32_t elapsed = (uint32_t)osKernelGetTickCount() - press_start_tick;
            if (elapsed >= LONGPRESS_MS && !sent_repeat) {
                /* First repeat */
                ble_hid_btn_send_report(&REPORT_RELEASE);
                osal_msleep(10);
                (void)memset_s(&report, sizeof(report), 0, sizeof(report));
                report.keys[0] = keycode;
                ble_hid_btn_send_report(&report);
                sent_repeat = true;
                osal_printk("%s repeat key=0x%02X\r\n", BLE_HID_SAMPLE_TAG, keycode);
            }
#endif
        } else if (!last_stable && pressed) {
            /* ---- Edge: pressed → released ---- */
            pressed = false;
            ble_hid_btn_send_report(&REPORT_RELEASE);
            osal_printk("%s release\r\n", BLE_HID_SAMPLE_TAG);
        }
    }
    return 0;
}

/* ==========================================================
 *  Main entry
 * ========================================================== */

#define HID_MAIN_TASK_STACK  0x1000
#define HID_MAIN_TASK_PRIO   26
#define HID_BTN_TASK_STACK   0x400
#define HID_BTN_TASK_PRIO    30

static int hid_main_task(const char *arg)
{
    unused(arg);
    osal_printk("%s main task start\r\n", BLE_HID_SAMPLE_TAG);

    /* Wait for scheduler + BLE stack to be ready */
    osal_msleep(3000);

    /* 1. Init HID service + start advertising */
    if (ble_hid_btn_init() != ERRCODE_SUCC) {
        osal_printk("%s HID init fail\r\n", BLE_HID_SAMPLE_TAG);
        return -1;
    }
    ble_hid_adv_start();

    /* 2. Spawn button-polling task */
    osal_kthread_lock();
    osal_task *btn = osal_kthread_create((osal_kthread_handler)hid_btn_task,
                                          NULL, "hid_btn", HID_BTN_TASK_STACK);
    if (btn != NULL) {
        osal_kthread_set_priority(btn, HID_BTN_TASK_PRIO);
        osal_kfree(btn);
    }
    osal_kthread_unlock();

    osal_printk("%s ready\r\n", BLE_HID_SAMPLE_TAG);
    return 0;
}

static void ble_hid_btn_sample_entry(void)
{
    osal_printk("%s entry\r\n", BLE_HID_SAMPLE_TAG);

    osal_kthread_lock();
    osal_task *task = osal_kthread_create((osal_kthread_handler)hid_main_task,
                                           NULL, "hid_main", HID_MAIN_TASK_STACK);
    if (task != NULL) {
        osal_kthread_set_priority(task, HID_MAIN_TASK_PRIO);
        osal_kfree(task);
    }
    osal_kthread_unlock();
}

app_run(ble_hid_btn_sample_entry);
