/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2024. All rights reserved.
 *
 * Description: SLE Sensor Report Server header. \n
 *
 * History: \n
 * 2024-06-01, Create file. \n
 */

#ifndef SLE_SENSOR_REPORT_SERVER_H
#define SLE_SENSOR_REPORT_SERVER_H

#include <stdint.h>
#include "errcode.h"

/* SSAP UUID 定义 */
#define SENSOR_SERVICE_UUID              0x5555
#define SENSOR_DATA_PROPERTY_UUID        0x5656   /* 常规数据 Property */
#define SENSOR_ALARM_PROPERTY_UUID       0x5757   /* 告警数据 Property */

/* 广播名称 */
#define SENSOR_SERVER_NAME               "sensor_server"

/* 定时器周期 (ms) */
#define SENSOR_REPORT_INTERVAL_MS        1000

/* 告警阈值 (x100) */
#define TEMP_ALARM_HIGH                  8000   /* 80.00C */
#define TEMP_ALARM_LOW                   (-1000) /* -10.00C */
#define HUMIDITY_ALARM_LOW               20     /* 20% */

/* Property 权限 */
#define SENSOR_PROPERTY_PERMISSIONS      (SSAP_PERMISSION_READ)

/* 常规数据 Property: READ + NOTIFY */
#define SENSOR_DATA_PROPERTY_OP_INDICATION \
    (SSAP_OPERATE_INDICATION_BIT_READ | \
     SSAP_OPERATE_INDICATION_BIT_NOTIFY)

#define SENSOR_DATA_PROPERTY_PERMISSIONS  (SSAP_PERMISSION_READ)

/* 告警数据 Property: READ + INDICATE (需 CCCD) */
#define SENSOR_ALARM_PROPERTY_OP_INDICATION \
    (SSAP_OPERATE_INDICATION_BIT_READ | \
     SSAP_OPERATE_INDICATION_BIT_INDICATE)

/* 传感器数据帧结构体 */
#define SENSOR_FRAME_TYPE_PERIODIC       0x01
#define SENSOR_FRAME_TYPE_ALARM          0x02

typedef struct {
    uint8_t  frame_type;
    uint8_t  sensor_count;
    uint32_t timestamp;
    int16_t  temperature;
    uint8_t  humidity;
    uint16_t light;
} __attribute__((packed)) sensor_data_frame_t;

/* Public API */
errcode_t sle_sensor_report_server_init(void);
uint16_t sle_sensor_report_server_is_connected(void);

#endif /* SLE_SENSOR_REPORT_SERVER_H */
