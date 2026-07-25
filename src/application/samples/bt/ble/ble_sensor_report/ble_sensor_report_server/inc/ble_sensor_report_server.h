/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 * Description: BLE Hello GATT server interface.
 */

#ifndef BLE_SENSOR_REPORT_SERVER_H
#define BLE_SENSOR_REPORT_SERVER_H

#include <stdint.h>
#include "errcode.h"

#define BLE_SENSOR_REPORT_SERVICE_UUID 0x3333
#define BLE_SENSOR_REPORT_DATA_UUID 0x3434
#define BLE_SENSOR_REPORT_NOTIFY_UUID 0x3435
#define BLE_SENSOR_REPORT_CCCD_UUID 0x2902
#define BLE_SENSOR_REPORT_PROPERTY_MAX_LEN 32

errcode_t ble_sensor_report_server_init(void);
void ble_sensor_report_server_report_loop(void);
errcode_t ble_sensor_report_server_send_notification(const uint8_t *data, uint16_t len);

#endif
