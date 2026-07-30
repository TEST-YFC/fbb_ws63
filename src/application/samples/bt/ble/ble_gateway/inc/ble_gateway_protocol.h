/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026-2026.
 * Description: Binary protocol shared by the BLE gateway roles.
 */

#ifndef BLE_GATEWAY_PROTOCOL_H
#define BLE_GATEWAY_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#define BLE_GATEWAY_PROTOCOL_VERSION 1U
#define BLE_GATEWAY_NODE_ID_DEFAULT 1U
#define BLE_GATEWAY_REPORT_SIZE 14U
#define BLE_GATEWAY_COMMAND_SIZE 6U
#define BLE_GATEWAY_COMMAND_SET_INTERVAL 1U
#define BLE_GATEWAY_MIN_INTERVAL_S 5U
#define BLE_GATEWAY_MAX_INTERVAL_S 3600U
#define BLE_GATEWAY_DEFAULT_INTERVAL_S 10U

typedef struct {
    uint8_t version;
    uint8_t node_id;
    uint32_t sequence;
    int16_t temperature_tenths_celsius;
    uint16_t humidity_tenths_percent;
    uint32_t pressure_pa;
} ble_gateway_report_t;

static inline uint16_t ble_gateway_get_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t ble_gateway_get_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static inline void ble_gateway_put_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static inline void ble_gateway_put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static inline void ble_gateway_encode_report(uint8_t output[BLE_GATEWAY_REPORT_SIZE],
                                             const ble_gateway_report_t *report)
{
    output[0] = report->version;
    output[1] = report->node_id;
    ble_gateway_put_u32_le(&output[2], report->sequence);
    ble_gateway_put_u16_le(&output[6], (uint16_t)report->temperature_tenths_celsius);
    ble_gateway_put_u16_le(&output[8], report->humidity_tenths_percent);
    ble_gateway_put_u32_le(&output[10], report->pressure_pa);
}

static inline bool ble_gateway_decode_report(const uint8_t *data, uint16_t length, ble_gateway_report_t *report)
{
    if (data == NULL || report == NULL || length != BLE_GATEWAY_REPORT_SIZE ||
        data[0] != BLE_GATEWAY_PROTOCOL_VERSION) {
        return false;
    }
    report->version = data[0];
    report->node_id = data[1];
    report->sequence = ble_gateway_get_u32_le(&data[2]);
    report->temperature_tenths_celsius = (int16_t)ble_gateway_get_u16_le(&data[6]);
    report->humidity_tenths_percent = ble_gateway_get_u16_le(&data[8]);
    report->pressure_pa = ble_gateway_get_u32_le(&data[10]);
    return report->node_id == BLE_GATEWAY_NODE_ID_DEFAULT && report->sequence != 0U &&
           report->temperature_tenths_celsius >= -400 && report->temperature_tenths_celsius <= 850 &&
           report->humidity_tenths_percent <= 1000U && report->pressure_pa >= 30000U &&
           report->pressure_pa <= 110000U;
}

static inline void ble_gateway_encode_interval_command(uint8_t output[BLE_GATEWAY_COMMAND_SIZE], uint32_t interval_s)
{
    output[0] = BLE_GATEWAY_PROTOCOL_VERSION;
    output[1] = BLE_GATEWAY_COMMAND_SET_INTERVAL;
    ble_gateway_put_u32_le(&output[2], interval_s);
}

static inline bool ble_gateway_decode_interval_command(const uint8_t *data, uint16_t length, uint32_t *interval_s)
{
    uint32_t value;
    if (data == NULL || interval_s == NULL || length != BLE_GATEWAY_COMMAND_SIZE ||
        data[0] != BLE_GATEWAY_PROTOCOL_VERSION || data[1] != BLE_GATEWAY_COMMAND_SET_INTERVAL) {
        return false;
    }
    value = ble_gateway_get_u32_le(&data[2]);
    if (value < BLE_GATEWAY_MIN_INTERVAL_S || value > BLE_GATEWAY_MAX_INTERVAL_S) {
        return false;
    }
    *interval_s = value;
    return true;
}

#endif
