/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2023. All rights reserved.
 * Description:  PMP DRIVER header.
 */

#ifndef __PMP_CFG_H__
#define __PMP_CFG_H__

#include <stdint.h>

#if defined(CONFIG_SAMPLE_SUPPORT_PMP)
#define PMP_SAMPLE_REGION_INDEX 8U
#define PMP_SAMPLE_PROTECTED_SIZE 32U
#define PMP_SAMPLE_BUFFER_SIZE 64U

extern volatile uint8_t g_pmp_sample_buffer[PMP_SAMPLE_BUFFER_SIZE];
#endif

void pmp_enable(void);

#endif
