/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2024. All rights reserved.
 *
 * Description: SLE Sensor Report Client header. \n
 *
 * History: \n
 * 2024-06-01, Create file. \n
 */

#ifndef SLE_SENSOR_REPORT_CLIENT_H
#define SLE_SENSOR_REPORT_CLIENT_H

#include "sle_ssap_client.h"

void sle_sensor_report_client_init(ssapc_notification_callback notification_cb,
                                    ssapc_indication_callback indication_cb);
uint16_t sle_sensor_report_client_is_connected(void);
void sle_sensor_report_client_start_scan(void);

#endif /* SLE_SENSOR_REPORT_CLIENT_H */
