/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 * Description: AHT20 and BMP280 sensor module interface.
 */

#ifndef AHT20_BMP280_H
#define AHT20_BMP280_H

#include <stdint.h>
#include "errcode.h"

typedef struct {
    int32_t temperature_tenths_celsius;
    uint32_t humidity_tenths_percent;
    uint32_t pressure_pa;
} aht20_bmp280_data_t;

errcode_t aht20_bmp280_init(void);
errcode_t aht20_bmp280_read(aht20_bmp280_data_t *sensor_data);

#endif
