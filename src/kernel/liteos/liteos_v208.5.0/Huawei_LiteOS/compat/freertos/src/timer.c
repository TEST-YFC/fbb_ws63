/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description : LiteOS adapt FreeRTOS Timer.
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


#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "freertos_compat_pri.h"

/* LiteOS Kernel Header Files */
#include "los_swtmr_pri.h"
#include "los_memory.h"
#include "los_task.h"
#include "los_spinlock.h"
#include "securec.h"

typedef void (* TimerCallbackFunction_t)(TimerHandle_t xTimer);

typedef struct tmrTimerControl {
    const char *pcTimerName;
    void *pvTimerID;
    TimerCallbackFunction_t pxCallback;
    TickType_t xTimerPeriodInTicks;
    UINT16 usSwTmrID;
    UINT8  ucStatus;
} xTIMER;

typedef xTIMER Timer_t;

#define FR_SWTMR_LOCK(state)   LOS_SpinLockSave(&g_swtmrSpin, &(state))
#define FR_SWTMR_UNLOCK(state) LOS_SpinUnlockRestore(&g_swtmrSpin, (state))

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 在编译期阻止 Timer_t 增长导致 StaticTimer_t 调用者内存越界。 */
typedef char StaticTimerBufferSizeCheck[(sizeof(StaticTimer_t) >= sizeof(Timer_t)) ? 1 : -1];
#endif

#define TIMER_STATUS_IS_ACTIVE                  (0x01U)
#define TIMER_STATUS_IS_STATICALLY_ALLOCATED    (0x02U)
#define TIMER_STATUS_IS_AUTORELOAD              (0x04U)
#define TIMER_STATUS_DELETE_PENDING             (0x08U)
#if (INCLUDE_xTimerPendFunctionCall == 1)
#if (configTIMER_QUEUE_LENGTH < 1)
#error "configTIMER_QUEUE_LENGTH must be greater than zero"
#endif
typedef struct {
    PendedFunction_t function;
    VOID *parameter1;
    uint32_t parameter2;
} PendedCall_t;

/* A single software timer drains this ring in FIFO order, matching the
 * asynchronous execution contract without allocating one timer per call. */
static PendedCall_t g_pendedCalls[configTIMER_QUEUE_LENGTH];
static UBaseType_t g_pendedHead;
static UBaseType_t g_pendedTail;
static UBaseType_t g_pendedCount;
static UINT16 g_pendedTimerId;
static BOOL g_pendedTimerReady;
static BOOL g_pendedTimerInitializing;

static void prvPendedFunctionWrapper(UINT32 arg)
{
    (void)arg;
    for (;;) {
        PendedCall_t call;
        UINT32 intSave = LOS_IntLock();
        if (g_pendedCount == 0U) {
            LOS_IntRestore(intSave);
            return;
        }
        call = g_pendedCalls[g_pendedHead];
        g_pendedHead = (g_pendedHead + 1U) % configTIMER_QUEUE_LENGTH;
        g_pendedCount--;
        LOS_IntRestore(intSave);

        if (call.function != NULL) {
            call.function(call.parameter1, call.parameter2);
        }
    }
}

static BaseType_t prvPendedCallEnsureTimer(void)
{
    UINT32 intSave = LOS_IntLock();
    if (g_pendedTimerReady != FALSE) {
        LOS_IntRestore(intSave);
        return pdPASS;
    }
    if (g_pendedTimerInitializing != FALSE) {
        LOS_IntRestore(intSave);
        return pdFAIL;
    }
    g_pendedTimerInitializing = TRUE;
    LOS_IntRestore(intSave);

    UINT16 timerId;
    UINT32 ret = LOS_SwtmrCreate(1U, LOS_SWTMR_MODE_NO_SELFDELETE,
                                 (SWTMR_PROC_FUNC)prvPendedFunctionWrapper, &timerId, 0U);
    intSave = LOS_IntLock();
    g_pendedTimerInitializing = FALSE;
    if (ret == LOS_OK) {
        g_pendedTimerId = timerId;
        g_pendedTimerReady = TRUE;
    }
    LOS_IntRestore(intSave);
    return (ret == LOS_OK) ? pdPASS : pdFAIL;
}

static BaseType_t prvPendedCallSchedule(PendedFunction_t function, VOID *parameter1, uint32_t parameter2)
{
    if (prvPendedCallEnsureTimer() != pdPASS) {
        return pdFAIL;
    }

    UINT32 intSave = LOS_IntLock();
    if (g_pendedCount >= configTIMER_QUEUE_LENGTH) {
        LOS_IntRestore(intSave);
        return pdFAIL;
    }
    BOOL needStart = (g_pendedCount == 0U) ? TRUE : FALSE;
    g_pendedCalls[g_pendedTail].function = function;
    g_pendedCalls[g_pendedTail].parameter1 = parameter1;
    g_pendedCalls[g_pendedTail].parameter2 = parameter2;
    g_pendedTail = (g_pendedTail + 1U) % configTIMER_QUEUE_LENGTH;
    g_pendedCount++;
    UINT16 timerId = g_pendedTimerId;
    LOS_IntRestore(intSave);

    if ((needStart != FALSE) && (LOS_SwtmrStart(timerId) != LOS_OK)) {
        return pdFAIL;
    }
    return pdPASS;
}

BaseType_t xCompatTimerCancelPendedCalls(PendedFunction_t firstFunction, PendedFunction_t secondFunction,
                                         VOID *parameter1)
{
    UBaseType_t originalCount;
    UBaseType_t keptCount = 0U;
    UINT32 intSave = LOS_IntLock();

    originalCount = g_pendedCount;
    for (UBaseType_t offset = 0U; offset < originalCount; offset++) {
        UBaseType_t readIndex = (g_pendedHead + offset) % configTIMER_QUEUE_LENGTH;
        PendedCall_t call = g_pendedCalls[readIndex];
        if ((call.parameter1 == parameter1) &&
            ((call.function == firstFunction) || (call.function == secondFunction))) {
            continue;
        }
        g_pendedCalls[(g_pendedHead + keptCount) % configTIMER_QUEUE_LENGTH] = call;
        keptCount++;
    }
    g_pendedCount = keptCount;
    g_pendedTail = (g_pendedHead + keptCount) % configTIMER_QUEUE_LENGTH;
    LOS_IntRestore(intSave);
    return (keptCount != originalCount) ? pdTRUE : pdFALSE;
}
#endif

#if (configUSE_TIMERS == 1)
static void prvTimerCallbackWrapper(UINT32 uwArg)
{
    Timer_t *pxTimer = (Timer_t *)(UINTPTR)uwArg;
    if (pxTimer == NULL) {
        return;
    }
    if ((pxTimer->ucStatus & TIMER_STATUS_IS_AUTORELOAD) == 0U) {
        /* 保留单次 FreeRTOS timer 对象，使到期后仍可 restart、reset 和 delete。 */
        pxTimer->ucStatus &= (UINT8)~TIMER_STATUS_IS_ACTIVE;
    }
    if (pxTimer->pxCallback != NULL) {
        pxTimer->pxCallback((TimerHandle_t) pxTimer);
    }

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    /* The wrapper remains owned by the in-flight LiteOS callback after delete. */
    if (((pxTimer->ucStatus & TIMER_STATUS_DELETE_PENDING) != 0U) &&
        ((pxTimer->ucStatus & TIMER_STATUS_IS_STATICALLY_ALLOCATED) == 0U)) {
        UINT32 intSave;
        BOOL canRelease = FALSE;
        FR_SWTMR_LOCK(intSave);
        LosSwtmrCB *swtmr = OsSwtmrIdVerify(pxTimer->usSwTmrID);
        if ((swtmr == NULL) || (swtmr->inProcess <= 1U)) {
            canRelease = TRUE;
        }
        FR_SWTMR_UNLOCK(intSave);
        if (canRelease != FALSE) {
            (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxTimer);
        }
    }
#endif
}

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
TimerHandle_t xTimerCreate(const char * const pcTimerName, const TickType_t xTimerPeriodInTicks,
    const BaseType_t xAutoReload, VOID * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction)
{
    UINT8 ucMode;
    UINT32 uwRet;

    if ((xTimerPeriodInTicks == 0U) || (pxCallbackFunction == NULL)) {
        return NULL;
    }
    Timer_t *pxNewTimer = (Timer_t *)LOS_MemAlloc(OS_SYS_MEM_ADDR, sizeof(Timer_t));
    if (pxNewTimer == NULL) {
        return NULL;
    }
    /* 保证动态 timer 的状态与所有权标记从确定值开始。 */
    if (memset_s(pxNewTimer, sizeof(Timer_t), 0, sizeof(Timer_t)) != EOK) {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxNewTimer);
        return NULL;
    }
    pxNewTimer->pcTimerName = pcTimerName;
    pxNewTimer->pvTimerID = pvTimerID;
    pxNewTimer->pxCallback = pxCallbackFunction;
    pxNewTimer->xTimerPeriodInTicks = xTimerPeriodInTicks;
    pxNewTimer->ucStatus = 0;
    if (xAutoReload != pdFALSE) {
        pxNewTimer->ucStatus |= (uint8_t)TIMER_STATUS_IS_AUTORELOAD;
    }

        /* 避免 LiteOS 单次定时器到期自删除，保证 FreeRTOS timer 后续仍可操作。 */
    ucMode = (xAutoReload != pdFALSE) ? LOS_SWTMR_MODE_PERIOD : LOS_SWTMR_MODE_NO_SELFDELETE;
    uwRet = LOS_SwtmrCreate((UINT32) xTimerPeriodInTicks, ucMode, \
        (SWTMR_PROC_FUNC)prvTimerCallbackWrapper, \
        &pxNewTimer->usSwTmrID, (UINT32)(UINTPTR)pxNewTimer);
    if (uwRet != LOS_OK) {
        LOS_MemFree(OS_SYS_MEM_ADDR, pxNewTimer);
        return NULL;
    }
    return (TimerHandle_t) pxNewTimer;
}

#endif /* configSUPPORT_DYNAMIC_ALLOCATION */

#if (configSUPPORT_STATIC_ALLOCATION == 1)
TimerHandle_t xTimerCreateStatic(const char * const pcTimerName, const TickType_t xTimerPeriodInTicks,
    const BaseType_t xAutoReload, VOID * const pvTimerID, TimerCallbackFunction_t pxCallbackFunction,
    StaticTimer_t *pxTimerBuffer)
{
    UINT8 ucMode;
    UINT32 uwRet;
    /* 复用调用者缓冲区，保证静态 timer 创建不额外占用堆。 */
    Timer_t *pxNewTimer = (Timer_t *) pxTimerBuffer;

        if ((pxNewTimer == NULL) || (xTimerPeriodInTicks == 0U) ||
        (pxCallbackFunction == NULL) || (sizeof(StaticTimer_t) < sizeof(Timer_t))) {
        return NULL;
    }
    if (memset_s(pxNewTimer, sizeof(StaticTimer_t), 0, sizeof(Timer_t)) != EOK) {
        return NULL;
    }
    /* Initialize static timer fields */
    pxNewTimer->pcTimerName = pcTimerName;
    pxNewTimer->pvTimerID = pvTimerID;
    pxNewTimer->pxCallback = pxCallbackFunction;
    pxNewTimer->xTimerPeriodInTicks = xTimerPeriodInTicks;
    pxNewTimer->ucStatus = TIMER_STATUS_IS_STATICALLY_ALLOCATED;
    if (xAutoReload != pdFALSE) {
        pxNewTimer->ucStatus |= (uint8_t)TIMER_STATUS_IS_AUTORELOAD;
    }

    ucMode = (xAutoReload != pdFALSE) ? LOS_SWTMR_MODE_PERIOD : LOS_SWTMR_MODE_NO_SELFDELETE;
    uwRet = LOS_SwtmrCreate((UINT32)xTimerPeriodInTicks, ucMode, \
        (SWTMR_PROC_FUNC)prvTimerCallbackWrapper, \
        &pxNewTimer->usSwTmrID, (UINT32)(UINTPTR)pxNewTimer);
    if (uwRet != LOS_OK) {
        return NULL;
    }

    return (TimerHandle_t)pxNewTimer;
}
#endif /* configSUPPORT_STATIC_ALLOCATION */

static BaseType_t prvTimerStart(Timer_t *pxTimer)
{
    if (LOS_SwtmrStart(pxTimer->usSwTmrID) != LOS_OK) {
        return pdFAIL;
    }
    pxTimer->ucStatus |= TIMER_STATUS_IS_ACTIVE;
    return pdPASS;
}

static BaseType_t prvTimerStop(Timer_t *pxTimer)
{
    UINT32 ret = LOS_SwtmrStop(pxTimer->usSwTmrID);
    if ((ret != LOS_OK) && (ret != LOS_ERRNO_SWTMR_NOT_STARTED)) {
        return pdFAIL;
    }
    pxTimer->ucStatus &= (UINT8)~TIMER_STATUS_IS_ACTIVE;
    return pdPASS;
}

static BaseType_t prvTimerChangePeriod(Timer_t *pxTimer, TickType_t period)
{
    if (period == 0U) {
        return pdFAIL;
    }

    UINT32 intSave;
    BaseType_t timerValid = pdFALSE;
    FR_SWTMR_LOCK(intSave);
    LosSwtmrCB *swtmr = OsSwtmrIdVerify(pxTimer->usSwTmrID);
    if (swtmr != NULL) {
        swtmr->interval = (UINT32)period;
        swtmr->expiry = (UINT32)period;
        timerValid = pdTRUE;
    }
    FR_SWTMR_UNLOCK(intSave);
    if ((timerValid == pdFALSE) ||
        (OsSwtmrStartTimer(pxTimer->usSwTmrID, period, period) != LOS_OK)) {
        return pdFAIL;
    }
    pxTimer->xTimerPeriodInTicks = period;
    pxTimer->ucStatus |= TIMER_STATUS_IS_ACTIVE;
    return pdPASS;
}

static BaseType_t prvTimerDelete(Timer_t *pxTimer)
{
    if (LOS_SwtmrDelete(pxTimer->usSwTmrID) != LOS_OK) {
        return pdFAIL;
    }
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    if ((pxTimer->ucStatus & TIMER_STATUS_IS_STATICALLY_ALLOCATED) == 0U) {
        UINT32 intSave;
        BOOL canRelease = FALSE;
        FR_SWTMR_LOCK(intSave);
        LosSwtmrCB *swtmr = OsSwtmrIdVerify(pxTimer->usSwTmrID);
        if ((swtmr == NULL) || (swtmr->inProcess == 0U)) {
            canRelease = TRUE;
        } else {
            pxTimer->ucStatus |= TIMER_STATUS_DELETE_PENDING;
        }
        FR_SWTMR_UNLOCK(intSave);
        if (canRelease != FALSE) {
            (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxTimer);
        }
    }
#endif
    return pdPASS;
}

BaseType_t xTimerGenericCommandFromTask(TimerHandle_t xTimer, const BaseType_t xCommandID,
    const TickType_t xOptionalValue, BaseType_t *const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait)
{
    Timer_t *pxTimer = (Timer_t *)xTimer;
    BaseType_t xReturn = pdFAIL;
    /* 明确保留 LiteOS 直接命令模型；当前无法兑现 FreeRTOS 命令队列的 xTicksToWait。 */
    (void) xTicksToWait;

    if (pxTimer == NULL) {
        return pdFAIL;
    }
    switch (xCommandID) {
        case tmrCOMMAND_START:
        case tmrCOMMAND_START_FROM_ISR:
        case tmrCOMMAND_RESET:
        case tmrCOMMAND_RESET_FROM_ISR:
            xReturn = prvTimerStart(pxTimer);
            break;
        case tmrCOMMAND_STOP:
        case tmrCOMMAND_STOP_FROM_ISR:
            xReturn = prvTimerStop(pxTimer);
            break;
        case tmrCOMMAND_CHANGE_PERIOD:
        case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
            xReturn = prvTimerChangePeriod(pxTimer, xOptionalValue);
            break;
        case tmrCOMMAND_DELETE:
            xReturn = prvTimerDelete(pxTimer);
            break;
        default:
            break;
    }

    (void)pxHigherPriorityTaskWoken;
    return xReturn;
}

BaseType_t xTimerGenericCommandFromISR(TimerHandle_t xTimer, const BaseType_t xCommandID,
    const TickType_t xOptionalValue, BaseType_t *const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait)
{
    return xTimerGenericCommandFromTask(xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, 0);
}

TickType_t xTimerGetPeriod(TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return 0;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    return pxTimer->xTimerPeriodInTicks;
}

void vTimerSetReloadMode(TimerHandle_t xTimer, const BaseType_t xAutoReload)
{
    if (xTimer == NULL) {
        return;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    UINT32 intSave;
    FR_SWTMR_LOCK(intSave);
    LosSwtmrCB *swtmr = OsSwtmrIdVerify(pxTimer->usSwTmrID);
    if (swtmr == NULL) {
        FR_SWTMR_UNLOCK(intSave);
        return;
    }
    if (xAutoReload != pdFALSE) {
        pxTimer->ucStatus |= (uint8_t)TIMER_STATUS_IS_AUTORELOAD;
        swtmr->mode = LOS_SWTMR_MODE_PERIOD;
    } else {
        pxTimer->ucStatus &= ((uint8_t) ~TIMER_STATUS_IS_AUTORELOAD);
        /* FreeRTOS one-shot timers remain valid after expiry. */
        swtmr->mode = LOS_SWTMR_MODE_NO_SELFDELETE;
    }
    FR_SWTMR_UNLOCK(intSave);
}

BaseType_t xTimerGetReloadMode(TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return pdFALSE;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    if ((pxTimer->ucStatus & TIMER_STATUS_IS_AUTORELOAD) == 0U) {
        return pdFALSE;
    }
    return pdTRUE;
}

UBaseType_t uxTimerGetReloadMode(TimerHandle_t xTimer)
{
    return (UBaseType_t)xTimerGetReloadMode(xTimer);
}


TickType_t xTimerGetExpiryTime(TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return 0;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    UINT32 uwRemainingTicks;
    UINT32 uwRet;

    uwRet = LOS_SwtmrTimeGet(pxTimer->usSwTmrID, &uwRemainingTicks);
    if (uwRet == LOS_OK) {
        return (TickType_t)(LOS_TickCountGet() + uwRemainingTicks);
    }
    return 0;
}


const char *pcTimerGetName(TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return NULL;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    return pxTimer->pcTimerName;
}


BaseType_t xTimerIsTimerActive(TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return pdFALSE;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    return ((pxTimer->ucStatus & TIMER_STATUS_IS_ACTIVE) != 0) ? pdTRUE : pdFALSE;
}


#if (INCLUDE_xTimerPendFunctionCall == 1)
BaseType_t xTimerPendFunctionCallFromISR (PendedFunction_t xFunctionToPend,
                                          VOID * pvParameter1,
                                          uint32_t ulParameter2,
                                          BaseType_t * pxHigherPriorityTaskWoken)
{
    if (xFunctionToPend == NULL) {
        return pdFAIL;
    }
        (void)pxHigherPriorityTaskWoken;
    return prvPendedCallSchedule(xFunctionToPend, pvParameter1, ulParameter2);
}

BaseType_t xTimerPendFunctionCall (PendedFunction_t xFunctionToPend,
                                   VOID * pvParameter1,
                                   uint32_t ulParameter2,
                                   TickType_t xTicksToWait)
{
    if (xFunctionToPend == NULL) {
        return pdFAIL;
    }
    TickType_t start = xTaskGetTickCount();
    do {
        BaseType_t ret = prvPendedCallSchedule(xFunctionToPend, pvParameter1, ulParameter2);
        if (ret == pdPASS) {
            return pdPASS;
        }
        if (xTicksToWait == 0U) {
            return pdFAIL;
        }
        vTaskDelay(1U);
    } while ((xTicksToWait == portMAX_DELAY) ||
             ((TickType_t)(xTaskGetTickCount() - start) < xTicksToWait));
    return pdFAIL;
}
#endif /* INCLUDE_xTimerPendFunctionCall */

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xTimerGetStaticBuffer(TimerHandle_t xTimer, StaticTimer_t **ppxTimerBuffer)
{
    Timer_t *pxTimer = (Timer_t *)xTimer;
    if ((pxTimer == NULL) || (ppxTimerBuffer == NULL) ||
        ((pxTimer->ucStatus & TIMER_STATUS_IS_STATICALLY_ALLOCATED) == 0U)) {
        return pdFALSE;
    }
    *ppxTimerBuffer = (StaticTimer_t *)pxTimer;
    return pdTRUE;
}
#endif

void *pvTimerGetTimerID(const TimerHandle_t xTimer)
{
    if (xTimer == NULL) {
        return NULL;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    return pxTimer->pvTimerID;
}


void vTimerSetTimerID(TimerHandle_t xTimer, void *pvNewID)
{
    if (xTimer == NULL) {
        return;
    }
    Timer_t *pxTimer = (Timer_t *)xTimer;
    pxTimer->pvTimerID = pvNewID;
}
#endif /* configUSE_TIMERS == 1 */
