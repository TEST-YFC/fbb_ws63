/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026. All rights reserved.
 *
 * Description: SLE connection parameter tuning server announce interface. \n
 */
#ifndef SLE_CONN_PARAM_TUNING_SERVER_ADV_H
#define SLE_CONN_PARAM_TUNING_SERVER_ADV_H

#include <stdint.h>
#include "errcode.h"

#define SLE_CONN_PARAM_ADV_HANDLE 1

typedef struct {
    uint8_t length;
    uint8_t type;
    uint8_t value;
} sle_conn_param_adv_common_value_t;

errcode_t sle_conn_param_tuning_announce_register_callbacks(void);
errcode_t sle_conn_param_tuning_server_announce_start(void);

#endif
