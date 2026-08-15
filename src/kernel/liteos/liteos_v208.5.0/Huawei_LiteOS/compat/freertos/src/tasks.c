/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights reserved.
 * Description : LiteOS adapt FreeRTOS task.
 * Author : Huawei LiteOS Team
 * Create : 2026-1-13
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

/* Standard includes. */
#include <stdlib.h>
#include <string.h>

/* liteos includes. */
#include "los_task.h"
#include "los_task_base.h"
#include "los_task_pri.h"
#include "los_event.h"
#include "los_spinlock.h"
#include "los_stackinfo_pri.h"
#include "los_mp_pri.h"
#include "los_sched_pri.h"
#include "securec.h"

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "stack_macros.h"
#include "freertos_compat_pri.h"

#ifndef configINITIAL_TICK_COUNT
    #define configINITIAL_TICK_COUNT    0
#endif

#ifndef portMAX_DELAY
    #define portMAX_DELAY    (TickType_t) 0xffffffffUL
#endif

/* Values that can be assigned to the ucNotifyState member of the TCB. */
#define taskNOT_WAITING_NOTIFICATION              ((uint8_t) 0) /* Must be zero as it is the initialised value. */
#define taskWAITING_NOTIFICATION                  ((uint8_t) 1)
#define taskNOTIFICATION_RECEIVED                 ((uint8_t) 2)

/* Other file private variables. --------------------------------*/
PRIVILEGED_DATA static volatile BaseType_t xNumOfOverflows = (BaseType_t) 0;
PRIVILEGED_DATA static volatile TickType_t xTickCount = (TickType_t) configINITIAL_TICK_COUNT;
PRIVILEGED_DATA static volatile UBaseType_t uxSchedulerSuspended = 0;

/*
 * 用于适配liteos的tskTCB
 */
typedef struct tskTaskControlBlock {
    UINT32 taskId;
        UBaseType_t uxBasePriority;
    uint8_t ucStaticallyAllocated;
    uint8_t ucKernelOwned;
    #if (configUSE_APPLICATION_TASK_TAG == 1)
        TaskHookFunction_t pxTaskTag;
    #endif
    #if (configUSE_TASK_NOTIFICATIONS == 1)
                EVENT_CB_S notifyEvent;
        volatile uint32_t ulNotifiedValue[configTASK_NOTIFICATION_ARRAY_ENTRIES];
        volatile uint8_t ucNotifyState[configTASK_NOTIFICATION_ARRAY_ENTRIES];
    #endif
    #if (INCLUDE_xTaskAbortDelay == 1)
        uint8_t ucDelayAborted;
    #endif
    #if (configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0)
    void *pvThreadLocalStoragePointers[configNUM_THREAD_LOCAL_STORAGE_POINTERS];
    #endif /* configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0 */
} tskTCB;
typedef tskTCB TCB_t;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 在编译期阻止 StaticTask_t 容量不足导致调用者内存越界。 */
typedef char StaticTaskBufferSizeCheck[(sizeof(StaticTask_t) >= sizeof(tskTCB)) ? 1 : -1];
#endif

#define TASK_NOTIFY_WAKE_BIT 0x1U

#if (LOSCFG_BASE_CORE_TSK_LIMIT >= 1)
    portDONT_DISCARD PRIVILEGED_DATA TCB_t *volatile pxCurrentTCBs[LOSCFG_BASE_CORE_TSK_LIMIT] = {0};
    #define pxCurrentTCB    xTaskGetCurrentTaskHandle()
#endif

#if (INCLUDE_xTaskGetIdleTaskHandle == 1)
/* 为 LiteOS 内核创建的 idle task 提供生命周期稳定的 FreeRTOS 句柄。 */
static TCB_t g_idleTaskTCB;
static BOOL g_idleTaskTCBInitialized;
#endif

/* spinlock for freertos task module, only available on SMP mode */
SPIN_LOCK_S g_fr_taskSpin;
LITE_OS_SEC_BSS  SPIN_LOCK_INIT(g_fr_taskSpin);

#define FR_TASK_LOCK(state)       LOS_SpinLockSave(&g_fr_taskSpin, &(state))
#define FR_TASK_UNLOCK(state)     LOS_SpinUnlockRestore(&g_fr_taskSpin, (state))

/*
 * FreeRTOS 与 LiteOS 的优先级方向相反：
 *   FreeRTOS: 数值越大优先级越高，0 = 最低(idle)，范围 0 ~ (configMAX_PRIORITIES-1)
 *   LiteOS  : 数值越小优先级越高，0 = 最高，范围 LOS_TASK_PRIORITY_HIGHEST(0) ~ LOS_TASK_PRIORITY_LOWEST(31)
 * 这里把 FreeRTOS 优先级映射到 LiteOS 的低端(数值大)区间，保持"高优先级"语义一致。
 */
static inline UINT16 PrioFR2LOS(UBaseType_t frPrio)
{
    if (frPrio >= configMAX_PRIORITIES) {
        frPrio = configMAX_PRIORITIES - 1;
    }
    return (UINT16)(LOS_TASK_PRIORITY_LOWEST - frPrio);
}

static inline UBaseType_t PrioLOS2FR(UINT16 losPrio)
{
    if (losPrio > LOS_TASK_PRIORITY_LOWEST) {
        losPrio = LOS_TASK_PRIORITY_LOWEST;
    }
    if (losPrio < (LOS_TASK_PRIORITY_LOWEST - (configMAX_PRIORITIES - 1))) {
        /* 内核里比 FreeRTOS 可表达范围更高的优先级(如系统任务)，钳到最高可表达值 */
        return (UBaseType_t)(configMAX_PRIORITIES - 1);
    }
    return (UBaseType_t)(LOS_TASK_PRIORITY_LOWEST - losPrio);
}

static eTaskState xTaskStatusAdapter(UINT16 taskStatus)
{
    if (taskStatus & OS_TASK_STATUS_UNUSED) {
        return eDeleted;
    }

    if (taskStatus & OS_TASK_STATUS_SUSPEND) {
        return eSuspended;
    }

    if (taskStatus & OS_TASK_STATUS_READY) {
        return eReady;
    }

    if (taskStatus & OS_TASK_STATUS_PEND) {
        return eBlocked;
    }

    if (taskStatus & OS_TASK_STATUS_RUNNING) {
        return eRunning;
    }

    return eInvalid;
}

static UBaseType_t prvStackHighWaterMark(const LosTaskCB *taskCB)
{
    UINT32 peakUsed = 0;
    UINTPTR stackBottom;

    if (taskCB == NULL) {
        return 0;
    }
    stackBottom = TRUNCATE(((UINTPTR)taskCB->topOfStack + taskCB->stackSize),
                           LOSCFG_STACK_POINT_ALIGN_SIZE);
    if ((OsStackWaterLineGet((const UINTPTR *)stackBottom,
                             (const UINTPTR *)taskCB->topOfStack, &peakUsed) != LOS_OK) ||
        (peakUsed > taskCB->stackSize)) {
        return 0;
    }
    /* 将 LiteOS 峰值已用字节数换算为 FreeRTOS 要求的历史最小剩余栈元素数。 */
    return (UBaseType_t)((taskCB->stackSize - peakUsed) / sizeof(StackType_t));
}

static BaseType_t xTaskMapTaskId(TaskHandle_t xtask, UINT32 taskId)
{
    UINT32 intSave;
        if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    FR_TASK_LOCK(intSave);
    pxCurrentTCBs[taskId] = xtask;
    FR_TASK_UNLOCK(intSave);
    return pdPASS;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    TaskHandle_t xReturn;
    UINT32 intSave;
    UINT32 taskId = LOS_CurTaskIDGet();
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return NULL;
    }
    FR_TASK_LOCK(intSave);
    xReturn = pxCurrentTCBs[taskId];
    FR_TASK_UNLOCK(intSave);

    return xReturn;
}

#if (INCLUDE_xTaskGetIdleTaskHandle == 1)
static TaskHandle_t prvGetIdleTaskHandle(void)
{
    if (LOS_TaskIsScheduled() != TRUE) {
        return NULL;
    }

    UINT32 taskId = OsGetIdleTaskId();
    if ((taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) ||
        ((OS_TCB_FROM_TID(taskId)->taskStatus & OS_TASK_STATUS_UNUSED) != 0U)) {
        return NULL;
    }

    UINT32 intSave;
    FR_TASK_LOCK(intSave);
    TaskHandle_t taskHandle = pxCurrentTCBs[taskId];
    if (taskHandle == NULL) {
        if (g_idleTaskTCBInitialized == FALSE) {
            if (memset_s(&g_idleTaskTCB, sizeof(g_idleTaskTCB), 0, sizeof(g_idleTaskTCB)) != EOK) {
                FR_TASK_UNLOCK(intSave);
                return NULL;
            }
            g_idleTaskTCB.taskId = taskId;
            g_idleTaskTCB.uxBasePriority = tskIDLE_PRIORITY;
            g_idleTaskTCB.ucKernelOwned = pdTRUE;
            g_idleTaskTCBInitialized = TRUE;
        }
        pxCurrentTCBs[taskId] = &g_idleTaskTCB;
        taskHandle = &g_idleTaskTCB;
    }
    FR_TASK_UNLOCK(intSave);
    return taskHandle;
}

#if (configNUMBER_OF_CORES == 1)
TaskHandle_t xTaskGetIdleTaskHandle(void)
{
    return prvGetIdleTaskHandle();
}
#endif

TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t xCoreID)
{
    return (xCoreID == 0) ? prvGetIdleTaskHandle() : NULL;
}
#endif

TaskHandle_t xTaskGetHandleByKernelTaskId(UINT32 taskId)
{
    TaskHandle_t xReturn;
    UINT32 intSave;

    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return NULL;
    }
    /* 为互斥量持有者查询提供 LiteOS task ID 到 FreeRTOS 句柄的稳定映射。 */
    FR_TASK_LOCK(intSave);
    xReturn = pxCurrentTCBs[taskId];
    FR_TASK_UNLOCK(intSave);
    return xReturn;
}

#if (INCLUDE_xTaskGetHandle == 1)
TaskHandle_t xTaskGetHandle(const char *pcNameToQuery)
{
    if (pcNameToQuery == NULL) {
        return NULL;
    }

    UINT32 intSave;
    TaskHandle_t result = NULL;
    FR_TASK_LOCK(intSave);
    for (UINT32 taskId = 0; taskId < LOSCFG_BASE_CORE_TSK_LIMIT; taskId++) {
        TaskHandle_t handle = pxCurrentTCBs[taskId];
        LosTaskCB *taskCB = OS_TCB_FROM_TID(taskId);
        if ((handle == NULL) || ((taskCB->taskStatus & OS_TASK_STATUS_UNUSED) != 0U)) {
            continue;
        }
        if ((taskCB->taskName != NULL) && (strcmp(taskCB->taskName, pcNameToQuery) == 0)) {
            result = handle;
            break;
        }
    }
    FR_TASK_UNLOCK(intSave);
    return result;
}
#endif

eTaskState eTaskGetState(TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
    const LosTaskCB *taskCB =  OS_TCB_FROM_TID(taskId);
    return xTaskStatusAdapter(taskCB->taskStatus);
}

BaseType_t xTaskGetSchedulerState(void)
{
    if (LOS_TaskIsScheduled() != TRUE) {
        return taskSCHEDULER_NOT_STARTED;
    }
    return (uxSchedulerSuspended > 0U) ? taskSCHEDULER_SUSPENDED : taskSCHEDULER_RUNNING;
}

#if (configUSE_TASK_NOTIFICATIONS == 1)
/* 使用 LiteOS EVENT 仅承担阻塞唤醒，通知值和状态保留在 compat TCB，以维持 FreeRTOS 通知语义。 */
static void prvNotifySyncDeinit(TCB_t *taskCB, UBaseType_t count)
{
    (void)count;
    (void)LOS_EventDestroy(&taskCB->notifyEvent);
}

static BaseType_t prvNotifySyncInit(TCB_t *taskCB)
{
    if (LOS_EventInit(&taskCB->notifyEvent) != LOS_OK) {
        return pdFAIL;
    }
    for (UBaseType_t i = 0; i < configTASK_NOTIFICATION_ARRAY_ENTRIES; i++) {
        taskCB->ulNotifiedValue[i] = 0;
        taskCB->ucNotifyState[i] = taskNOT_WAITING_NOTIFICATION;
    }
    return pdPASS;
}
#endif /* configUSE_TASK_NOTIFICATIONS */

static void prvTaskUnmapTaskId(UINT32 taskId)
{
    UINT32 intSave;
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return;
    }
    FR_TASK_LOCK(intSave);
    pxCurrentTCBs[taskId] = NULL;
    FR_TASK_UNLOCK(intSave);
}

/* 区分动态与静态 TCB 所有权，避免删除静态任务时释放调用者内存。 */
static void prvTaskControlBlockRelease(TCB_t *taskCB)
{
    if ((taskCB != NULL) && (taskCB->ucStaticallyAllocated == pdFALSE) &&
        (taskCB->ucKernelOwned == pdFALSE)) {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, taskCB);
    }
}

static BaseType_t prvTaskControlBlockInit(TCB_t *taskCB, size_t bufferSize,
                                          UBaseType_t uxPriority, BaseType_t isStatic)
{
    if (memset_s(taskCB, bufferSize, 0, sizeof(tskTCB)) != EOK) {
        return pdFAIL;
    }
    taskCB->ucStaticallyAllocated = (uint8_t)isStatic;
    taskCB->uxBasePriority =
        (uxPriority < configMAX_PRIORITIES) ? uxPriority : (configMAX_PRIORITIES - 1U);
#if (configUSE_TASK_NOTIFICATIONS == 1)
    if (prvNotifySyncInit(taskCB) != pdPASS) {
        return pdFAIL;
    }
#endif
#if (INCLUDE_xTaskAbortDelay == 1)
    taskCB->ucDelayAborted = pdFALSE;
#endif
    return pdPASS;
}

static void prvTaskInitParam(TSK_INIT_PARAM_S *initParam, TaskFunction_t taskCode,
                             const char *name, configSTACK_DEPTH_TYPE stackDepth,
                             void *parameters, UBaseType_t priority)
{
    initParam->pcName = (char *)name;
    initParam->pfnTaskEntry = (TSK_ENTRY_FUNC)taskCode;
    initParam->pArgs = parameters;
    initParam->uwStackSize = stackDepth * sizeof(StackType_t);
    initParam->usTaskPrio = PrioFR2LOS(priority);
}

static void prvTaskCreateCleanup(TCB_t *taskCB, BaseType_t taskCreated,
                                 BaseType_t taskMapped)
{
    if (taskMapped != pdFALSE) {
        prvTaskUnmapTaskId(taskCB->taskId);
    }
    if (taskCreated != pdFALSE) {
        (void)LOS_TaskDelete(taskCB->taskId);
    }
#if (configUSE_TASK_NOTIFICATIONS == 1)
    prvNotifySyncDeinit(taskCB, configTASK_NOTIFICATION_ARRAY_ENTRIES);
#endif
    prvTaskControlBlockRelease(taskCB);
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char * const pcName,
                       const configSTACK_DEPTH_TYPE uxStackDepth,
                       void * const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t * const pxCreatedTask)
{
    UINT32 ret;
    TSK_INIT_PARAM_S initParam = {0};
    TaskHandle_t pxNewTCB;
    if (pxTaskCode == NULL) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    pxNewTCB = (TaskHandle_t)LOS_MemAlloc(OS_SYS_MEM_ADDR, sizeof(tskTCB));
    if (pxNewTCB == NULL) {
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    if (prvTaskControlBlockInit(pxNewTCB, sizeof(tskTCB), uxPriority, pdFALSE) != pdPASS) {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxNewTCB);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    prvTaskInitParam(&initParam, pxTaskCode, pcName, uxStackDepth, pvParameters, uxPriority);

    ret = LOS_TaskCreateOnly(&(pxNewTCB->taskId), &initParam);
    if (ret != LOS_OK) {
        prvTaskCreateCleanup(pxNewTCB, pdFALSE, pdFALSE);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    ret = xTaskMapTaskId(pxNewTCB, pxNewTCB->taskId);
    if (ret != pdPASS) {
        prvTaskCreateCleanup(pxNewTCB, pdTRUE, pdFALSE);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    if (pxCreatedTask != NULL) {
        *pxCreatedTask = pxNewTCB;
    }
    ret = LOS_TaskResume(pxNewTCB->taskId);
    if (ret != LOS_OK) {
        if (pxCreatedTask != NULL) {
            *pxCreatedTask = NULL;
        }
        prvTaskCreateCleanup(pxNewTCB, pdTRUE, pdTRUE);
        return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    return pdPASS;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char * const pcName,
                               const configSTACK_DEPTH_TYPE uxStackDepth,
                               void * const pvParameters,
                               UBaseType_t uxPriority,
                               StackType_t * const puxStackBuffer,
                               StaticTask_t * const pxTaskBuffer)
{
    UINT32 ret;
    TSK_INIT_PARAM_S initParam = {0};

    /* 保留调用者栈缓冲区身份，并由 LiteOS 校验 3322 所需的 16 字节地址与大小对齐。 */
    if ((pxTaskCode == NULL) || (puxStackBuffer == NULL) || (pxTaskBuffer == NULL) ||
        (sizeof(StaticTask_t) < sizeof(tskTCB))) {
        return NULL;
    }

    TaskHandle_t task = (TaskHandle_t)pxTaskBuffer;
    if (prvTaskControlBlockInit(task, sizeof(StaticTask_t), uxPriority, pdTRUE) != pdPASS) {
        return NULL;
    }
    prvTaskInitParam(&initParam, pxTaskCode, pcName, uxStackDepth, pvParameters, uxPriority);

    ret = LOS_TaskCreateOnlyStatic(&(task->taskId), &initParam, puxStackBuffer);
    if (ret != LOS_OK) {
        prvTaskCreateCleanup(task, pdFALSE, pdFALSE);
        return NULL;
    }

    ret = xTaskMapTaskId(task, task->taskId);
    if (ret != pdPASS) {
        prvTaskCreateCleanup(task, pdTRUE, pdFALSE);
        return NULL;
    }

    ret = LOS_TaskResume(task->taskId);
    if (ret != LOS_OK) {
        prvTaskCreateCleanup(task, pdTRUE, pdTRUE);
        return NULL;
    }

    return task;
}

TickType_t xTaskGetTickCount(void)
{
    // liteos为自系统启动开始，freertos为调度开始。
    return LOS_TickCountGet();
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    UINT32 taskId;
    if (xTaskToDelete == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTaskToDelete->taskId;
    }

    if ((taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) || (pxCurrentTCBs[taskId] == NULL)) {
                return;
    }

    TCB_t *taskCB = pxCurrentTCBs[taskId];
    /* 保留 LiteOS 内核拥有的系统任务，避免删除失败后 compat 仍清理有效句柄。 */
    if (taskCB->ucKernelOwned != pdFALSE) {
        return;
    }

    if (taskId == LOS_CurTaskIDGet()) {
                if (OsPreemptable() == FALSE) {
            return;
        }
#if (configUSE_TASK_NOTIFICATIONS == 1)
        prvNotifySyncDeinit(taskCB, configTASK_NOTIFICATION_ARRAY_ENTRIES);
#endif
        prvTaskUnmapTaskId(taskId);
        prvTaskControlBlockRelease(taskCB);
        (void)LOS_TaskDelete(taskId);
        return;
    }

    if (LOS_TaskDelete(taskCB->taskId) != LOS_OK) {
        return;
    }
#if (configUSE_TASK_NOTIFICATIONS == 1)
    prvNotifySyncDeinit(taskCB, configTASK_NOTIFICATION_ARRAY_ENTRIES);
#endif
        prvTaskUnmapTaskId(taskId);
    prvTaskControlBlockRelease(taskCB);
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
    LOS_TaskDelay(xTicksToDelay);
}

BaseType_t xTaskDelayUntil(TickType_t *pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    if (pxPreviousWakeTime == NULL) {
        return pdFALSE;
    }
    UINT32 intSave;
    BaseType_t retVal = pdFALSE;
    TickType_t cur_tick = LOS_TickCountGet();
    /* 计算下一个唤醒点(无符号tick，溢出用减法) */
    FR_TASK_LOCK(intSave);
    TickType_t next_wake = *pxPreviousWakeTime + xTimeIncrement;
    if ((TickType_t)(cur_tick - *pxPreviousWakeTime) < xTimeIncrement) {
        /* 还没到时间，计算剩余tick */
        TickType_t left_tick = next_wake - cur_tick;
        *pxPreviousWakeTime = next_wake;
        FR_TASK_UNLOCK(intSave);
        UINT32 losRet = LOS_TaskDelay(left_tick);
        return (losRet == LOS_OK) ? pdTRUE : pdFALSE;
    } else {
        /* 已经超期：仍推进基准时间，避免累计漂移 */
        *pxPreviousWakeTime = next_wake;
        retVal = pdFALSE;
    }
    FR_TASK_UNLOCK(intSave);
    return retVal;
}

UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }

    return PrioLOS2FR((UINT16)LOS_TaskPriGet(taskId));
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }

        if ((pxCurrentTCBs[taskId] != NULL) && (pxCurrentTCBs[taskId]->ucKernelOwned != pdFALSE)) {
        return;
    }
    if (pxCurrentTCBs[taskId] != NULL) {
        pxCurrentTCBs[taskId]->uxBasePriority =
            (uxNewPriority < configMAX_PRIORITIES) ? uxNewPriority : (configMAX_PRIORITIES - 1U);
    }
    LOS_TaskPriSet(taskId, PrioFR2LOS(uxNewPriority));
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    UINT32 taskId;
    if (xTaskToSuspend == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTaskToSuspend->taskId;
    }
    LOS_TaskSuspend(taskId);
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
    if (xTaskToResume == NULL) {
        return;
    }

    LOS_TaskResume(xTaskToResume->taskId);
}

BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    if ((xTaskToResume == NULL) ||
        (xTaskToResume->taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) ||
        (pxCurrentTCBs[xTaskToResume->taskId] != xTaskToResume)) {
        return pdFALSE;
    }

    LosTaskCB *targetTask = OS_TCB_FROM_TID(xTaskToResume->taskId);
    LosTaskCB *currentTask = OsCurrTaskGet();
    BaseType_t shouldYield = ((currentTask != NULL) &&
                              (targetTask->priority < currentTask->priority)) ? pdTRUE : pdFALSE;
    UINT32 ret = LOS_TaskResume(xTaskToResume->taskId);
    return ((ret == LOS_OK) && (shouldYield != pdFALSE)) ? pdTRUE : pdFALSE;
}

#if (INCLUDE_xTaskAbortDelay == 1)
BaseType_t xTaskAbortDelay(TaskHandle_t xTask)
{
    (void)xTask;
    return pdFALSE;
}
#endif

UBaseType_t uxTaskPriorityGetFromISR(const TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }

    UBaseType_t pri = PrioLOS2FR((UINT16)LOS_TaskPriGet(taskId));

    return pri;
}

UBaseType_t uxTaskBasePriorityGet(const TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
        if ((taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) || (pxCurrentTCBs[taskId] == NULL) ||
        ((xTask != NULL) && (pxCurrentTCBs[taskId] != xTask))) {
        return pdFAIL;
    }
    TCB_t *taskCB = pxCurrentTCBs[taskId];
    if (taskCB == NULL) {
        return 0;
    }
    return taskCB->uxBasePriority;
}

UBaseType_t uxTaskBasePriorityGetFromISR(const TaskHandle_t xTask)
{
    UINT32 intSave;

    FR_TASK_LOCK(intSave);
    UBaseType_t pri = uxTaskBasePriorityGet(xTask);
    FR_TASK_UNLOCK(intSave);

    return pri;
}

UBaseType_t uxTaskGetNumberOfTasks(void)
{
    return g_taskUsed;
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t * const pxTaskStatusArray,
                                 const UBaseType_t uxArraySize,
                                 configRUN_TIME_COUNTER_TYPE * const pulTotalRunTime)
{
    UBaseType_t task_num = uxTaskGetNumberOfTasks();
    if (pxTaskStatusArray == NULL || uxArraySize < task_num) {
        return 0;
    }
    const LosTaskCB *taskCB = NULL;
    UINT32 intSave;
        UBaseType_t taskIndex = 0;
    FR_TASK_LOCK(intSave);
    /* 扫描完整任务表，避免 LiteOS task ID 删除复用后遗漏非连续任务。 */
    for (UBaseType_t loop = 0; loop < LOSCFG_BASE_CORE_TSK_LIMIT; loop++) {
        taskCB = g_osTaskCBArray + loop;
        if ((taskCB->taskStatus & OS_TASK_STATUS_UNUSED) != 0U) {
            continue;
        }
        pxTaskStatusArray[taskIndex].xHandle = pxCurrentTCBs[taskCB->taskId];
        pxTaskStatusArray[taskIndex].pcTaskName = taskCB->taskName;
        pxTaskStatusArray[taskIndex].xTaskNumber = taskCB->taskId;
        pxTaskStatusArray[taskIndex].eCurrentState = xTaskStatusAdapter(taskCB->taskStatus);
        pxTaskStatusArray[taskIndex].uxCurrentPriority = PrioLOS2FR((UINT16)taskCB->priority);
        pxTaskStatusArray[taskIndex].uxBasePriority = (pxCurrentTCBs[taskCB->taskId] != NULL) ?
            pxCurrentTCBs[taskCB->taskId]->uxBasePriority :
            PrioLOS2FR((UINT16)taskCB->priority);
        pxTaskStatusArray[taskIndex].pxStackBase = (StackType_t *)taskCB->topOfStack;
        pxTaskStatusArray[taskIndex].usStackHighWaterMark = prvStackHighWaterMark(taskCB);
#ifdef LOSCFG_WCFS_SCHEDULER
        pxTaskStatusArray[taskIndex].ulRunTimeCounter = taskCB->runtick;
#endif
        taskIndex++;
    }
    FR_TASK_UNLOCK(intSave);
    if (pulTotalRunTime != NULL) {
        *pulTotalRunTime = LOS_TickCountGet();
    }
    return taskIndex;
}

void vTaskGetInfo(TaskHandle_t xTask,
                  TaskStatus_t * pxTaskStatus,
                  BaseType_t xGetFreeStackSpace,
                  eTaskState eState)
{
    UINT32 taskId;
    if (pxTaskStatus == NULL) {
        return;
    }
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
    const LosTaskCB *taskCB =  OS_TCB_FROM_TID(taskId);
    if (taskCB == NULL) {
        return;
    }
    UINT32 intSave;
    FR_TASK_LOCK(intSave);
    pxTaskStatus->xHandle = pxCurrentTCBs[taskCB->taskId];
    pxTaskStatus->pcTaskName = taskCB->taskName;
    pxTaskStatus->xTaskNumber = taskCB->taskId;
    pxTaskStatus->eCurrentState = xTaskStatusAdapter(taskCB->taskStatus);
    pxTaskStatus->uxCurrentPriority = PrioLOS2FR((UINT16)taskCB->priority);
    pxTaskStatus->uxBasePriority = (pxCurrentTCBs[taskCB->taskId] != NULL) ?
        pxCurrentTCBs[taskCB->taskId]->uxBasePriority :
        PrioLOS2FR((UINT16)taskCB->priority);
    pxTaskStatus->pxStackBase = (StackType_t *)taskCB->topOfStack;
    pxTaskStatus->usStackHighWaterMark = prvStackHighWaterMark(taskCB);
#ifdef LOSCFG_WCFS_SCHEDULER
    pxTaskStatus->ulRunTimeCounter = taskCB->runtick;
#endif
    FR_TASK_UNLOCK(intSave);
    xGetFreeStackSpace = pxTaskStatus->usStackHighWaterMark;
    (void)xGetFreeStackSpace;
}

#if (configUSE_APPLICATION_TASK_TAG == 1)
TaskHookFunction_t xTaskGetApplicationTaskTag(TaskHandle_t xTask)
{
    UINT32 taskId;
    TaskHookFunction_t xReturn;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
        xReturn = pxCurrentTCBs[taskId]->pxTaskTag;
    } else {
        xReturn = xTask->pxTaskTag;
    }
    return xReturn;
}

TaskHookFunction_t xTaskGetApplicationTaskTagFromISR(TaskHandle_t xTask)
{
    UINT32 intSave;
    TaskHookFunction_t xReturn;
    FR_TASK_LOCK(intSave);
    xReturn = xTaskGetApplicationTaskTag(xTask);
    FR_TASK_UNLOCK(intSave);
    return xReturn;
}

BaseType_t xTaskCallApplicationTaskHook(TaskHandle_t xTask, void *pvParameter)
{
    UINT32 taskId;
    BaseType_t ret;
    TaskHookFunction_t RunpxTaskTag = NULL;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
        RunpxTaskTag = pxCurrentTCBs[taskId]->pxTaskTag;
    } else {
        RunpxTaskTag = xTask->pxTaskTag;
    }

    if (RunpxTaskTag != NULL) {
        ret = RunpxTaskTag(pvParameter);
    } else {
        ret = pdFAIL;
    }
    return ret;
}

void vTaskSetApplicationTaskTag(TaskHandle_t xTask, TaskHookFunction_t pxTagValue)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
        pxCurrentTCBs[taskId]->pxTaskTag = pxTagValue;
    } else {
        xTask->pxTaskTag = pxTagValue;
    }
}
#endif

#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
    const LosTaskCB *taskCB = OS_TCB_FROM_TID(taskId);
    return prvStackHighWaterMark(taskCB);
}
#endif

#if (INCLUDE_uxTaskGetStackHighWaterMark2 == 1)
configSTACK_DEPTH_TYPE uxTaskGetStackHighWaterMark2(TaskHandle_t xTask)
{
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
    const LosTaskCB *taskCB = OS_TCB_FROM_TID(taskId);
    return (configSTACK_DEPTH_TYPE)prvStackHighWaterMark(taskCB);
}
#endif

#if (configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0)
void vTaskSetThreadLocalStoragePointer(TaskHandle_t xTaskToSet, BaseType_t xIndex, void *pvValue)
{
    UINT32 taskId;
    TaskHandle_t xtaskCB = NULL;
    if (xTaskToSet == NULL) {
        taskId = LOS_CurTaskIDGet();
        xtaskCB = pxCurrentTCBs[taskId];
    } else {
        xtaskCB = xTaskToSet;
    }
    if ((xIndex >= 0) &&
            (xIndex < (BaseType_t) configNUM_THREAD_LOCAL_STORAGE_POINTERS)) {
        if (xtaskCB == NULL) {
            return;
        }
        xtaskCB->pvThreadLocalStoragePointers[xIndex] = pvValue;
    }
}

void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t xTaskToQuery, BaseType_t xIndex)
{
    UINT32 taskId;
    void *pvReturn = NULL;
    TaskHandle_t xtaskCB = NULL;
    if (xTaskToQuery == NULL) {
        taskId = LOS_CurTaskIDGet();
        xtaskCB = pxCurrentTCBs[taskId];
    } else {
        xtaskCB = xTaskToQuery;
    }
    if ((xIndex >= 0) &&
            (xIndex < (BaseType_t) configNUM_THREAD_LOCAL_STORAGE_POINTERS)) {
        if (xtaskCB == NULL) {
            return NULL;
        }

        pvReturn = xtaskCB->pvThreadLocalStoragePointers[xIndex];
    } else {
        pvReturn = NULL;
    }
    return pvReturn;
}
#endif /* configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0 */

void vTaskSetTimeOutState(TimeOut_t * const pxTimeOut)
{
    if (pxTimeOut == NULL) {
        return;
    }
    pxTimeOut->xOverflowCount = xNumOfOverflows;
    pxTimeOut->xTimeOnEntering = LOS_TickCountGet();
}

BaseType_t xTaskCheckForTimeOut(TimeOut_t * const pxTimeOut, TickType_t * const pxTicksToWait)
{
    BaseType_t xReturn;
    if (pxTimeOut == NULL || pxTicksToWait == NULL) {
        return pdTRUE;
    }
        
    const TickType_t xConstTickCount = LOS_TickCountGet();
    const TickType_t xElapsedTime = xConstTickCount - pxTimeOut->xTimeOnEntering;
#if (INCLUDE_vTaskSuspend == 1)
    if (*pxTicksToWait == portMAX_DELAY) {
        /* If INCLUDE_vTaskSuspend is set to 1 and the block time
        * specified is the maximum block time then the task should block
        * indefinitely, and therefore never time out. */
        return pdFALSE;
    }
#endif
    if (xConstTickCount < pxTimeOut->xTimeOnEntering) {
            xReturn = pdTRUE;
            *pxTicksToWait = (TickType_t) 0;
    } else if (xElapsedTime < *pxTicksToWait) {
            /* Not a genuine timeout. Adjust parameters for time remaining. */
            *pxTicksToWait -= xElapsedTime;
            vTaskSetTimeOutState(pxTimeOut);
            xReturn = pdFALSE;
    } else {
            *pxTicksToWait = (TickType_t) 0;
            xReturn = pdTRUE;
    }
    return xReturn;
}

eSleepModeStatus eTaskConfirmSleepModeStatus(void)
{
    // 不支持低功耗模式
    return eAbortSleep;
}

#if ((portCRITICAL_NESTING_IN_TCB == 1 ) || ( configNUMBER_OF_CORES > 1))
void vTaskEnterCritical(void)
{
    portENTER_CRITICAL();
}

void vTaskExitCritical(void)
{
    portEXIT_CRITICAL();
}
#endif

void vTaskStartScheduler(void)
{
    // liteos启动流程已经启动调度
    return;
}

void vTaskEndScheduler(void)
{
    // 此函数目前仅在 x86 实模式 PC 端口中实现。
    return;
}

void vTaskSuspendAll(void)
{
    /* 记录 FreeRTOS 调度挂起层数，保证嵌套 suspend 和 resume 与 LiteOS 调度锁成对。 */
    UINT32 intSave = LOS_IntLock();
    uxSchedulerSuspended++;
    LOS_IntRestore(intSave);
    LOS_TaskLock();
}

BaseType_t xTaskResumeAll(void)
{
    UINT32 intSave = LOS_IntLock();
    if (uxSchedulerSuspended > 0U) {
        uxSchedulerSuspended--;
        LOS_IntRestore(intSave);
        LOS_TaskUnlock();
    } else {
        LOS_IntRestore(intSave);
    }
    return pdFALSE;
}

void vTaskStepTick(TickType_t xTicksToJump)
{
    // 在低功耗休眠后（liteos），更新xTickCount
    xTickCount += xTicksToJump;
}

BaseType_t xTaskCatchUpTicks(TickType_t xTicksToCatchUp)
{
    BaseType_t xYieldOccurred;
    vTaskSuspendAll();
    vTaskStepTick(xTicksToCatchUp);
    xYieldOccurred = xTaskResumeAll();
    return xYieldOccurred;
}

TickType_t xTaskGetTickCountFromISR(void)
{
    return LOS_TickCountGet();
}

#if (configUSE_TASK_NOTIFICATIONS == 1)
static UINT32 wait_notifiedvalue_timeout(TCB_t *taskCB, UBaseType_t uxIndexToWaitOn,
                                         TickType_t xTicksToWait)
{
    if (xTicksToWait == 0) {
        return LOS_NOK;
    }
    TickType_t xStart = xTaskGetTickCount();
    for (;;) {
        UINT32 timeout = LOS_WAIT_FOREVER;
        if (xTicksToWait != portMAX_DELAY) {
            TickType_t elapsed = (TickType_t)(xTaskGetTickCount() - xStart);
            if (elapsed >= xTicksToWait) {
                return LOS_NOK;
            }
            timeout = (UINT32)(xTicksToWait - elapsed);
        }

        /* 将 EVENT 限定为唤醒令牌，避免其位值替代 FreeRTOS 的独立通知状态。 */
        UINT32 ret = LOS_EventRead(&taskCB->notifyEvent, TASK_NOTIFY_WAKE_BIT,
                                   LOS_WAITMODE_OR | LOS_WAITMODE_CLR, timeout);
        UINT32 intSave;
        FR_TASK_LOCK(intSave);
        BaseType_t notified =
            (taskCB->ucNotifyState[uxIndexToWaitOn] == taskNOTIFICATION_RECEIVED) ? pdTRUE : pdFALSE;
        FR_TASK_UNLOCK(intSave);
        if (notified != pdFALSE) {
            return LOS_OK;
        }
        if (ret != TASK_NOTIFY_WAKE_BIT) {
            return ret;
        }
        /* 丢弃超时边界遗留的旧唤醒令牌，避免下一次等待被错误唤醒。 */
    }
}

STATIC UINT32 wake_notifiedvalue(TCB_t *taskCB, UBaseType_t uxIndexToNotify)
{
    (void)uxIndexToNotify;
    return LOS_EventWrite(&taskCB->notifyEvent, TASK_NOTIFY_WAKE_BIT);
}

static BaseType_t prvTaskNotifyValueUpdate(TCB_t *taskCB, UBaseType_t index,
                                           uint32_t value, eNotifyAction action,
                                           uint8_t originalState)
{
    switch (action) {
        case eSetBits:
            taskCB->ulNotifiedValue[index] |= value;
            return pdPASS;
        case eIncrement:
            taskCB->ulNotifiedValue[index]++;
            return pdPASS;
        case eSetValueWithOverwrite:
            taskCB->ulNotifiedValue[index] = value;
            return pdPASS;
        case eSetValueWithoutOverwrite:
            if (originalState == taskNOTIFICATION_RECEIVED) {
                return pdFAIL;
            }
            taskCB->ulNotifiedValue[index] = value;
            return pdPASS;
        case eNoAction:
            return pdPASS;
        default:
            return pdFAIL;
    }
}

static BaseType_t prvTaskGenericNotify(TaskHandle_t xTaskToNotify,
                                       UBaseType_t uxIndexToNotify,
                                       uint32_t ulValue,
                                       eNotifyAction eAction,
                                       uint32_t *pulPreviousNotificationValue,
                                       BaseType_t *pxWasWaiting)
{
    if ((xTaskToNotify == NULL) ||
        (xTaskToNotify->taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) ||
        (uxIndexToNotify >= configTASK_NOTIFICATION_ARRAY_ENTRIES)) {
        return pdFAIL;
    }
    UINT32 intSave;
    BaseType_t taskLockHeld = pdFALSE;

    if (!OS_INT_ACTIVE) {
        LOS_TaskLock();
        taskLockHeld = pdTRUE;
    }
    FR_TASK_LOCK(intSave);
    TCB_t *taskCB = pxCurrentTCBs[xTaskToNotify->taskId];
    if (taskCB != xTaskToNotify) {
        FR_TASK_UNLOCK(intSave);
        if (taskLockHeld != pdFALSE) {
            LOS_TaskUnlock();
        }
        return pdFAIL;
    }
    if (pulPreviousNotificationValue != NULL) {
        *pulPreviousNotificationValue = taskCB->ulNotifiedValue[uxIndexToNotify];
    }

    uint8_t originalState = taskCB->ucNotifyState[uxIndexToNotify];
    taskCB->ucNotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
    BaseType_t xReturn = prvTaskNotifyValueUpdate(taskCB, uxIndexToNotify, ulValue,
                                                  eAction, originalState);
    if (pxWasWaiting != NULL) {
        *pxWasWaiting = (originalState == taskWAITING_NOTIFICATION) ? pdTRUE : pdFALSE;
    }
    FR_TASK_UNLOCK(intSave);
    if (originalState == taskWAITING_NOTIFICATION) {
        (void)wake_notifiedvalue(taskCB, uxIndexToNotify);
    }
    if (taskLockHeld != pdFALSE) {
        LOS_TaskUnlock();
    }
    return xReturn;
}

BaseType_t xTaskGenericNotify(TaskHandle_t xTaskToNotify,
                              UBaseType_t uxIndexToNotify,
                              uint32_t ulValue,
                              eNotifyAction eAction,
                              uint32_t * pulPreviousNotificationValue)
{
    return prvTaskGenericNotify(xTaskToNotify, uxIndexToNotify, ulValue, eAction,
                                pulPreviousNotificationValue, NULL);
}

BaseType_t xTaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify,
                                     UBaseType_t uxIndexToNotify,
                                     uint32_t ulValue,
                                     eNotifyAction eAction,
                                     uint32_t * pulPreviousNotificationValue,
                                     BaseType_t * pxHigherPriorityTaskWoken)
{
    BaseType_t xReturn;
    BaseType_t wasWaiting = pdFALSE;
    BaseType_t shouldYield = pdFALSE;
    UINT32 targetTaskId = (xTaskToNotify != NULL) ? xTaskToNotify->taskId : LOSCFG_BASE_CORE_TSK_LIMIT;

    xReturn = prvTaskGenericNotify(xTaskToNotify, uxIndexToNotify, ulValue, eAction,
                                   pulPreviousNotificationValue, &wasWaiting);
    if ((xReturn == pdPASS) && (wasWaiting != pdFALSE) &&
        (targetTaskId < LOSCFG_BASE_CORE_TSK_LIMIT)) {
        LosTaskCB *targetTask = OS_TCB_FROM_TID(targetTaskId);
        LosTaskCB *currentTask = OsCurrTaskGet();
        shouldYield = ((currentTask != NULL) && (targetTask->priority < currentTask->priority)) ? pdTRUE : pdFALSE;
    }
    if ((pxHigherPriorityTaskWoken != NULL) && (shouldYield != pdFALSE)) {
        *pxHigherPriorityTaskWoken = ((xReturn == pdPASS) && (shouldYield != pdFALSE)) ? pdTRUE : pdFALSE;
    }
    return xReturn;
}

void vTaskGenericNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify,
                                   BaseType_t * pxHigherPriorityTaskWoken)
{
    (void)xTaskGenericNotifyFromISR(xTaskToNotify, uxIndexToNotify, 0U, eIncrement,
        NULL, pxHigherPriorityTaskWoken);
}

uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn,
                                 BaseType_t xClearCountOnExit,
                                 TickType_t xTicksToWait)
{
    uint32_t ulReturn;
    UINT32 intSave;
    if (uxIndexToWaitOn >= configTASK_NOTIFICATION_ARRAY_ENTRIES) {
        return 0;
    }
    UINT32 taskId = LOS_CurTaskIDGet();
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return 0;
    }
    TCB_t *taskCB = pxCurrentTCBs[taskId];
        if (taskCB == NULL) {
        return 0;
    }

        FR_TASK_LOCK(intSave);
    /* Only block if the notification count is not already non-zero. */
    BaseType_t needBlock = pdFALSE;
    if (taskCB->ulNotifiedValue[uxIndexToWaitOn] == 0U) {
        /* Mark this task as waiting for a notification. */
        taskCB->ucNotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
        needBlock = (xTicksToWait > (TickType_t) 0) ? pdTRUE : pdFALSE;
    }
    /* 阻塞前释放自旋锁，避免任务在关中断或持锁状态下挂起。 */
    FR_TASK_UNLOCK(intSave);

    if (needBlock != pdFALSE) {
        (void)wait_notifiedvalue_timeout(taskCB, uxIndexToWaitOn, xTicksToWait);
    }

    FR_TASK_LOCK(intSave);
    ulReturn = taskCB->ulNotifiedValue[uxIndexToWaitOn];
    if (ulReturn != 0U) {
        if (xClearCountOnExit != pdFALSE) {
            taskCB->ulNotifiedValue[uxIndexToWaitOn] = (uint32_t) 0U;
        } else {
            taskCB->ulNotifiedValue[uxIndexToWaitOn] = ulReturn - (uint32_t) 1;
        }
    }

    taskCB->ucNotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
    FR_TASK_UNLOCK(intSave);
    /* 清理超时边界可能到达的唤醒令牌，避免污染下一次通知等待。 */
    (void)LOS_EventClear(&taskCB->notifyEvent, ~TASK_NOTIFY_WAKE_BIT);

    return ulReturn;
}

BaseType_t xTaskGenericNotifyWait(UBaseType_t uxIndexToWaitOn,
                                  uint32_t ulBitsToClearOnEntry,
                                  uint32_t ulBitsToClearOnExit,
                                  uint32_t * pulNotificationValue,
                                  TickType_t xTicksToWait)
{
    BaseType_t xReturn;
    UINT32 intSave;
    if (uxIndexToWaitOn >= configTASK_NOTIFICATION_ARRAY_ENTRIES) {
        return pdFAIL;
    }
    UINT32 taskId = LOS_CurTaskIDGet();
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return pdFAIL;
    }
    TCB_t *taskCB = pxCurrentTCBs[taskId];
        if (taskCB == NULL) {
        return pdFAIL;
    }

    FR_TASK_LOCK(intSave);
    /* Only block if a notification is not already pending. */
    BaseType_t needBlock = pdFALSE;
    if (taskCB->ucNotifyState[uxIndexToWaitOn] != taskNOTIFICATION_RECEIVED) {
        /* Clear bits in the task's notification value as bits may get
        * set by the notifying task or interrupt. This can be used
        * to clear the value to zero. */
        taskCB->ulNotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnEntry;
        /* Mark this task as waiting for a notification. */
        taskCB->ucNotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
        needBlock = (xTicksToWait > (TickType_t) 0) ? pdTRUE : pdFALSE;
    }
    /* 阻塞前释放自旋锁，避免任务在关中断或持锁状态下挂起。 */
    FR_TASK_UNLOCK(intSave);

    if (needBlock != pdFALSE) {
        (void)wait_notifiedvalue_timeout(taskCB, uxIndexToWaitOn, xTicksToWait);
    }

    FR_TASK_LOCK(intSave);
    if (pulNotificationValue != NULL) {
        /* Output the current notification value, which may or may not
        * have changed. */
        *pulNotificationValue = taskCB->ulNotifiedValue[uxIndexToWaitOn];
    }
    /* If ucNotifyValue is set then either the task never entered the
    * blocked state (because a notification was already pending) or the
    * task unblocked because of a notification.  Otherwise the task
    * unblocked because of a timeout. */
    if (taskCB->ucNotifyState[uxIndexToWaitOn] != taskNOTIFICATION_RECEIVED) {
        /* A notification was not received. */
        xReturn = pdFALSE;
    } else {
        /* A notification was already pending or a notification was
        * received while the task was waiting. */
        taskCB->ulNotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnExit;
        xReturn = pdTRUE;
    }

    taskCB->ucNotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
    FR_TASK_UNLOCK(intSave);
    (void)LOS_EventClear(&taskCB->notifyEvent, ~TASK_NOTIFY_WAKE_BIT);

    return xReturn;
}

char *pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    /* 从 LiteOS TCB 获取权威任务名，避免 compat 句柄重复保存名称。 */
    UINT32 taskId = (xTaskToQuery == NULL) ? LOS_CurTaskIDGet() : xTaskToQuery->taskId;
    if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return NULL;
    }
    return OS_TCB_FROM_TID(taskId)->taskName;
}

BaseType_t xTaskGenericNotifyStateClear(TaskHandle_t xTask, UBaseType_t uxIndexToClear)
{
    BaseType_t xReturn;
    UINT32 intSave;

    if (uxIndexToClear >= configTASK_NOTIFICATION_ARRAY_ENTRIES) {
        return pdFAIL;
    }
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
        if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return pdFAIL;
    }
    FR_TASK_LOCK(intSave);
    TCB_t *taskCB = pxCurrentTCBs[taskId];
    if ((taskCB == NULL) || ((xTask != NULL) && (taskCB != xTask))) {
        FR_TASK_UNLOCK(intSave);
        return pdFAIL;
    }
    if (taskCB->ucNotifyState[uxIndexToClear] == taskNOTIFICATION_RECEIVED) {
        taskCB->ucNotifyState[uxIndexToClear] = taskNOT_WAITING_NOTIFICATION;
        xReturn = pdPASS;
    } else {
        xReturn = pdFAIL;
    }
    FR_TASK_UNLOCK(intSave);
    return xReturn;
}

uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t xTask, UBaseType_t uxIndexToClear, uint32_t ulBitsToClear)
{
    uint32_t ulReturn;
    UINT32 intSave;

    if (uxIndexToClear >= configTASK_NOTIFICATION_ARRAY_ENTRIES) {
        return 0U;
    }
    UINT32 taskId;
    if (xTask == NULL) {
        taskId = LOS_CurTaskIDGet();
    } else {
        taskId = xTask->taskId;
    }
        if (taskId >= LOSCFG_BASE_CORE_TSK_LIMIT) {
        return 0U;
    }
    FR_TASK_LOCK(intSave);
    TCB_t *taskCB = pxCurrentTCBs[taskId];
    if ((taskCB == NULL) || ((xTask != NULL) && (taskCB != xTask))) {
        FR_TASK_UNLOCK(intSave);
        return 0U;
    }
    /* Return the notification as it was before the bits were cleared,
    * then clear the bit mask. */
    ulReturn = taskCB->ulNotifiedValue[uxIndexToClear];
    taskCB->ulNotifiedValue[uxIndexToClear] &= ~ulBitsToClear;
    FR_TASK_UNLOCK(intSave);

    return ulReturn;
}

#endif /* configUSE_TASK_NOTIFICATIONS */
