# FreeRTOS 兼容层

## 1. 概述

本目录提供基于 LiteOS 内核的 FreeRTOS API 兼容层，接口头文件来自 FreeRTOS Kernel 11.1.0。它用于帮助依赖 FreeRTOS API 的三方件迁移到 LiteOS，不是完整的 FreeRTOS 内核替代品。

兼容层复用 LiteOS 的任务、队列、信号量、互斥锁、事件和软件定时器能力。使用前应同时检查：

- 目标接口是否由当前 `FreeRTOSConfig.h` 宏启用；
- 本文“语义差异与限制”是否影响三方件；
- 三方件需要的 FreeRTOS 配置是否与 LiteOS ABI 一致。

FreeRTOS 官方文档：<https://www.freertos.org/Documentation/02-Kernel/04-API-references/01-Task-creation/00-TaskHandle>

## 2. 已实现接口

下列是当前主要可用接口。FreeRTOS 头文件中的宏接口会展开到本目录实现或官方 `stream_buffer.c`。

- **临界区与中断**
  `portENTER_CRITICAL`、`portEXIT_CRITICAL`、`portSET_INTERRUPT_MASK_FROM_ISR`、`portCLEAR_INTERRUPT_MASK_FROM_ISR`、`taskENTER_CRITICAL`、`taskEXIT_CRITICAL`
- **任务**
  `xTaskCreate`、`xTaskCreateStatic`、`vTaskDelete`、`vTaskDelay`、`xTaskDelayUntil`、`vTaskDelayUntil`、`xTaskGetCurrentTaskHandle`、`xTaskGetHandle`、`xTaskGetTickCount`、`xTaskGetTickCountFromISR`、`eTaskGetState`、`xTaskGetSchedulerState`、`uxTaskPriorityGet`、`vTaskPrioritySet`、`vTaskSuspend`、`vTaskResume`、`xTaskResumeFromISR`、`uxTaskGetSystemState`、`vTaskGetInfo`、`uxTaskGetStackHighWaterMark`、`uxTaskGetStackHighWaterMark2`、`vTaskSuspendAll`、`xTaskResumeAll`
- **任务扩展数据**
  `xTaskGetApplicationTaskTag`、`xTaskGetApplicationTaskTagFromISR`、`vTaskSetApplicationTaskTag`、`xTaskCallApplicationTaskHook`、`vTaskSetThreadLocalStoragePointer`、`pvTaskGetThreadLocalStoragePointer`、`vTaskSetTimeOutState`、`xTaskCheckForTimeOut`
- **任务通知**
  `xTaskNotifyGive`、`xTaskNotifyGiveIndexed`、`vTaskNotifyGiveFromISR`、`vTaskNotifyGiveIndexedFromISR`、`ulTaskNotifyTake`、`ulTaskNotifyTakeIndexed`、`xTaskNotify`、`xTaskNotifyIndexed`、`xTaskNotifyAndQuery`、`xTaskNotifyAndQueryIndexed`、对应的 `FromISR` 接口、`xTaskNotifyWait`、`xTaskNotifyWaitIndexed`、`xTaskNotifyStateClear`、`xTaskNotifyStateClearIndexed`、`ulTaskNotifyValueClear`、`ulTaskNotifyValueClearIndexed`
- **队列**
  `xQueueCreate`、`xQueueCreateStatic`、`vQueueDelete`、`xQueueReset`、`xQueueSend`、`xQueueSendToBack`、`xQueueSendToFront`、`xQueueOverwrite`、`xQueueReceive`、`xQueuePeek`、对应的 `FromISR` 接口、`uxQueueMessagesWaiting`、`uxQueueMessagesWaitingFromISR`、`uxQueueSpacesAvailable`、`xQueueIsQueueEmptyFromISR`、`xQueueIsQueueFullFromISR`、`xQueueGetStaticBuffers`
- **信号量与互斥锁**
  二值/计数信号量、普通/递归互斥锁的动态与静态创建接口，以及 `xSemaphoreTake`、`xSemaphoreGive`、递归和 `FromISR` 变体、`xSemaphoreGetMutexHolder`、`uxSemaphoreGetCount`、`vSemaphoreDelete`
- **队列集**
  `xQueueCreateSet`、`xQueueAddToSet`、`xQueueRemoveFromSet`、`xQueueSelectFromSet`、`xQueueSelectFromSetFromISR`
- **事件组**
  `xEventGroupCreate`、`xEventGroupCreateStatic`、`vEventGroupDelete`、`xEventGroupWaitBits`、`xEventGroupSetBits`、`xEventGroupClearBits`、`xEventGroupGetBits`、`xEventGroupSync`、对应的 `FromISR` 接口、`xEventGroupGetStaticBuffer`
- **软件定时器**
  `xTimerCreate`、`xTimerCreateStatic`、`xTimerStart`、`xTimerStop`、`xTimerReset`、`xTimerChangePeriod`、`xTimerDelete`、对应的 `FromISR` 接口、`xTimerIsTimerActive`、`xTimerGetPeriod`、`xTimerGetExpiryTime`、`vTimerSetReloadMode`、`xTimerGetReloadMode`、`pvTimerGetTimerID`、`vTimerSetTimerID`、`pcTimerGetName`、`xTimerPendFunctionCall`、`xTimerPendFunctionCallFromISR`、`xTimerGetStaticBuffer`
- **流缓冲区与消息缓冲区**
  FreeRTOS Kernel 11.1.0 `stream_buffer.c` 提供的动态、静态、普通和 `FromISR` 接口
- **内存**
  `pvPortMalloc`、`vPortFree`

接口是否可见仍受 `configUSE_*`、`configSUPPORT_*` 和 `INCLUDE_*` 宏控制。例如 `INCLUDE_xTaskAbortDelay=0` 时，头文件不会声明且兼容层不会生成 `xTaskAbortDelay` 符号。

## 3. 语义差异与限制

### 3.1 Tick 与调度

- WS63 LiteOS 的 tick ABI 为 32 位，必须使用 `configTICK_TYPE_WIDTH_IN_BITS=TICK_TYPE_WIDTH_32_BITS`。
- `xTaskGetTickCount` 和 `xTaskGetTickCountFromISR` 返回 LiteOS 自系统启动以来的 tick；FreeRTOS 原生实现通常从调度器启动开始计数。
- LiteOS 在应用入口运行前已经启动调度，因此 `vTaskStartScheduler` 和 `vTaskEndScheduler` 不负责启动或停止内核。
- `eTaskConfirmSleepModeStatus` 固定返回 `eAbortSleep`，不提供 FreeRTOS tickless idle 决策。
- `vTaskStepTick`/`xTaskCatchUpTicks` 只维护兼容层计数，不驱动 LiteOS 内核 tick。

### 3.2 `xTaskAbortDelay` 不支持

LiteOS 当前没有可安全中止任意队列、事件、信号量或延时等待的统一公开接口。兼容层不能满足 FreeRTOS `xTaskAbortDelay` 的基本语义，因此 WS63 配置必须保持：

```c
#define INCLUDE_xTaskAbortDelay 0
```

应用和测试代码必须使用相同配置，不得无条件引用该接口，否则会产生 `undefined reference to xTaskAbortDelay`。源码中保留的条件实现只用于其他平台配置，不代表 WS63 支持该能力。

### 3.3 临界区与 ISR API

- `portSET_INTERRUPT_MASK_FROM_ISR` 复用 LiteOS 全局中断锁，不支持按 FreeRTOS 中断优先级阈值屏蔽。
- 队列、信号量和任务通知的 `FromISR` 接口不执行阻塞等待，并在能够判断时设置 `pxHigherPriorityTaskWoken`。
- 软件定时器和事件组的部分 `FromISR` 操作通过兼容层 pended-call 队列异步执行；其回调运行在 LiteOS 软件定时器上下文，不是 FreeRTOS timer daemon task。

### 3.4 队列、信号量与队列集

- 静态队列的数据存储使用调用者提供的 `pucQueueStorage`，控制块使用调用者提供的 `StaticQueue_t`；动态和静态对象删除时按所有权分别处理。
- 计数信号量同时保留 `uxMaxCount` 和 `uxInitialCount` 语义。
- `xQueueAddToSet` 仅允许尚未加入其他集合且当前没有待处理数据的成员；`xQueueRemoveFromSet` 仅允许移除无待处理数据的成员。
- 队列集当前只在 `configUSE_QUEUE_SETS=1` 且 `configSUPPORT_DYNAMIC_ALLOCATION=1` 时编译。建议只把队列和信号量加入队列集，不要把互斥锁用于队列集，因为这会破坏优先级继承语义。
- `xSemaphoreGetMutexHolder` 需要 `INCLUDE_xSemaphoreGetMutexHolder=1`。当前未实现 `xSemaphoreGetMutexHolderFromISR`。

### 3.5 事件组

- 多个等待者由同一次 set 唤醒时，会先保存同一份事件位快照，再统一处理 `clear-on-exit`，避免第一个等待者清位后影响其他等待者。
- `xEventGroupSync` 的 set、条件检查、等待者登记和成功清位在调度锁保护下完成。
- `xEventGroupSetBitsFromISR` 和 `xEventGroupClearBitsFromISR` 依赖 `configUSE_TIMERS=1` 与 `INCLUDE_xTimerPendFunctionCall=1`。
- 删除事件组会唤醒仍在等待的任务，并取消尚未执行的该事件组 pended call。删除后不得继续使用原句柄。

### 3.6 软件定时器

- start、stop、reset、change-period 和 delete 直接操作 LiteOS 软件定时器，不经过 FreeRTOS timer command queue；这些命令的 `xTicksToWait` 不具备原生命令队列等待语义。
- `xTimerPendFunctionCall` 和 `xTimerPendFunctionCallFromISR` 使用长度为 `configTIMER_QUEUE_LENGTH` 的 FIFO 环形队列异步执行。任务接口可按 `xTicksToWait` 等待队列空间，ISR 接口队列满时立即返回 `pdFAIL`。
- `vTimerSetReloadMode` 会更新 LiteOS timer 的周期/单次模式；单次 timer 到期后保持句柄有效，可再次启动。
- `pxHigherPriorityTaskWoken` 不具备 FreeRTOS timer daemon task 的精确优先级比较语义。

### 3.7 任务通知与资源

- 每个通知索引使用独立 LiteOS 事件对象，索引范围由 `configTASK_NOTIFICATION_ARRAY_ENTRIES` 决定。
- 任务删除时会清理兼容层控制块和通知事件；应用不得在任务删除后继续使用旧的 `TaskHandle_t`。
- `configNUM_THREAD_LOCAL_STORAGE_POINTERS` 只决定每任务 TLS 槽位数量。兼容层本身没有要求必须为 3；FreeRTOS+FAT 等三方件需要更多槽位时应在对应组件提交中单独配置。

### 3.8 内存

- `LOSCFG_COMPAT_FREERTOS_HEAP=3` 时，`pvPortMalloc`/`vPortFree` 使用 LiteOS 系统内存。
- `vPortHeapResetState` 为空实现。

## 4. 配置与接入

### 4.1 LiteOS 配置

当前 WS63 完整验证 profile 使用以下配置：

```ini
LOSCFG_COMPAT_FREERTOS=y
LOSCFG_COMPAT_FREERTOS_HEAP=3
LOSCFG_TASK_STACK_STATIC_ALLOCATION=y
LOSCFG_QUEUE_STATIC_ALLOCATION=y
LOSCFG_BASE_CORE_SWTMR=y
```

- `LOSCFG_TASK_STACK_STATIC_ALLOCATION` 是 `xTaskCreateStatic` 的必要配置。
- 当前验证 profile 开启了 `LOSCFG_QUEUE_STATIC_ALLOCATION`；compat 静态数据队列自身使用调用者缓冲区和 LiteOS 信号量，不直接调用 `LOS_QueueCreateStatic`。若目标不依赖其他 LiteOS 静态队列功能，可在单独验证后裁剪该宏。
- `LOSCFG_BASE_CORE_SWTMR` 是 FreeRTOS software timer、pended call 和事件组 ISR 延迟操作的基础。
- 不使用某类静态对象或 timer 时，可以按实际接口裁剪对应配置，但必须重新编译并验证目标三方件。

`compat/CMakeLists.txt` 已包含：

```cmake
list(APPEND MODULE_${LOSCFG_COMPAT_FREERTOS} freertos)
```

正常情况下无需再次手工修改该文件。

### 4.2 FreeRTOS 配置

WS63 配置文件位于：

```text
kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/targets/ws63/include/FreeRTOSConfig.h
```

关键约束：

```c
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 3U
#define configUSE_TIMERS 1
#define configUSE_QUEUE_SETS 1
#define INCLUDE_xTimerPendFunctionCall 1
#define INCLUDE_xTaskAbortDelay 0
#define INCLUDE_xSemaphoreGetMutexHolder 1
```

`configNUM_THREAD_LOCAL_STORAGE_POINTERS` 应按应用实际依赖设置。普通 compat 测试使用 1 即可；不要因为某一个三方件的需求全局固定为 3。

### 4.3 组件头文件

使用兼容层的组件至少需要 FreeRTOS Kernel 与 compat 头文件路径：

```cmake
set(FREERTOS_KERNEL_ROOT
    ${ROOT_DIR}/kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/open_source/FreeRTOS/FreeRTOS-Kernel-11.1.0)
set(FREERTOS_COMPAT_ROOT
    ${ROOT_DIR}/kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/compat/freertos)

set(PRIVATE_HEADER
    ${FREERTOS_KERNEL_ROOT}/include
    ${FREERTOS_COMPAT_ROOT}/include
)
```

## 5. 创建第一个 FreeRTOS Hello World

下面通过创建一个 FreeRTOS 静态任务，让开发板通过串口输出 `Hello World`。示例入口使用 SDK 的 `app_run`。

### 5.1 创建示例目录和文件

在 `src/application/samples` 下创建 `freertos_demo` 目录，并在目录中创建：

```text
freertos_demo/
├── CMakeLists.txt
├── demo.c
└── demo.h
```

### 5.2 编写 CMakeLists.txt

在 `freertos_demo/CMakeLists.txt` 中添加源码和头文件路径：

```cmake
set(SOURCES_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}/demo.c
)

set(PUBLIC_HEADER_LIST
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/open_source/FreeRTOS/FreeRTOS-Kernel-11.1.0/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../kernel/liteos/liteos_v208.5.0/Huawei_LiteOS/compat/freertos/include
)

set(SOURCES "${SOURCES_LIST}" PARENT_SCOPE)
set(PUBLIC_HEADER "${PUBLIC_HEADER_LIST}" PARENT_SCOPE)
```

然后在 `src/application/samples/CMakeLists.txt` 中注册示例目录：

```cmake
add_subdirectory_if_exist(freertos_demo)
```

### 5.3 编写 demo.h

```c
#ifndef FREERTOS_DEMO_H
#define FREERTOS_DEMO_H

void freertos_demo_entry(void);

#endif
```

### 5.4 编写 demo.c

```c
#include "FreeRTOS.h"
#include "task.h"

#include "app_init.h"
#include "common_def.h"
#include "osal_debug.h"

#include "demo.h"

#define STACK_DEPTH 1024U

static StaticTask_t g_task_buffer;
static StackType_t g_task_stack[STACK_DEPTH]
    __attribute__((aligned(LOSCFG_STACK_POINT_ALIGN_SIZE)));

static void hello_world_task(void *argument)
{
    unused(argument);
    osal_printk("Hello World\r\n");

    /* 任务执行完成后删除自己。 */
    vTaskDelete(NULL);
}

void freertos_demo_entry(void)
{
    TaskHandle_t task = xTaskCreateStatic(
        hello_world_task,  /* 任务入口函数。 */
        "hello",           /* 任务名称。 */
        STACK_DEPTH,       /* 栈深度，单位为 StackType_t。 */
        NULL,              /* 传递给任务的参数。 */
        2U,                /* FreeRTOS 任务优先级。 */
        g_task_stack,      /* 任务栈。 */
        &g_task_buffer);   /* 静态任务控制块。 */

    if (task == NULL) {
        osal_printk("Create FreeRTOS task failed\r\n");
    }
}

app_run(freertos_demo_entry);
```

`xTaskCreateStatic` 的 `uxStackDepth` 参数单位是 `StackType_t` 元素个数，因此任务栈声明为 `StackType_t g_task_stack[STACK_DEPTH]`，不需要再次乘以 `sizeof(StackType_t)`。

### 5.5 开启配置

确认目标使用的 LiteOS 配置已经开启：

```ini
LOSCFG_COMPAT_FREERTOS=y
LOSCFG_COMPAT_FREERTOS_HEAP=3
LOSCFG_TASK_STACK_STATIC_ALLOCATION=y
```

同时确认 WS63 的 `FreeRTOSConfig.h` 中已经开启静态分配：

```c
#define configSUPPORT_STATIC_ALLOCATION 1
```

## 6. 编译、烧录和查看结果

修改配置或首次接入示例后执行干净编译：

```powershell
$env:FBB_SDK_DIR = (Resolve-Path .\src).Path
fbb build --clean ws63-liteos-app
```

编译成功后烧录下面的 `_all.fwpkg` 文件：

```text
src/output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg
```

开发板启动后打开串口。任务创建和运行成功时，可以看到：

```text
Hello World
```

看到 `Hello World`，说明 FreeRTOS 兼容层、静态任务创建和任务调度已经正常工作。

提交兼容层改动前，还应验证动态/静态任务、索引通知、动态/静态队列、信号量与互斥锁、队列集、事件组、多等待者清位、software timer、pended call 和真实 ISR 路径。测试代码与兼容层必须使用同一份 `FreeRTOSConfig.h`。
