/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2026. All rights
 * reserved. Description : LiteOS adapt FreeRTOS. Author : Huawei LiteOS Team
 * Create : 2026-1-13
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include "FreeRTOS.h"
#include "task.h"
#include "freertos_compat_pri.h"
#include "queue.h"

#include "los_memory.h"
#include "los_sem.h"
#include "los_mux.h"
#include "los_task.h"
#include "los_queue.h"
#include "los_queue_pri.h"
#include "los_sem_pri.h"
#include "los_task_pri.h"
#include "los_typedef.h"
#include "securec.h"

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
#include "los_mux_pri.h"
#endif

#define FR_NAME_MAX 20
#define INVALID_VALUE 0xFFFFFFFF

typedef struct QueuePointers {
    int8_t *pcTail;
    int8_t *pcReadFrom;
} QueuePointers_t;

typedef struct SemaphoreData {
    TaskHandle_t xMutexHolder;
    UBaseType_t uxRecursiveCallCount;
} SemaphoreData_t;

typedef struct QueueDefinition {
    int8_t *pcHead;     /**< Points to the beginning of the queue storage area. */
    union {
        UINT32 queueId;
        UINT32 semId;
        UINT32 muxId;
    } Id;
        /* 保存每个对象的容量，避免计数信号量错误使用 LiteOS 全局上限。 */
    UBaseType_t uxLength;
    UINT32 uxItemSize;
    uint8_t *pucQueueStorage;
    uint8_t ucQueueType;
#if ((configSUPPORT_STATIC_ALLOCATION == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
    uint8_t ucStaticallyAllocated;
#endif
    union {
        QueuePointers_t xQueue;     /**< Data required exclusively when this structure is used as a queue. */
        SemaphoreData_t xSemaphore; /**< Data required exclusively when this structure is used as a semaphore. */
    } u;
    volatile UBaseType_t uxMessagesWaiting; /**< The number of items currently in the queue. */
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    /* Static queues use the caller's ring buffer because LiteOS queue storage
     * requires an additional per-message header. */
    UINT32 staticItemsSemId;
    UINT32 staticSpacesSemId;
    UBaseType_t staticHead;
    UBaseType_t staticTail;
    uint8_t ucUsesStaticStorage;
#endif
#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    UINT32 semInitCount;
    struct QueueDefinition *pxQueueSetContainer;
#endif
} xQUEUE;
typedef xQUEUE Queue_t;

#if (configSUPPORT_STATIC_ALLOCATION == 1)
/* 在编译期保证 StaticQueue_t 足以容纳静态信号量和互斥量的 compat wrapper。 */
typedef char StaticQueueBufferSizeCheck[(sizeof(StaticQueue_t) >= sizeof(Queue_t)) ? 1 : -1];
#endif

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
static UINT32 prvQueueReadable(Queue_t *pxQueue);
#endif

static UINT32 los_err_to_freertos(UINT32 los_err)
{
    if (los_err == LOS_OK) {
        return pdPASS;
    }
    switch (los_err) {
        case LOS_ERRNO_QUEUE_ISFULL:
            return errQUEUE_FULL;
        case LOS_ERRNO_QUEUE_TIMEOUT:
            return pdFAIL;
        case LOS_ERRNO_QUEUE_ISEMPTY:
            return pdFAIL;
        default:
            return pdFAIL;
    }
}

static BaseType_t prvWaitListWakesHigherPriority(LOS_DL_LIST *waitList)
{
    if ((waitList == NULL) || LOS_ListEmpty(waitList)) {
        return pdFALSE;
    }
    LosTaskCB *waiter = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(waitList));
    LosTaskCB *current = OsCurrTaskGet();
    if (current == NULL) {
        return pdFALSE;
    }
    return (waiter->priority < current->priority) ? pdTRUE : pdFALSE;
}

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
static BaseType_t prvQueueSetWakesHigherPriority(const Queue_t *pxQueue, BaseType_t xCopyPosition)
{
    if (pxQueue->pxQueueSetContainer == NULL) {
        return pdFALSE;
    }
    if ((xCopyPosition == queueOVERWRITE) &&
        (pxQueue->ucQueueType == queueQUEUE_TYPE_BASE) &&
        (prvQueueReadable((Queue_t *)pxQueue) != 0U)) {
        return pdFALSE;
    }
    LosQueueCB *setCB = GET_QUEUE_HANDLE(pxQueue->pxQueueSetContainer->Id.queueId);
    return prvWaitListWakesHigherPriority(&setCB->readWriteList[OS_QUEUE_READ]);
}
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static inline BOOL prvCreateParaCheck(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize,
    uint8_t *pucQueueStorage, StaticQueue_t *pxStaticQueue)
{
    if ((pxStaticQueue == NULL) || (uxQueueLength == 0U) || (uxQueueLength > UINT16_MAX) ||
        (sizeof(StaticQueue_t) < sizeof(Queue_t))) {
        return false;
    }

    if ((pucQueueStorage == NULL) != (uxItemSize == 0U)) {
        return false;
    }

    return true;
}

QueueHandle_t xQueueGenericCreateStatic(const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize,
    uint8_t *pucQueueStorage, StaticQueue_t *pxStaticQueue, const uint8_t ucQueueType)
{
    Queue_t *pxNewQueue = NULL;
    UINT32 count = uxQueueLength;
    UINT32 ret = LOS_OK;

    if (!prvCreateParaCheck(uxQueueLength, uxItemSize, pucQueueStorage, pxStaticQueue)) {
        return NULL;
    }

    /* 复用 StaticQueue_t 保存静态信号量和互斥量 wrapper，保证 compat 层不额外使用堆。 */
    pxNewQueue = (Queue_t *)pxStaticQueue;
    if (memset_s(pxNewQueue, sizeof(StaticQueue_t), 0, sizeof(Queue_t)) != EOK) {
        return NULL;
    }

    if (ucQueueType == queueQUEUE_TYPE_BASE) {
                ret = LOS_SemCreate(0U, &pxNewQueue->staticItemsSemId);
        if (ret == LOS_OK) {
            ret = LOS_SemCreate((UINT16)uxQueueLength, &pxNewQueue->staticSpacesSemId);
            if (ret != LOS_OK) {
                (void)LOS_SemDelete(pxNewQueue->staticItemsSemId);
            }
        }
        if (ret == LOS_OK) {
            pxNewQueue->ucUsesStaticStorage = pdTRUE;
        }
    } else if ((ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX) ||
               (ucQueueType == queueQUEUE_TYPE_MUTEX)) {
        ret = LOS_MuxCreate(&pxNewQueue->Id.muxId);
    } else if (ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        ret = LOS_SemCreate(count, &pxNewQueue->Id.semId);
    } else if (ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) {
        /* FreeRTOS 二值信号量创建后语义为"空"(count=0)，必须先 give 才能 take。
         * 不能用 count(=uxQueueLength=1) 建成满的，否则首次 give 触发 LiteOS SEM_OVERFLOW。 */
        ret = LOS_BinarySemCreate(0, &pxNewQueue->Id.semId);
    } else {
        return NULL;
    }
    if (ret != LOS_OK) {
        return NULL;
    }
    pxNewQueue->uxItemSize = (UINT32)uxItemSize;
    pxNewQueue->uxLength = uxQueueLength;
        pxNewQueue->pucQueueStorage = pucQueueStorage;
    pxNewQueue->ucQueueType = ucQueueType;

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    pxNewQueue->semInitCount = count;
    pxNewQueue->pxQueueSetContainer = NULL;
#endif
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    pxNewQueue->ucStaticallyAllocated = pdTRUE;
#endif
    return pxNewQueue;
}
#endif

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
QueueHandle_t xQueueGenericCreate(
    const UBaseType_t uxQueueLength, const UBaseType_t uxItemSize, const uint8_t ucQueueType)
{
    Queue_t *pxNewQueue = NULL;
    UINT32 ret = LOS_OK;
    UINT32 count = uxQueueLength;
    char name[FR_NAME_MAX] = {0};

    /* 提前拒绝无效长度和容量溢出，避免除零或错误分配。 */
        if ((uxQueueLength == 0U) || (uxQueueLength > UINT16_MAX)) {
        return NULL;
    }
    if ((SIZE_MAX / uxQueueLength) < uxItemSize) {
        return NULL;
    }

    if ((UBaseType_t)(SIZE_MAX - sizeof(Queue_t)) < (uxQueueLength * uxItemSize)) {
        return NULL;
    }

    pxNewQueue = (Queue_t *)LOS_MemAlloc(OS_SYS_MEM_ADDR, sizeof(Queue_t));
    if (pxNewQueue == NULL) {
        return NULL;
    }
    /* 清除堆内存中的随机值，保证队列类型和所有权状态初始有效。 */
    if (memset_s(pxNewQueue, sizeof(Queue_t), 0, sizeof(Queue_t)) != EOK) {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxNewQueue);
        return NULL;
    }

    if (ucQueueType == queueQUEUE_TYPE_BASE) {
        ret = LOS_QueueCreate(name, uxQueueLength, &pxNewQueue->Id.queueId, 0, uxItemSize);
    } else if (ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX || ucQueueType == queueQUEUE_TYPE_MUTEX) {
        ret = LOS_MuxCreate(&pxNewQueue->Id.muxId);
    } else if (ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        ret = LOS_SemCreate(count, &pxNewQueue->Id.semId);
    } else if (ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) {
        /* FreeRTOS 二值信号量创建后语义为"空"(count=0)，必须先 give 才能 take。
         * 不能用 count(=uxQueueLength=1) 建成满的，否则首次 give 触发 LiteOS SEM_OVERFLOW。 */
        ret = LOS_BinarySemCreate(0, &pxNewQueue->Id.semId);
    } else {
        (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxNewQueue);
        return NULL;
    }
    if (ret != LOS_OK) {
        LOS_MemFree(OS_SYS_MEM_ADDR, pxNewQueue);
        return NULL;
    }
    pxNewQueue->uxItemSize = (UINT32)uxItemSize;
    pxNewQueue->uxLength = uxQueueLength;
    pxNewQueue->ucQueueType = ucQueueType;
#if ((configSUPPORT_STATIC_ALLOCATION == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
    pxNewQueue->ucStaticallyAllocated = pdFALSE;
#endif

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    pxNewQueue->semInitCount = count;
    pxNewQueue->pxQueueSetContainer = NULL;
#endif

    return (QueueHandle_t)pxNewQueue;
}
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static UINT32 prvStaticQueueDelete(Queue_t *pxQueue)
{
    UINT32 intSave;
    UINT32 itemsRet;
    UINT32 spacesRet;

    LOS_TaskLock();
    SCHEDULER_LOCK(intSave);
    BOOL hasWaiter = (!LOS_ListEmpty(&GET_SEM(pxQueue->staticItemsSemId)->semList) ||
                      !LOS_ListEmpty(&GET_SEM(pxQueue->staticSpacesSemId)->semList));
    SCHEDULER_UNLOCK(intSave);
    if (hasWaiter != FALSE) {
        LOS_TaskUnlock();
        return LOS_NOK;
    }
    itemsRet = LOS_SemDelete(pxQueue->staticItemsSemId);
    spacesRet = LOS_SemDelete(pxQueue->staticSpacesSemId);
    LOS_TaskUnlock();
    return ((itemsRet == LOS_OK) && (spacesRet == LOS_OK)) ? LOS_OK : LOS_NOK;
}
#endif

static UINT32 prvQueueDeleteKernelObject(Queue_t *pxQueue)
{
    if (pxQueue->ucQueueType == queueQUEUE_TYPE_BASE) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            return prvStaticQueueDelete(pxQueue);
        }
#endif
        return LOS_QueueDelete(pxQueue->Id.queueId);
    }
    if ((pxQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX) ||
        (pxQueue->ucQueueType == queueQUEUE_TYPE_MUTEX)) {
        return LOS_MuxDelete(pxQueue->Id.muxId);
    }
    if ((pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) ||
        (pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE)) {
        return LOS_SemDelete(pxQueue->Id.semId);
    }
    return LOS_NOK;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    Queue_t *const pxQueue = xQueue;

    if (pxQueue == NULL) {
        return;
    }
    if (prvQueueDeleteKernelObject(pxQueue) != LOS_OK) {
        return;
    }
#if ((configSUPPORT_STATIC_ALLOCATION == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
    /* 保留调用者拥有的 StaticQueue_t，只释放相应 LiteOS 内核对象。 */
    if (pxQueue->ucStaticallyAllocated == pdTRUE) {
        (void)memset_s(pxQueue, sizeof(StaticQueue_t), 0, sizeof(Queue_t));
        return;
    }
#endif
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    (void)LOS_MemFree(OS_SYS_MEM_ADDR, pxQueue);
#endif
}

static UINT32 prvQueueOverwrite(Queue_t *pxQueue, const void *pvItemToQueue)
{
    /* 保证长度为 1 的队列原子替换旧值，避免出队再入队产生空窗和乱序。 */
    for (;;) {
        UINT32 ret = LOS_QueueWriteHeadCopy(pxQueue->Id.queueId, (VOID *)pvItemToQueue,
                                            pxQueue->uxItemSize, LOS_NO_WAIT);
        if (ret != LOS_ERRNO_QUEUE_ISFULL) {
            return ret;
        }

        LosQueueCB *queueCB = GET_QUEUE_HANDLE(pxQueue->Id.queueId);
        UINT32 intSave = LOS_IntLock();
        if (queueCB->readWriteableCnt[OS_QUEUE_READ] != 0U) {
            UINT16 head = queueCB->queueHead;
            UINT8 *headNode = &queueCB->queueHandle[head * sizeof(QueueMsgHead)];
            UINT8 *dataNode = &queueCB->queueHandle[queueCB->queueLen * sizeof(QueueMsgHead) +
                                                    head * queueCB->queueSize];
            if (memcpy_s(dataNode, queueCB->queueSize, pvItemToQueue, pxQueue->uxItemSize) != EOK) {
                LOS_IntRestore(intSave);
                return LOS_NOK;
            }
            *((QueueMsgHead *)(UINTPTR)headNode) = (QueueMsgHead)pxQueue->uxItemSize;
            LOS_IntRestore(intSave);
            return LOS_OK;
        }
        LOS_IntRestore(intSave);
        /* 处理判满后被并发接收者取空的竞争，避免伪造队列状态。 */
    }
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static UINT32 prvStaticQueueTakeToken(UINT32 semId, TickType_t ticksToWait)
{
    if (!OS_INT_ACTIVE) {
        return LOS_SemPend(semId, (UINT32)ticksToWait);
    }
    if (ticksToWait != 0U) {
        return LOS_NOK;
    }

    UINT32 intSave;
    UINT32 ret = LOS_NOK;
    SCHEDULER_LOCK(intSave);
    LosSemCB *semCB = GET_SEM(semId);
    if ((semCB->semStat == LOS_USED) && (semCB->semCount > 0U)) {
        semCB->semCount--;
        ret = LOS_OK;
    }
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

static UINT32 prvStaticQueueSend(Queue_t *pxQueue, const void *item, TickType_t ticksToWait,
                                 BaseType_t copyPosition)
{
    if ((item == NULL) || (prvStaticQueueTakeToken(pxQueue->staticSpacesSemId, ticksToWait) != LOS_OK)) {
        return LOS_NOK;
    }

    UINT32 intSave = LOS_IntLock();
    UBaseType_t position;
    if (copyPosition == queueSEND_TO_FRONT) {
        position = (pxQueue->staticHead == 0U) ?
            (pxQueue->uxLength - 1U) : (pxQueue->staticHead - 1U);
    } else if (copyPosition == queueOVERWRITE) {
                position = pxQueue->staticTail;
    } else {
        position = pxQueue->staticTail;
    }
    UINT8 *destination = pxQueue->pucQueueStorage + (position * pxQueue->uxItemSize);
    if (memcpy_s(destination, pxQueue->uxItemSize, item, pxQueue->uxItemSize) != EOK) {
        LOS_IntRestore(intSave);
        (void)LOS_SemPost(pxQueue->staticSpacesSemId);
        return LOS_NOK;
    }
        if (copyPosition == queueSEND_TO_FRONT) {
        pxQueue->staticHead = position;
    } else {
        pxQueue->staticTail = (pxQueue->staticTail + 1U) % pxQueue->uxLength;
    }
    pxQueue->uxMessagesWaiting++;
    LOS_IntRestore(intSave);
    return LOS_SemPost(pxQueue->staticItemsSemId);
}

static UINT32 prvStaticQueueReceive(Queue_t *pxQueue, void *buffer, TickType_t ticksToWait)
{
    if ((buffer == NULL) || (prvStaticQueueTakeToken(pxQueue->staticItemsSemId, ticksToWait) != LOS_OK)) {
        return LOS_NOK;
    }

    UINT32 intSave = LOS_IntLock();
    UINT8 *source = pxQueue->pucQueueStorage + (pxQueue->staticHead * pxQueue->uxItemSize);
    if (memcpy_s(buffer, pxQueue->uxItemSize, source, pxQueue->uxItemSize) != EOK) {
        LOS_IntRestore(intSave);
        (void)LOS_SemPost(pxQueue->staticItemsSemId);
        return LOS_NOK;
    }
    pxQueue->staticHead = (pxQueue->staticHead + 1U) % pxQueue->uxLength;
    pxQueue->uxMessagesWaiting--;
    LOS_IntRestore(intSave);
    return LOS_SemPost(pxQueue->staticSpacesSemId);
}
#endif

static UINT32 xOnlyQueueSend(
    QueueHandle_t pxQueue, const void *const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
    UINT32 ret;
    if (pvItemToQueue == NULL || pxQueue->uxItemSize == (UBaseType_t)0U) {
        return LOS_NOK;
    }
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    if (pxQueue->ucUsesStaticStorage != pdFALSE) {
        if ((xCopyPosition == queueOVERWRITE) && (pxQueue->uxLength == 1U)) {
            UINT32 intSave = LOS_IntLock();
            if (pxQueue->uxMessagesWaiting != 0U) {
                UINT32 copyRet = memcpy_s(pxQueue->pucQueueStorage, pxQueue->uxItemSize,
                                          pvItemToQueue, pxQueue->uxItemSize);
                LOS_IntRestore(intSave);
                return (copyRet == EOK) ? LOS_OK : LOS_NOK;
            }
            LOS_IntRestore(intSave);
        }
        return prvStaticQueueSend(pxQueue, pvItemToQueue, xTicksToWait, xCopyPosition);
    }
#endif
    if (xCopyPosition == queueSEND_TO_BACK) {
        ret = LOS_QueueWriteCopy(pxQueue->Id.queueId, (VOID *)pvItemToQueue, pxQueue->uxItemSize, xTicksToWait);
    } else if (xCopyPosition == queueSEND_TO_FRONT) {
        ret = LOS_QueueWriteHeadCopy(pxQueue->Id.queueId, (VOID *)pvItemToQueue, pxQueue->uxItemSize, xTicksToWait);
    } else if (xCopyPosition == queueOVERWRITE) {
        /* 限制 overwrite 仅用于长度为 1 的队列，确保普通路径与 ISR 路径都不引入分配和竞争窗口。 */
        configASSERT(pxQueue->uxLength == 1U);
        if (pxQueue->uxLength != 1U) {
            return LOS_NOK;
        }
        ret = prvQueueOverwrite(pxQueue, pvItemToQueue);
    } else {
        return LOS_NOK;
    }
    return ret;
}

static UINT32 prvQueueGiveMutex(Queue_t *pxQueue)
{
    UINT32 ret = LOS_MuxPost(pxQueue->Id.muxId);
    if (ret == LOS_OK) {
        LosMuxCB *muxCB = GET_MUX(pxQueue->Id.muxId);
        pxQueue->u.xSemaphore.xMutexHolder = (muxCB->owner == NULL) ?
            NULL : xTaskGetHandleByKernelTaskId(muxCB->owner->taskId);
    }
    return ret;
}

static UINT32 prvQueueGiveSemaphore(Queue_t *pxQueue)
{
    if (pxQueue->ucQueueType != queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        return LOS_SemPost(pxQueue->Id.semId);
    }

    UINT32 intSave = LOS_IntLock();
    if (GET_SEM(pxQueue->Id.semId)->semCount >= pxQueue->uxLength) {
        LOS_IntRestore(intSave);
        return INVALID_VALUE;
    }
    UINT32 ret = LOS_SemPost(pxQueue->Id.semId);
    LOS_IntRestore(intSave);
    return ret;
}

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
static BaseType_t prvQueueSetNotify(Queue_t *pxQueue, UINT32 ret, UINT32 dataCountBefore,
                                    UINT32 dataCountAfter, BaseType_t xCopyPosition,
                                    TickType_t xTicksToWait)
{
    if ((ret != LOS_OK) || (pxQueue->pxQueueSetContainer == NULL) ||
        ((xCopyPosition == queueOVERWRITE) && (dataCountBefore == dataCountAfter))) {
        return los_err_to_freertos(ret);
    }

    Queue_t *pxQueueSet = pxQueue->pxQueueSetContainer;
    VOID *queueMember = pxQueue;
    ret = LOS_QueueWriteCopy(pxQueueSet->Id.queueId, &queueMember,
                             pxQueueSet->uxItemSize, xTicksToWait);
    return los_err_to_freertos(ret);
}
#endif

BaseType_t xQueueGenericSend(
    QueueHandle_t xQueue, const void *const pvItemToQueue, TickType_t xTicksToWait, const BaseType_t xCopyPosition)
{
    Queue_t *const pxQueue = xQueue;
    UINT32 ret;
#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    UINT32 dataCountBefore = 0, dataCountAfter = 0;
#endif

    if (pxQueue == NULL) {
        return pdFAIL;
    }

    if (pxQueue->ucQueueType == queueQUEUE_TYPE_BASE) {  // 队列
#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
        dataCountBefore = prvQueueReadable(pxQueue);
        ret = xOnlyQueueSend(pxQueue, pvItemToQueue, xTicksToWait, xCopyPosition);
        dataCountAfter = prvQueueReadable(pxQueue);
#else
        ret = xOnlyQueueSend(pxQueue, pvItemToQueue, xTicksToWait, xCopyPosition);
#endif
    } else if ((pxQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX) ||
               (pxQueue->ucQueueType == queueQUEUE_TYPE_MUTEX)) { // 互斥量
        ret = prvQueueGiveMutex(pxQueue);
    } else if ((pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) ||
               (pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE)) { // 信号量
        ret = prvQueueGiveSemaphore(pxQueue);
        if (ret == INVALID_VALUE) {
            return pdFAIL;
        }
    } else {
        return pdFAIL;
    }

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    return prvQueueSetNotify(pxQueue, ret, dataCountBefore, dataCountAfter,
                             xCopyPosition, xTicksToWait);
#else
    return los_err_to_freertos(ret);
#endif
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
{
    Queue_t *const pxQueue = xQueue;
    UINT32 ret;

    if (pxQueue == NULL) {
        return pdFAIL;
    }

    if (xQueue->ucQueueType == queueQUEUE_TYPE_BASE) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            ret = prvStaticQueueReceive(pxQueue, pvBuffer, xTicksToWait);
        } else
#endif
        {
        ret = LOS_QueueReadCopy(pxQueue->Id.queueId, pvBuffer, &pxQueue->uxItemSize, xTicksToWait);
        }
    } else if (xQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX ||
        xQueue->ucQueueType == queueQUEUE_TYPE_MUTEX) {
        ret = LOS_MuxPend(pxQueue->Id.muxId, (UINT32)xTicksToWait);
    } else if (pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE || \
                pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        ret = LOS_SemPend(pxQueue->Id.semId, (UINT32)xTicksToWait);
    } else {
        return pdFAIL;
    }
    return los_err_to_freertos(ret);
}

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue)
{
    Queue_t *const pxQueue = xQueue;

    if (pxQueue == NULL) {
        return pdFAIL;
    }
    /* 按 FreeRTOS 契约统一计算队列、信号量和互斥量的剩余空间。 */
    UBaseType_t uxMessages = uxQueueMessagesWaiting(xQueue);
    return (uxMessages <= pxQueue->uxLength) ? (pxQueue->uxLength - uxMessages) : 0;
}

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue)
{
    UBaseType_t uxReturn = INVALID_VALUE;
    Queue_t *const pxQueue = xQueue;
    QUEUE_INFO_S queueInfo;

    if (pxQueue == NULL) {
        return pdFAIL;
    }

    if (xQueue->ucQueueType == queueQUEUE_TYPE_BASE) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            UINT32 intSave = LOS_IntLock();
            uxReturn = pxQueue->uxMessagesWaiting;
            LOS_IntRestore(intSave);
            return uxReturn;
        }
#endif
        UINT32 ret = LOS_QueueInfoGet(pxQueue->Id.queueId, &queueInfo);
        if (ret == LOS_OK) {
            uxReturn = queueInfo.usReadableCnt;
        }
    } else if (xQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX ||
        xQueue->ucQueueType == queueQUEUE_TYPE_MUTEX) {
        LosMuxCB *muxInfo = GET_MUX(pxQueue->Id.muxId);
                uxReturn = (muxInfo->muxCount == 0) ? 1 : 0;
    } else if (xQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE ||
        xQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        LosSemCB *semInfo = GET_SEM(pxQueue->Id.semId);
        uxReturn = semInfo->semCount;
    } else {
        return pdFAIL;
    }
    return uxReturn;
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t xQueue, const void *const pvItemToQueue,
    BaseType_t *const pxHigherPriorityTaskWoken, const BaseType_t xCopyPosition)
{
    BaseType_t shouldYield = pdFALSE;
    Queue_t *pxQueue = (Queue_t *)xQueue;

        if (pxQueue == NULL) {
        return pdFAIL;
    }
        if ((pxQueue->ucQueueType == queueQUEUE_TYPE_MUTEX) ||
        (pxQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX)) {
        return pdFAIL;
    }

    UINT32 intSave = LOS_IntLock();
    if (pxQueue->ucQueueType == queueQUEUE_TYPE_BASE) {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            shouldYield = prvWaitListWakesHigherPriority(&GET_SEM(pxQueue->staticItemsSemId)->semList);
        } else
#endif
        {
        LosQueueCB *queueCB = GET_QUEUE_HANDLE(pxQueue->Id.queueId);
        shouldYield = prvWaitListWakesHigherPriority(&queueCB->readWriteList[OS_QUEUE_READ]);
        }
    } else if ((pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) ||
               (pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE)) {
        shouldYield = prvWaitListWakesHigherPriority(&GET_SEM(pxQueue->Id.semId)->semList);
    }
#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    if (prvQueueSetWakesHigherPriority(pxQueue, xCopyPosition) != pdFALSE) {
        shouldYield = pdTRUE;
    }
#endif
    LOS_IntRestore(intSave);

    BaseType_t ret = xQueueGenericSend(xQueue, pvItemToQueue, 0, xCopyPosition);
    if ((pxHigherPriorityTaskWoken != NULL) && (ret == pdPASS) && (shouldYield != pdFALSE)) {
                *pxHigherPriorityTaskWoken = pdTRUE;
    }
    return ret;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *const pvBuffer, BaseType_t *const pxHigherPriorityTaskWoken)
{
    Queue_t *pxQueue = (Queue_t *)xQueue;
    BaseType_t shouldYield = pdFALSE;
        if (pxQueue == NULL) {
        return pdFAIL;
    }

    if (pxQueue->ucQueueType == queueQUEUE_TYPE_BASE) {
        UINT32 intSave = LOS_IntLock();
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            shouldYield = prvWaitListWakesHigherPriority(&GET_SEM(pxQueue->staticSpacesSemId)->semList);
        } else
#endif
        {
        LosQueueCB *queueCB = GET_QUEUE_HANDLE(pxQueue->Id.queueId);
        shouldYield = prvWaitListWakesHigherPriority(&queueCB->readWriteList[OS_QUEUE_WRITE]);
        }
        LOS_IntRestore(intSave);
        BaseType_t ret = xQueueReceive(xQueue, pvBuffer, 0);
        if ((pxHigherPriorityTaskWoken != NULL) && (ret == pdPASS) && (shouldYield != pdFALSE)) {
            *pxHigherPriorityTaskWoken = pdTRUE;
        }
        return ret;
    }

    if ((pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) ||
        (pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE)) {
                UINT32 intSave;
        BaseType_t ret = pdFAIL;
        SCHEDULER_LOCK(intSave);
        LosSemCB *semCB = GET_SEM(pxQueue->Id.semId);
        if ((semCB->semStat == LOS_USED) && (semCB->semCount > 0U)) {
            semCB->semCount--;
            ret = pdPASS;
        }
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    return pdFAIL;
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
static BaseType_t prvStaticQueueReset(Queue_t *pxQueue)
{
    UINT32 intSave;

    LOS_TaskLock();
    SCHEDULER_LOCK(intSave);
    pxQueue->staticHead = 0U;
    pxQueue->staticTail = 0U;
    pxQueue->uxMessagesWaiting = 0U;
    GET_SEM(pxQueue->staticItemsSemId)->semCount = 0U;
    GET_SEM(pxQueue->staticSpacesSemId)->semCount = 0U;
    SCHEDULER_UNLOCK(intSave);
    for (UBaseType_t i = 0; i < pxQueue->uxLength; i++) {
        (void)LOS_SemPost(pxQueue->staticSpacesSemId);
    }
    LOS_TaskUnlock();
    return pdPASS;
}
#endif

static BaseType_t prvDynamicQueueReset(Queue_t *pxQueue)
{
    QUEUE_INFO_S queueInfo;
    if (LOS_QueueInfoGet(pxQueue->Id.queueId, &queueInfo) != LOS_OK) {
        return pdFAIL;
    }

    UINT32 bufferSize = queueInfo.usQueueSize;
    void *pvBuffer = LOS_MemAlloc(OS_SYS_MEM_ADDR, bufferSize);
    if (pvBuffer == NULL) {
        return pdFAIL;
    }

    BaseType_t result = pdPASS;
    LOS_TaskLock();
    for (;;) {
        UINT32 readSize = bufferSize;
        UINT32 ret = LOS_QueueReadCopy(pxQueue->Id.queueId, pvBuffer, &readSize, 0);
        if (ret == LOS_ERRNO_QUEUE_ISEMPTY) {
            break;
        }
        if (ret != LOS_OK) {
            result = pdFAIL;
            break;
        }
    }
    LOS_TaskUnlock();
    (void)LOS_MemFree(OS_SYS_MEM_ADDR, pvBuffer);
    return result;
}

BaseType_t xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue)
{
    Queue_t *const pxQueue = xQueue;

    (void)xNewQueue;
    if ((pxQueue == NULL) || (pxQueue->ucQueueType != queueQUEUE_TYPE_BASE)) {
        return pdFAIL;
    }
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    if (pxQueue->ucUsesStaticStorage != pdFALSE) {
        return prvStaticQueueReset(pxQueue);
    }
#endif
    return prvDynamicQueueReset(pxQueue);
}

static BaseType_t prvQueuePeekAtHead(Queue_t *pxQueue, void *pvBuffer)
{
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    if (pxQueue->ucUsesStaticStorage != pdFALSE) {
        if (prvStaticQueueTakeToken(pxQueue->staticItemsSemId, 0U) != LOS_OK) {
            return pdFALSE;
        }
        UINT32 intSave = LOS_IntLock();
        UINT8 *source = pxQueue->pucQueueStorage + (pxQueue->staticHead * pxQueue->uxItemSize);
        BaseType_t result = (memcpy_s(pvBuffer, pxQueue->uxItemSize, source,
                                      pxQueue->uxItemSize) == EOK) ? pdTRUE : pdFALSE;
        LOS_IntRestore(intSave);
        (void)LOS_SemPost(pxQueue->staticItemsSemId);
        return result;
    }
#endif
    /* 在不改变队列索引和计数的前提下读取队首，补足 LiteOS 缺少的非破坏性 peek。 */
    LosQueueCB *queueCB = GET_QUEUE_HANDLE(pxQueue->Id.queueId);
    UINT32 intSave = LOS_IntLock();

    /* 空队列直接失败，避免访问无效队首。 */
    if (queueCB->readWriteableCnt[OS_QUEUE_READ] == 0) {
        LOS_IntRestore(intSave);
        return pdFALSE;
    }

    UINT16 head = queueCB->queueHead;
    UINT8 *headNode = &queueCB->queueHandle[head * sizeof(QueueMsgHead)];
    UINT8 *dataNode = &queueCB->queueHandle[queueCB->queueLen * sizeof(QueueMsgHead) +
                                            head * queueCB->queueSize];
    QueueMsgHead msgLen = *((QueueMsgHead *)(UINTPTR)headNode);

    if (memcpy_s(pvBuffer, msgLen, dataNode, msgLen) != EOK) {
        LOS_IntRestore(intSave);
        return pdFALSE;
    }
    LOS_IntRestore(intSave);
    return pdTRUE;
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *const pvBuffer, TickType_t xTicksToWait)
{
    Queue_t *const pxQueue = (Queue_t *)xQueue;

    if (pxQueue == NULL || pvBuffer == NULL) {
        return pdFAIL;
    }
    if (pxQueue->ucQueueType != queueQUEUE_TYPE_BASE || pxQueue->uxItemSize == (UBaseType_t)0U) {
        return pdFAIL;
    }
        TickType_t xStart = xTaskGetTickCount();
    do {
        if (prvQueuePeekAtHead(pxQueue, pvBuffer) == pdTRUE) {
            return pdPASS;
        }
        if (xTicksToWait == 0) {
            break;
        }
        vTaskDelay(1);
    } while ((xTicksToWait == portMAX_DELAY) ||
             ((TickType_t)(xTaskGetTickCount() - xStart) < xTicksToWait));

    return pdFAIL;
}

BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void *const pvBuffer)
{
    Queue_t *const pxQueue = (Queue_t *)xQueue;

    if (pxQueue == NULL || pvBuffer == NULL) {
        return pdFALSE;
    }
    if (pxQueue->ucQueueType != queueQUEUE_TYPE_BASE || pxQueue->uxItemSize == (UBaseType_t)0U) {
        return pdFALSE;
    }
    return prvQueuePeekAtHead(pxQueue, pvBuffer);
}

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue)
{
    BaseType_t xReturn;
    if (uxQueueMessagesWaiting(xQueue) == (UBaseType_t)0) {
        xReturn = pdTRUE;
    } else {
        xReturn = pdFALSE;
    }

    return xReturn;
}

BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t xQueue)
{
    BaseType_t xReturn;

    if (uxQueueSpacesAvailable(xQueue) == (UBaseType_t)0) {
        xReturn = pdTRUE;
    } else {
        xReturn = pdFALSE;
    }
    return xReturn;
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xQueueGenericGetStaticBuffers(
    QueueHandle_t xQueue, uint8_t **ppucQueueStorage, StaticQueue_t **ppxStaticQueue)
{
    BaseType_t xReturn = pdFALSE;
    Queue_t *const pxQueue = xQueue;
        if ((pxQueue == NULL) || (ppxStaticQueue == NULL)) {
        return pdFAIL;
    }
    #if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    {
        if (pxQueue->ucStaticallyAllocated == (uint8_t)pdTRUE) {
            if (ppucQueueStorage != NULL) {
                *ppucQueueStorage = pxQueue->pucQueueStorage;
            }
            *ppxStaticQueue = (StaticQueue_t *)pxQueue;
            xReturn = pdTRUE;
        } else {
            xReturn = pdFALSE;
        }
    }
    #else
    {
        if (ppucQueueStorage != NULL) {
            *ppucQueueStorage = pxQueue->pucQueueStorage;
        }
        *ppxStaticQueue = (StaticQueue_t *) pxQueue;
        xReturn = pdTRUE;
    }
    #endif
    return xReturn;
}
#endif

UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue)
{
    return uxQueueMessagesWaiting(xQueue);
}

UBaseType_t uxQueueGetQueueItemSize(QueueHandle_t xQueue)
{
    return (xQueue == NULL) ? 0U : (UBaseType_t)xQueue->uxItemSize;
}

UBaseType_t uxQueueGetQueueLength(QueueHandle_t xQueue)
{
    return (xQueue == NULL) ? 0U : xQueue->uxLength;
}

BaseType_t xQueueGiveFromISR(QueueHandle_t xQueue, BaseType_t *const pxHigherPriorityTaskWoken)
{
    return xQueueGenericSendFromISR(xQueue, NULL, pxHigherPriorityTaskWoken, queueSEND_TO_BACK);
}

#define SEMAPHORE_QUEUE_ITEM_LENGTH    ((UBaseType_t) 0)
#define MUTEX_GIVE_BLOCK_TIME          ((TickType_t) 0U)

/* 创建计数信号量（动态） */
#if (configUSE_COUNTING_SEMAPHORES == 1)
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount)
{
        if ((uxMaxCount == 0U) || (uxInitialCount > uxMaxCount)) {
        return NULL;
    }

    QueueHandle_t xHandle = xQueueGenericCreate(uxMaxCount, SEMAPHORE_QUEUE_ITEM_LENGTH, \
        queueQUEUE_TYPE_COUNTING_SEMAPHORE);
    if (xHandle != NULL) {
        /* 同时维护 FreeRTOS 初始计数和每对象最大值，避免 LiteOS 单一 count 参数丢失上限语义。 */
        Queue_t *queue = (Queue_t *)xHandle;
        UINT32 intSave = LOS_IntLock();
        GET_SEM(queue->Id.semId)->semCount = (UINT16)uxInitialCount;
        LOS_IntRestore(intSave);
    }

    return xHandle;
}
#endif /* (configSUPPORT_DYNAMIC_ALLOCATION == 1) */

#if (configSUPPORT_STATIC_ALLOCATION == 1)
QueueHandle_t xQueueCreateCountingSemaphoreStatic(const UBaseType_t uxMaxCount,
                                                  const UBaseType_t uxInitialCount,
                                                  StaticQueue_t *pxStaticQueue)
{
    if ((uxMaxCount == 0U) || (uxInitialCount > uxMaxCount)) {
        return NULL;
    }
    QueueHandle_t xHandle = xQueueGenericCreateStatic(uxMaxCount, SEMAPHORE_QUEUE_ITEM_LENGTH, NULL, \
        pxStaticQueue, queueQUEUE_TYPE_COUNTING_SEMAPHORE);
    if (xHandle != NULL) {
        /* 保持静态与动态计数信号量的初始值和上限语义一致。 */
        Queue_t *queue = (Queue_t *)xHandle;
        UINT32 intSave = LOS_IntLock();
        GET_SEM(queue->Id.semId)->semCount = (UINT16)uxInitialCount;
        LOS_IntRestore(intSave);
    }
    return xHandle;
}
#endif /* (configSUPPORT_STATIC_ALLOCATION == 1) */
#endif

#if (configUSE_MUTEXES == 1)
static void InitMutex(Queue_t * pxNewQueue)
{
    if (pxNewQueue == NULL) {
        return;
    }

    /* 保证新建互斥量的 holder 为空，避免把创建者误报为持有者。 */
    pxNewQueue->u.xSemaphore.xMutexHolder = NULL;
    pxNewQueue->pcHead = NULL;

    /* In case this is a recursive mutex. */
    pxNewQueue->u.xSemaphore.uxRecursiveCallCount = 0;

    /* 避免对已处于可用状态的新 LiteOS mutex 执行非 owner 释放。 */
}

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType)
{
    const UBaseType_t uxMutexLength = (UBaseType_t) 1;
    const UBaseType_t uxMutexSize = (UBaseType_t) 0;
    QueueHandle_t xNewQueue = xQueueGenericCreate(uxMutexLength, uxMutexSize, ucQueueType);
    InitMutex((Queue_t *) xNewQueue);
    return xNewQueue;
}
#endif
#if (configSUPPORT_STATIC_ALLOCATION == 1)
QueueHandle_t xQueueCreateMutexStatic(const uint8_t ucQueueType, StaticQueue_t *pxStaticQueue)
{
    const UBaseType_t uxMutexLength = (UBaseType_t) 1;
    const UBaseType_t uxMutexSize = (UBaseType_t) 0;
    QueueHandle_t xNewQueue = xQueueGenericCreateStatic(uxMutexLength, uxMutexSize, NULL, pxStaticQueue, ucQueueType);
    InitMutex((Queue_t *) xNewQueue);

    return xNewQueue;
}
#endif

#if (INCLUDE_xSemaphoreGetMutexHolder == 1)
TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore)
{
    Queue_t * const pxSemaphore = (Queue_t *) xSemaphore;
    if (pxSemaphore == NULL) {
        return NULL;
    }

    if (pxSemaphore->ucQueueType == queueQUEUE_TYPE_BASE ||
        pxSemaphore->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE ||
        pxSemaphore->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        return NULL;
    }

    return pxSemaphore->u.xSemaphore.xMutexHolder;
}
#endif /* (INCLUDE_xSemaphoreGetMutexHolder == 1) */
#endif

BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    Queue_t * const pxQueue = (Queue_t *)xQueue;
    /* Check the queue pointer is not NULL. */
    if (pxQueue == NULL) {
        return pdFAIL;
    }
    UINT32 ret;
    if (pxQueue->ucQueueType == queueQUEUE_TYPE_MUTEX || pxQueue->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX) {
        ret = LOS_MuxPend(pxQueue->Id.muxId, (UINT32)xTicksToWait);
        if (ret == LOS_OK) {
            /* 复用 LiteOS 的递归计数和优先级继承，仅同步 FreeRTOS holder 查询状态。 */
            pxQueue->u.xSemaphore.xMutexHolder = xTaskGetCurrentTaskHandle();
        }
        return (ret == LOS_OK) ? pdPASS : pdFAIL;
    } else if (pxQueue->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE || \
                pxQueue->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE) {
        ret = LOS_SemPend(pxQueue->Id.semId, (UINT32)xTicksToWait);
        return (ret == LOS_OK) ? pdPASS : pdFAIL;
    }
    return pdFAIL;
}

#if (configUSE_RECURSIVE_MUTEXES == 1)
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t xTicksToWait)
{
    Queue_t * const pxMutex = (Queue_t *) xMutex;

    if ((pxMutex == NULL) || (pxMutex->ucQueueType != queueQUEUE_TYPE_RECURSIVE_MUTEX)) {
        return pdFAIL;
    }
    /* 复用 LiteOS 递归加锁计数，避免 compat 重复维护并产生状态分叉。 */
    return xQueueSemaphoreTake(pxMutex, xTicksToWait);
}

BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex)
{
    Queue_t * const pxMutex = (Queue_t *) xMutex;
    if ((pxMutex == NULL) || (pxMutex->ucQueueType != queueQUEUE_TYPE_RECURSIVE_MUTEX)) {
        return pdFAIL;
    }
        return xQueueGenericSend(pxMutex, NULL, MUTEX_GIVE_BLOCK_TIME, queueSEND_TO_BACK);
}
#endif /* configUSE_RECURSIVE_MUTEXES */

#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    static UINT32 prvQueueReadable(Queue_t *pxQueue)
    {
#if (configSUPPORT_STATIC_ALLOCATION == 1)
        if (pxQueue->ucUsesStaticStorage != pdFALSE) {
            UINT32 intSave = LOS_IntLock();
            UINT32 queueReadable = (UINT32)pxQueue->uxMessagesWaiting;
            LOS_IntRestore(intSave);
            return queueReadable;
        }
#endif
        UINT32 queueReadable = 0;
        QUEUE_INFO_S queueInfo;
        (void)LOS_QueueInfoGet(pxQueue->Id.queueId, &queueInfo);
        queueReadable = queueInfo.usReadableCnt;
        return queueReadable;
    }

    static BOOL prvIsQueueEmpty(QueueSetMemberHandle_t xQueueOrSemaphore)
    {
        if (xQueueOrSemaphore == NULL) {
            return false;
        }

        if (xQueueOrSemaphore->ucQueueType == queueQUEUE_TYPE_BASE) {    // 队列
#if (configSUPPORT_STATIC_ALLOCATION == 1)
            if (xQueueOrSemaphore->ucUsesStaticStorage != pdFALSE) {
                return (prvQueueReadable(xQueueOrSemaphore) == 0U) ? true : false;
            }
#endif
            QUEUE_INFO_S queueInfo;
            UINT32 ret = LOS_QueueInfoGet(xQueueOrSemaphore->Id.queueId, &queueInfo);
            if (ret != LOS_OK) {
                return false;
            }
            if (queueInfo.usReadableCnt != 0) { // 队列中有可读的数据
                return false;
            }
            return true;
        }
        if (xQueueOrSemaphore->ucQueueType == queueQUEUE_TYPE_MUTEX ||
            xQueueOrSemaphore->ucQueueType == queueQUEUE_TYPE_RECURSIVE_MUTEX) { // 互斥量
            LosMuxCB *muxHandle = NULL;
            muxHandle = GET_MUX(xQueueOrSemaphore->Id.muxId);
                        return ((muxHandle->muxCount != 0U) || (muxHandle->owner != NULL)) ? true : false;
        }
        if (xQueueOrSemaphore->ucQueueType == queueQUEUE_TYPE_COUNTING_SEMAPHORE ||
            xQueueOrSemaphore->ucQueueType == queueQUEUE_TYPE_BINARY_SEMAPHORE) {    // 信号量
            LosSemCB *semHandle = NULL;
            semHandle = GET_SEM(xQueueOrSemaphore->Id.semId);
                        if (semHandle->semCount == 0U) {
                return true;
            }
            return false;
        }
        return false;   // 传来的句柄不在队列、信号量、互斥量中，直接返回false
    }

    QueueSetHandle_t xQueueCreateSet(const UBaseType_t uxEventQueueLength)
    {
        QueueSetHandle_t pxQueue = NULL;

        pxQueue = xQueueGenericCreate(uxEventQueueLength, (UBaseType_t) sizeof(Queue_t *), queueQUEUE_TYPE_SET);
        if (pxQueue == NULL) {
            return NULL;
        }
        pxQueue->pxQueueSetContainer = NULL;
        pxQueue->semInitCount = 0;

        return pxQueue;
    }

    BaseType_t xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
    {
        BaseType_t xReturn = pdPASS;

        if (xQueueOrSemaphore == NULL || xQueueSet == NULL) {
            return pdFAIL;
        }

        taskENTER_CRITICAL();
        {
            if (xQueueOrSemaphore->pxQueueSetContainer == NULL && prvIsQueueEmpty(xQueueOrSemaphore)) {
                xQueueOrSemaphore->pxQueueSetContainer = xQueueSet;
            } else {
                xReturn = pdFAIL;
            }
        }
        taskEXIT_CRITICAL();

        return xReturn;
    }

    BaseType_t xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore, QueueSetHandle_t xQueueSet)
    {
        BaseType_t xReturn = pdFAIL;

        if (xQueueOrSemaphore == NULL || xQueueSet == NULL) {
            return pdFAIL;
        }

        taskENTER_CRITICAL();
        if ((xQueueOrSemaphore->pxQueueSetContainer == xQueueSet) &&
            prvIsQueueEmpty(xQueueOrSemaphore)) {
            xQueueOrSemaphore->pxQueueSetContainer = NULL;
            xReturn = pdPASS;
        }
        taskEXIT_CRITICAL();
        return xReturn;
    }

    QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t xQueueSet, TickType_t const xTicksToWait)
    {
        QueueSetMemberHandle_t xReturn = NULL;

        // 获取队列队首
        xQueueReceive((QueueHandle_t)xQueueSet, &xReturn, xTicksToWait);

        return xReturn;
    }
    
    QueueSetMemberHandle_t xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet)
    {
        QueueSetMemberHandle_t xReturn = NULL;

                (void)xQueueReceiveFromISR((QueueHandle_t)xQueueSet, &xReturn, NULL);

        return xReturn;
    }

//#endif /* configUSE_QUEUE_SETS && configSUPPORT_DYNAMIC_ALLOCATI ON*/
#endif
