/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description : LiteOS adapt FreeRTOS Event Group.
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
#include "event_groups.h"
#include "timers.h"
#include "freertos_compat_pri.h"

/* LiteOS includes. */
#include "los_event.h"
#include "los_config.h"
#include "los_memory.h"
#include "los_spinlock.h"
#include "los_task.h"

typedef struct EventGroupDef_t {
    EVENT_CB_S event;
    UINT8      isStatic;
} EventGroup_t;

/* LiteOS clears event bits as each waiter resumes. Keep per-task snapshots so
 * all FreeRTOS waiters matched by one set operation observe the same bits. */
typedef struct {
    EventGroup_t *group;
    EVENT_CB_S wakeEvent;
    EventBits_t waitBits;
    EventBits_t result;
    UINT8 inUse;
    UINT8 waitAll;
    UINT8 clearOnExit;
    UINT8 released;
    UINT8 wakePending;
} EventGroupWaiter_t;

#define EVENT_GROUP_WAKE_BIT 0x1U

static SPIN_LOCK_S g_eventGroupSpin;
LITE_OS_SEC_BSS static SPIN_LOCK_INIT(g_eventGroupSpin);
static EventGroupWaiter_t g_eventGroupWaiters[LOSCFG_BASE_CORE_TSK_LIMIT];

#define EVENT_GROUP_LOCK(state)   LOS_SpinLockSave(&g_eventGroupSpin, &(state))
#define EVENT_GROUP_UNLOCK(state) LOS_SpinUnlockRestore(&g_eventGroupSpin, (state))

static BaseType_t EventGroupConditionMet(EventBits_t bits, EventBits_t waitBits, BaseType_t waitAll)
{
    if (waitAll != pdFALSE) {
        return ((bits & waitBits) == waitBits) ? pdTRUE : pdFALSE;
    }
    return ((bits & waitBits) != 0U) ? pdTRUE : pdFALSE;
}

static EventBits_t EventGroupMarkMatchedWaiters(EventGroup_t *group, EventBits_t snapshot)
{
    EventBits_t bitsToClear = 0;

    for (UINT32 i = 0; i < LOSCFG_BASE_CORE_TSK_LIMIT; i++) {
        EventGroupWaiter_t *waiter = &g_eventGroupWaiters[i];
        if ((waiter->inUse == 0U) || (waiter->released != 0U) || (waiter->group != group)) {
            continue;
        }
        if (EventGroupConditionMet(snapshot, waiter->waitBits, waiter->waitAll) == pdFALSE) {
            continue;
        }

        waiter->result = snapshot;
        waiter->released = 1U;
        waiter->wakePending = 1U;
        if (waiter->clearOnExit != 0U) {
            bitsToClear |= waiter->waitBits;
        }
    }
    return bitsToClear;
}

/* The caller holds g_eventGroupSpin, so a timing-out waiter cannot destroy its private event. */
static void EventGroupWakeMarkedWaiters(EventGroup_t *group)
{
    for (UINT32 i = 0; i < LOSCFG_BASE_CORE_TSK_LIMIT; i++) {
        EventGroupWaiter_t *waiter = &g_eventGroupWaiters[i];
        if ((waiter->inUse == 0U) || (waiter->wakePending == 0U) || (waiter->group != group)) {
            continue;
        }
        waiter->wakePending = 0U;
        (void)LOS_EventWrite(&waiter->wakeEvent, EVENT_GROUP_WAKE_BIT);
    }
}

static EventGroupWaiter_t *EventGroupRegisterWaiter(EventGroup_t *group, EventBits_t waitBits,
    BaseType_t clearOnExit, BaseType_t waitAll)
{
    UINT32 taskId = LOS_CurTaskIDGet();
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return NULL;
    }

    EventGroupWaiter_t *waiter = &g_eventGroupWaiters[taskId];
    if (waiter->inUse != 0U) {
        /* A running task cannot still be blocked in a previous wait. The task ID was reused. */
        (void)LOS_EventDestroy(&waiter->wakeEvent);
        *waiter = (EventGroupWaiter_t){0};
    }
    if (LOS_EventInit(&waiter->wakeEvent) != LOS_OK) {
        return NULL;
    }

    waiter->group = group;
    waiter->waitBits = waitBits;
    waiter->result = 0;
    waiter->waitAll = (waitAll != pdFALSE) ? 1U : 0U;
    waiter->clearOnExit = (clearOnExit != pdFALSE) ? 1U : 0U;
    waiter->released = 0U;
    waiter->wakePending = 0U;
    waiter->inUse = 1U;
    return waiter;
}

static EventBits_t EventGroupFinishWait(EventGroup_t *group, EventGroupWaiter_t *waiter)
{
    EventBits_t result;
    UINT32 intSave;

    EVENT_GROUP_LOCK(intSave);
    if (waiter->released != 0U) {
        result = waiter->result;
    } else {
        result = (EventBits_t)group->event.uwEventID;
        /* Recheck under the lock when timeout races with a set operation. */
        if ((EventGroupConditionMet(result, waiter->waitBits, waiter->waitAll) != pdFALSE) &&
            (waiter->clearOnExit != 0U)) {
            group->event.uwEventID &= ~((UINT32)waiter->waitBits);
        }
    }
    waiter->group = NULL;
    waiter->inUse = 0U;
    waiter->released = 0U;
    waiter->wakePending = 0U;
    EVENT_GROUP_UNLOCK(intSave);
    (void)LOS_EventDestroy(&waiter->wakeEvent);
    return result;
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 在编译期保证 StaticEventGroup_t 足以容纳 compat wrapper，避免覆盖调用者内存。 */
typedef char StaticEventGroupBufferSizeCheck[
    (sizeof(StaticEventGroup_t) >= sizeof(EventGroup_t)) ? 1 : -1];
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *pxEventGroupBuffer)
{
        if ((pxEventGroupBuffer == NULL) || (sizeof(StaticEventGroup_t) < sizeof(EventGroup_t))) {
        return NULL;
    }
    /* 复用调用者缓冲区，保证静态创建不额外占用堆。 */
    EventGroup_t *pxEventBits = (EventGroup_t *)pxEventGroupBuffer;
    if (memset_s(pxEventBits, sizeof(StaticEventGroup_t), 0, sizeof(EventGroup_t)) != EOK) {
        return NULL;
    }
    UINT32 ret = LOS_EventInit(&pxEventBits->event);
    if (ret != LOS_OK) {
        return NULL;
    }
    pxEventBits->isStatic = pdTRUE;
    return (EventGroupHandle_t)pxEventBits;
}

BaseType_t xEventGroupGetStaticBuffer(EventGroupHandle_t xEventGroup, StaticEventGroup_t **ppxEventGroupBuffer)
{
    BaseType_t xReturn;
    EventGroup_t *pxEventBits = xEventGroup;

    if (pxEventBits == NULL || ppxEventGroupBuffer == NULL) {
        return pdFALSE;
    }

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    /* Check if the event group was statically allocated. */
    if (pxEventBits->isStatic == (uint8_t)pdTRUE) {
        *ppxEventGroupBuffer = (StaticEventGroup_t *)pxEventBits;
        xReturn = pdTRUE;
    } else {
        xReturn = pdFALSE;
    }
#else
    *ppxEventGroupBuffer = (StaticEventGroup_t *) pxEventBits;
    xReturn = pdTRUE;
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */
    return xReturn;
}
#endif /* configSUPPORT_STATIC_ALLOCATION */

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
EventGroupHandle_t xEventGroupCreate(void)
{
    UINT32 ret;
    EventGroup_t *pxEventBits = (EventGroup_t *)LOS_MemAlloc(OS_SYS_MEM_ADDR, sizeof(EventGroup_t));
    if (pxEventBits == NULL) {
        return NULL;
    }
    /* 保证动态事件组的所有权标记从确定值开始。 */
    if (memset_s(pxEventBits, sizeof(EventGroup_t), 0, sizeof(EventGroup_t)) != EOK) {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxEventBits);
        return NULL;
    }

    ret = LOS_EventInit(&pxEventBits->event);
    if (ret != LOS_OK) {
        LOS_MemFree(OS_SYS_MEM_ADDR, pxEventBits);
        return NULL;
    }
    pxEventBits->isStatic = pdFALSE;
    return (EventGroupHandle_t)pxEventBits;
}
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor,
    const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits, TickType_t xTicksToWait)
{
    if (xEventGroup == NULL || (uxBitsToWaitFor == 0)) {
        return 0;
    }
    EventGroup_t *pxEventBits = (EventGroup_t *)xEventGroup;
    EventGroupWaiter_t *waiter = NULL;
    EventBits_t snapshot;
    UINT32 intSave;

    EVENT_GROUP_LOCK(intSave);
    snapshot = (EventBits_t)pxEventBits->event.uwEventID;
    if (EventGroupConditionMet(snapshot, uxBitsToWaitFor, xWaitForAllBits) != pdFALSE) {
        if (xClearOnExit != pdFALSE) {
            pxEventBits->event.uwEventID &= ~((UINT32)uxBitsToWaitFor);
        }
    } else if (xTicksToWait != 0U) {
        waiter = EventGroupRegisterWaiter(pxEventBits, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits);
    }
    EVENT_GROUP_UNLOCK(intSave);

    if (waiter == NULL) {
        return snapshot;
    }

    (void)LOS_EventRead(&waiter->wakeEvent, EVENT_GROUP_WAKE_BIT,
        LOS_WAITMODE_OR | LOS_WAITMODE_CLR, (UINT32)xTicksToWait);
    snapshot = EventGroupFinishWait(pxEventBits, waiter);
    return snapshot;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    EventGroup_t *pxEventBits = (EventGroup_t *)xEventGroup;
    EventBits_t uxReturn;
    UINT32 intSave;

    EVENT_GROUP_LOCK(intSave);
    uxReturn = (EventBits_t)pxEventBits->event.uwEventID;
    pxEventBits->event.uwEventID &= ~((UINT32)uxBitsToClear);
    EVENT_GROUP_UNLOCK(intSave);

    return uxReturn;
}

VOID vEventGroupClearBitsCallback(VOID *pvEventGroup, uint32_t ulBitsToClear)
{
    if (pvEventGroup == NULL) {
        return;
    }
    (VOID) xEventGroupClearBits(pvEventGroup, (EventBits_t)ulBitsToClear);
}

#if (configUSE_TRACE_FACILITY == 1)
BaseType_t xEventGroupClearBitsFromISR(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear)
{
    if (xEventGroup == NULL) {
        return pdFAIL;
    }
    return xTimerPendFunctionCallFromISR(vEventGroupClearBitsCallback, (void *)xEventGroup,
                                         (uint32_t)uxBitsToClear, NULL);
}
#endif

EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup)
{
    return xEventGroupClearBits(xEventGroup, 0);
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    EventGroup_t *pxEventBits = (EventGroup_t *)xEventGroup;
    EventBits_t snapshot;
    EventBits_t bitsToClear;
    EventBits_t uxReturn;
    UINT32 intSave;

    /* Defer scheduling until every matched waiter has captured the same snapshot. */
    LOS_TaskLock();
    EVENT_GROUP_LOCK(intSave);
    pxEventBits->event.uwEventID |= (UINT32)uxBitsToSet;
    snapshot = (EventBits_t)pxEventBits->event.uwEventID;
    bitsToClear = EventGroupMarkMatchedWaiters(pxEventBits, snapshot);
    pxEventBits->event.uwEventID &= ~((UINT32)bitsToClear);
    uxReturn = (EventBits_t)pxEventBits->event.uwEventID;
    EventGroupWakeMarkedWaiters(pxEventBits);
    EVENT_GROUP_UNLOCK(intSave);
    LOS_TaskUnlock();
    return uxReturn;
}

VOID vEventGroupSetBitsCallback(VOID *pvEventGroup, uint32_t ulBitsToSet)
{
    if (pvEventGroup == NULL) {
        return;
    }
    (VOID)xEventGroupSetBits(pvEventGroup, (EventBits_t)ulBitsToSet);
}

#if ((configUSE_TRACE_FACILITY == 1) && (INCLUDE_xTimerPendFunctionCall == 1) && (configUSE_TIMERS == 1))
BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet,
                                     BaseType_t *pxHigherPriorityTaskWoken)
{
    if (xEventGroup == NULL) {
        return pdFAIL;
    }
    return xTimerPendFunctionCallFromISR(vEventGroupSetBitsCallback, (void *)xEventGroup,
                                         (uint32_t)uxBitsToSet, pxHigherPriorityTaskWoken);
}
#endif


void vEventGroupDelete(EventGroupHandle_t xEventGroup)
{
    if (xEventGroup == NULL) {
        return;
    }
    EventGroup_t *pxEventBits = (EventGroup_t *)xEventGroup;
    UINT32 intSave;
    UINT32 interruptSave;

    /* Exclude pending ISR callbacks while the wrapper and its waiters are released. */
    LOS_TaskLock();
    interruptSave = LOS_IntLock();
#if (INCLUDE_xTimerPendFunctionCall == 1)
    (void)xCompatTimerCancelPendedCalls(vEventGroupSetBitsCallback, vEventGroupClearBitsCallback,
                                        (VOID *)pxEventBits);
#endif
    EVENT_GROUP_LOCK(intSave);
    for (UINT32 i = 0; i < LOSCFG_BASE_CORE_TSK_LIMIT; i++) {
        EventGroupWaiter_t *waiter = &g_eventGroupWaiters[i];
        if ((waiter->inUse == 0U) || (waiter->group != pxEventBits)) {
            continue;
        }
        waiter->result = 0;
        waiter->released = 1U;
        waiter->wakePending = 1U;
    }
    EventGroupWakeMarkedWaiters(pxEventBits);
    EVENT_GROUP_UNLOCK(intSave);

    (void)LOS_EventDestroy(&pxEventBits->event);

    if (pxEventBits->isStatic != pdTRUE) {
        (VOID)LOS_MemFree(OS_SYS_MEM_ADDR, pxEventBits);
    } else {
        (VOID)memset_s(pxEventBits, sizeof(EventGroup_t), 0, sizeof(EventGroup_t));
    }
    LOS_IntRestore(interruptSave);
    LOS_TaskUnlock();
}

EventBits_t xEventGroupSync(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet,
    const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait)
{
    if (xEventGroup == NULL) {
        return 0;
    }
    EventGroup_t *pxEventBits = (EventGroup_t *)xEventGroup;
    EventGroupWaiter_t *waiter = NULL;
    EventBits_t snapshot;
    EventBits_t bitsToClear;
    EventBits_t result;
    UINT32 intSave;
    BaseType_t conditionMet;

    /* Set, test, waiter registration and clear form one scheduling transaction. */
    LOS_TaskLock();
    EVENT_GROUP_LOCK(intSave);
    pxEventBits->event.uwEventID |= (UINT32)uxBitsToSet;
    snapshot = (EventBits_t)pxEventBits->event.uwEventID;
    bitsToClear = EventGroupMarkMatchedWaiters(pxEventBits, snapshot);
    conditionMet = (uxBitsToWaitFor != 0U) ?
        EventGroupConditionMet(snapshot, uxBitsToWaitFor, pdTRUE) : pdFALSE;
    if (conditionMet != pdFALSE) {
        bitsToClear |= uxBitsToWaitFor;
    }
    pxEventBits->event.uwEventID &= ~((UINT32)bitsToClear);

    if ((uxBitsToWaitFor != 0U) && (conditionMet == pdFALSE) && (xTicksToWait != 0U)) {
        waiter = EventGroupRegisterWaiter(pxEventBits, uxBitsToWaitFor, pdTRUE, pdTRUE);
    }
    result = (conditionMet != pdFALSE) ? snapshot : (EventBits_t)pxEventBits->event.uwEventID;
    EventGroupWakeMarkedWaiters(pxEventBits);
    EVENT_GROUP_UNLOCK(intSave);
    LOS_TaskUnlock();

    if ((uxBitsToWaitFor == 0U) || (conditionMet != pdFALSE) || (waiter == NULL)) {
        return result;
    }

    (void)LOS_EventRead(&waiter->wakeEvent, EVENT_GROUP_WAKE_BIT,
        LOS_WAITMODE_OR | LOS_WAITMODE_CLR, (UINT32)xTicksToWait);
    result = EventGroupFinishWait(pxEventBits, waiter);
    return result;
}
