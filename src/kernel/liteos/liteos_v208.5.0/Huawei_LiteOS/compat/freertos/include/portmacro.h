/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description : LiteOS adapt FreeRTOS.
 * Author : Huawei LiteOS Team
 * Create : 2026-01-05
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H


#include "los_typedef.h"
#include "los_hwi.h"
#include "los_printf.h"
#include "los_task.h"
#include "los_exc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------
 * Port specific definitions.
 *
 * The settings in this file configure FreeRTOS correctly for the
 * given hardware and compiler.
 *
 * These settings should not be altered.
 *-----------------------------------------------------------
 */

#if (configUSE_MALLOC_FAILED_HOOK == 1)
extern void vApplicationMallocFailedHook(void);
#endif
/* Type definitions. */
#define portCHAR char
#define portFLOAT float
#define portDOUBLE double
#define portLONG long
#define portSHORT short

#define portSTACK_TYPE           uint32_t
#define portBASE_TYPE            int32_t
#define portUBASE_TYPE           uint32_t
#define portMAX_DELAY            (TickType_t) 0xffffffffUL
/* 由目标 tick 频率统一换算毫秒，避免 SDK 假定 1 tick 恒等于 1 ms。 */
#ifndef portTICK_PERIOD_MS
#define portTICK_PERIOD_MS       ((TickType_t) (1000 / configTICK_RATE_HZ))
#endif

typedef portSTACK_TYPE StackType_t;
typedef INT32 BaseType_t;
typedef UINT32 UBaseType_t;
typedef UINT32 TickType_t;

#define portSET_INTERRUPT_MASK_FROM_ISR() LOS_IntLock()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x) LOS_IntRestore(x)

/*-----------------------------------------------------------*/

/* Architecture specifics. */
#define portBYTE_ALIGNMENT 8
/*-----------------------------------------------------------*/

/* Critical section management. */
#define portCRITICAL_NESTING_IN_TCB    0

#define portDISABLE_INTERRUPTS()                                   LOS_IntLock()
#define portENABLE_INTERRUPTS()                                    LOS_IntUnLock()

extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);
extern void vPortYieldFromISR(BaseType_t xHigherPriorityTaskWoken);

#define portENTER_CRITICAL() vPortEnterCritical()
#define portEXIT_CRITICAL()  vPortExitCritical()

#ifndef configASSERT
#define configASSERT(x)                                                        \
    do {                                                                       \
        if (!(x)) {                                                            \
            LOS_Panic("FreeRTOS assertion failed in %s:%d\n", __FILE__, __LINE__); \
        }                                                                      \
    } while (0)
#endif

#define portYIELD() LOS_TaskYield()

#define portYIELD_FROM_ISR(xHigherPriorityTaskWoken) vPortYieldFromISR(xHigherPriorityTaskWoken)
#define portEND_SWITCHING_ISR(xSwitchRequired) portYIELD_FROM_ISR(xSwitchRequired)

extern UINT32 g_taskUsed;

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */
