/**
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2023-2026.
 *
 * @if Eng
 * @brief Implements the BLE UART bridge entry, buffering, and transfer worker.
 * @else
 * @brief 实现 BLE UART 透传入口、缓冲队列和传输任务。
 * @endif
 *
 * History: \n
 * 2026-07-25, Create file. \n
 */

#include "app_init.h"
#include "bts_def.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "uart.h"
#include "ble_uart_bridge.h"

#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
#include "ble_uart_bridge_server.h"
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_CLIENT_SAMPLE)
#include "ble_uart_bridge_client.h"
#endif

/* Worker task configuration. / 工作任务配置。 */
#define BLE_UART_BRIDGE_TASK_PRIO 26
#define BLE_UART_BRIDGE_TASK_STACK_SIZE 0x2000

/* UART1 hardware mapping and frame format. / UART1 硬件映射与帧格式。 */
#define BLE_UART_BRIDGE_UART_BUS UART_BUS_1
#define BLE_UART_BRIDGE_UART_TX_PIN S_MGPIO15
#define BLE_UART_BRIDGE_UART_RX_PIN S_MGPIO16
#define BLE_UART_BRIDGE_UART_TX_MODE PIN_MODE_1
#define BLE_UART_BRIDGE_UART_RX_MODE PIN_MODE_1
#define BLE_UART_BRIDGE_UART_BAUDRATE 115200

/* Queue capacity, transfer granularity, and scheduling interval. / 队列容量、传输粒度与调度周期。 */
#define BLE_UART_BRIDGE_UART_RING_SIZE 4096
#define BLE_UART_BRIDGE_UART_DRIVER_BUFFER_SIZE BLE_UART_BRIDGE_BLE_PAYLOAD_MAX_LEN
#define BLE_UART_BRIDGE_UART_TX_CHUNK_SIZE 32
#define BLE_UART_BRIDGE_WORKER_DELAY_MS 5
#define BLE_UART_BRIDGE_WORKER_ACTIVE_DELAY_MS 10

/* UART driver buffers, software rings, and transfer state. / UART 驱动缓冲、软件环形队列与传输状态。 */
static uint8_t g_uart_driver_buffer[BLE_UART_BRIDGE_UART_DRIVER_BUFFER_SIZE];
static uint8_t g_uart_rx_ring_buffer[BLE_UART_BRIDGE_UART_RING_SIZE];
static volatile uint16_t g_uart_rx_ring_head;
static volatile uint16_t g_uart_rx_ring_tail;
static volatile uint32_t g_uart_rx_dropped_bytes;
static uint8_t g_uart_tx_ring_buffer[BLE_UART_BRIDGE_UART_RING_SIZE];
static volatile uint16_t g_uart_tx_ring_head;
static volatile uint16_t g_uart_tx_ring_tail;
static volatile uint32_t g_uart_tx_dropped_bytes;
static uint8_t g_uart_tx_staging_buffer[BLE_UART_BRIDGE_UART_TX_CHUNK_SIZE];
static volatile bool g_ble_send_pending;
static volatile bool g_ble_send_completed;
static volatile errcode_t g_ble_send_status;
static uint16_t g_ble_send_pending_length;

/**
 * @if Eng
 * @brief Returns the number of bytes currently stored in a ring buffer.
 * @else
 * @brief 返回环形缓冲区中当前保存的字节数。
 * @endif
 */
static uint16_t ble_uart_bridge_ring_count(uint16_t head, uint16_t tail)
{
    /* Head and tail wrap independently, so account for the wrapped interval. / 头尾指针独立回绕，需计算跨界区间。 */
    return (head >= tail) ? (uint16_t)(head - tail) :
                            (uint16_t)(BLE_UART_BRIDGE_UART_RING_SIZE - tail + head);
}

/**
 * @if Eng
 * @brief Copies queued bytes without advancing the ring-buffer tail.
 * @else
 * @brief 复制队列中的字节，但不移动环形缓冲区尾指针。
 * @endif
 */
static uint16_t ble_uart_bridge_ring_peek(const uint8_t *ring,
                                          uint16_t head,
                                          uint16_t tail,
                                          uint8_t *data,
                                          uint16_t capacity)
{
    uint16_t length = 0;
    uint16_t index = tail;

    /* Peek leaves tail unchanged; the caller consumes bytes only after I/O succeeds. / 预读不移动尾指针，I/O 成功后再消费。 */
    while (length < capacity && index != head) {
        data[length++] = ring[index];
        index = (uint16_t)((index + 1) % BLE_UART_BRIDGE_UART_RING_SIZE);
    }
    return length;
}

/**
 * @if Eng
 * @brief Advances a ring-buffer tail after a transfer completes successfully.
 * @else
 * @brief 在传输成功完成后移动环形缓冲区尾指针。
 * @endif
 */
static uint16_t ble_uart_bridge_ring_advance(uint16_t tail, uint16_t length)
{
    /* The modulo operation keeps the consumer index inside the fixed-size ring. / 取模保证消费者索引始终位于环形队列内。 */
    return (uint16_t)((tail + length) % BLE_UART_BRIDGE_UART_RING_SIZE);
}

/**
 * @if Eng
 * @brief Queues bytes received by the UART interrupt without blocking.
 * @else
 * @brief 在 UART 中断回调中以非阻塞方式缓存接收字节。
 * @endif
 */
static void ble_uart_bridge_uart_rx_cb(const void *buffer, uint16_t length, bool error)
{
    const uint8_t *source = (const uint8_t *)buffer;
    uint16_t head = g_uart_rx_ring_head;
    uint16_t index;

    if (error || source == NULL || length == 0) {
        return;
    }

    /*
     * This is a single-producer/single-consumer ring: the ISR publishes head only after copying data,
     * while the worker task exclusively advances tail. / 这是单生产者单消费者队列：中断复制数据后才发布头指针，
     * 工作任务独占更新尾指针。
     */
    for (index = 0; index < length; index++) {
        uint16_t next_head = (uint16_t)((head + 1) % BLE_UART_BRIDGE_UART_RING_SIZE);
        /* One slot remains empty to distinguish a full ring from an empty ring. / 保留一个空槽用于区分队满与队空。 */
        if (next_head == g_uart_rx_ring_tail) {
            break;
        }
        g_uart_rx_ring_buffer[head] = source[index];
        head = next_head;
    }
    /* Publish all copied bytes in one step so the worker never sees partial data. / 一次性发布已复制数据，避免任务读取半成品。 */
    g_uart_rx_ring_head = head;
    if (index < length) {
        /* Record overflow in the ISR and defer logging to task context. / 中断内只记录溢出，日志延后到任务上下文输出。 */
        g_uart_rx_dropped_bytes += (uint32_t)(length - index);
    }
}

/**
 * @if Eng
 * @brief Queues data received from BLE for deferred UART transmission.
 * @else
 * @brief 将 BLE 接收数据加入队列，等待后续发送到 UART。
 * @endif
 */
errcode_t ble_uart_bridge_uart_enqueue(const uint8_t *data, uint16_t length)
{
    uint16_t head = g_uart_tx_ring_head;
    uint16_t free_length;
    uint16_t index;

    if (data == NULL || length == 0) {
        return ERRCODE_INVALID_PARAM;
    }
    /* Reserve one byte so head == tail continues to mean an empty queue. / 预留一个字节，确保头尾相等仍只表示队空。 */
    free_length = (uint16_t)(BLE_UART_BRIDGE_UART_RING_SIZE - 1 -
                             ble_uart_bridge_ring_count(head, g_uart_tx_ring_tail));
    if (length > free_length) {
        /* Reject the whole BLE fragment to preserve its byte ordering. / 整包拒绝 BLE 分片，避免破坏字节顺序。 */
        g_uart_tx_dropped_bytes += length;
        return ERRCODE_BT_BUSY;
    }
    for (index = 0; index < length; index++) {
        g_uart_tx_ring_buffer[head] = data[index];
        head = (uint16_t)((head + 1) % BLE_UART_BRIDGE_UART_RING_SIZE);
    }
    /* Publish head after the complete fragment is stored. / 完整分片写入后再发布头指针。 */
    g_uart_tx_ring_head = head;
    return ERRCODE_BT_SUCCESS;
}

/**
 * @if Eng
 * @brief Records the completion status of the current UART-to-BLE fragment.
 * @else
 * @brief 记录当前 UART 到 BLE 数据分片的完成状态。
 * @endif
 */
void ble_uart_bridge_ble_send_complete(errcode_t status)
{
    /* BLE callbacks only publish completion; the worker owns queue consumption. / BLE 回调只发布完成状态，队列消费由任务负责。 */
    if (g_ble_send_pending) {
        g_ble_send_status = status;
        g_ble_send_completed = true;
    }
}

/**
 * @if Eng
 * @brief Sends one queued BLE-to-UART fragment through UART.
 * @else
 * @brief 通过 UART 发送一段已排队的 BLE 到 UART 数据。
 * @endif
 */
static bool ble_uart_bridge_process_uart_tx(void)
{
    int32_t written;
    uint16_t length;

    length = ble_uart_bridge_ring_peek(g_uart_tx_ring_buffer, g_uart_tx_ring_head, g_uart_tx_ring_tail,
                                       g_uart_tx_staging_buffer, sizeof(g_uart_tx_staging_buffer));
    if (length == 0) {
        return false;
    }
    /* Limit each blocking UART write so BLE processing is serviced regularly. / 限制单次阻塞写长度，保证 BLE 处理能及时运行。 */
    written = uapi_uart_write_nolock(BLE_UART_BRIDGE_UART_BUS, g_uart_tx_staging_buffer, length, 0);
    if (written != length) {
        osal_printk("[ble uart bridge] UART TX failed: expected=%u, actual=%d\r\n", length, written);
        return false;
    }
    /* Consume the fragment only after the UART driver writes it completely. / UART 驱动完整写入分片后才消费数据。 */
    g_uart_tx_ring_tail = ble_uart_bridge_ring_advance(g_uart_tx_ring_tail, length);
    return true;
}

/**
 * @if Eng
 * @brief Sends queued UART data over BLE and preserves it until accepted or confirmed.
 * @else
 * @brief 通过 BLE 发送 UART 队列数据，在接收或确认前保留数据。
 * @endif
 */
static bool ble_uart_bridge_process_ble_tx(void)
{
    static uint8_t data[BLE_UART_BRIDGE_BLE_PAYLOAD_MAX_LEN];
    uint16_t length;
    errcode_t ret;

    if (g_ble_send_pending) {
        /* Only one BLE fragment may be in flight to preserve order and provide backpressure. / 同时只允许一个 BLE 分片在途。 */
        if (!g_ble_send_completed) {
            return false;
        }
        /* Advance only after success; a failed fragment remains queued for retry. / 仅成功后推进，失败分片保留待重试。 */
        if (g_ble_send_status == ERRCODE_BT_SUCCESS) {
            g_uart_rx_ring_tail = ble_uart_bridge_ring_advance(g_uart_rx_ring_tail, g_ble_send_pending_length);
        }
        g_ble_send_pending = false;
        g_ble_send_completed = false;
    }
    length = ble_uart_bridge_ring_peek(g_uart_rx_ring_buffer, g_uart_rx_ring_head, g_uart_rx_ring_tail,
                                       data, sizeof(data));
    if (length == 0) {
        return false;
    }
    /*
     * Mark the fragment pending before calling the role API because the client path may report
     * completion synchronously. / 调用角色接口前先标记在途，因为客户端路径可能同步上报完成。
     */
    g_ble_send_pending = true;
    g_ble_send_completed = false;
    g_ble_send_pending_length = length;
#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
    /* The server uses a confirmed indication and completes in the confirmation callback. / 服务端使用有确认的指示。 */
    ret = ble_uart_bridge_server_send_notification(data, length);
#else
    /* The client uses an unacknowledged Write Command and completes once accepted locally. / 客户端使用无响应写命令。 */
    ret = ble_uart_bridge_client_send_write(data, length);
#endif
    if (ret != ERRCODE_BT_SUCCESS) {
        /* Immediate rejection did not put data on air, so allow the worker to retry. / 立即失败表示未发出数据，允许任务重试。 */
        g_ble_send_pending = false;
    }
    if (ret != ERRCODE_BT_SUCCESS && ret != ERRCODE_BT_BUSY) {
        osal_printk("[ble uart bridge] UART RX -> BLE failed: bytes=%u, ret=0x%x\r\n", length, ret);
    }
    return ret == ERRCODE_BT_SUCCESS;
}

/**
 * @if Eng
 * @brief Configures UART1 pins, format, buffers, and the receive callback.
 * @else
 * @brief 配置 UART1 引脚、通信格式、缓冲区与接收回调。
 * @endif
 */
static errcode_t ble_uart_bridge_uart_init(void)
{
    uart_attr_t attr = {.baud_rate = BLE_UART_BRIDGE_UART_BAUDRATE,
                        .data_bits = UART_DATA_BIT_8,
                        .stop_bits = UART_STOP_BIT_1,
                        .parity = UART_PARITY_NONE};
    uart_pin_config_t pins = {.tx_pin = BLE_UART_BRIDGE_UART_TX_PIN,
                              .rx_pin = BLE_UART_BRIDGE_UART_RX_PIN,
                              .cts_pin = PIN_NONE,
                              .rts_pin = PIN_NONE};
    uart_buffer_config_t buffer = {.rx_buffer = g_uart_driver_buffer, .rx_buffer_size = sizeof(g_uart_driver_buffer)};
    errcode_t ret;

    ret = uapi_pin_set_mode(BLE_UART_BRIDGE_UART_TX_PIN, BLE_UART_BRIDGE_UART_TX_MODE);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    ret = uapi_pin_set_mode(BLE_UART_BRIDGE_UART_RX_PIN, BLE_UART_BRIDGE_UART_RX_MODE);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    /* Reinitialize UART1 so a previous sample configuration cannot leak into this case. / 重新初始化 UART1，隔离旧案例配置。 */
    uapi_uart_deinit(BLE_UART_BRIDGE_UART_BUS);
    ret = uapi_uart_init(BLE_UART_BRIDGE_UART_BUS, &pins, &attr, NULL, &buffer);
    if (ret != ERRCODE_SUCC) {
        return ret;
    }
    /* The idle condition flushes short packets; the callback immediately transfers them to the software ring. */
    /* 空闲条件用于及时提交短包，回调随后立即转存到软件环形队列。 */
    ret = uapi_uart_register_rx_callback(BLE_UART_BRIDGE_UART_BUS, UART_RX_CONDITION_FULL_OR_SUFFICIENT_DATA_OR_IDLE, 1,
                                         ble_uart_bridge_uart_rx_cb);
    if (ret == ERRCODE_SUCC) {
        osal_printk("[ble uart bridge] UART1 ready: TX=GPIO15 RX=GPIO16 115200 8N1\r\n");
    }
    return ret;
}

/**
 * @if Eng
 * @brief Initializes the selected BLE role and transfers queued data in both directions.
 * @else
 * @brief 初始化选中的 BLE 角色，并双向转发队列中的数据。
 * @endif
 */
static int ble_uart_bridge_task(const char *arg)
{
    errcode_t ret;
    uint32_t reported_rx_drops = 0;
    uint32_t reported_tx_drops = 0;

    (void)arg;
    ret = ble_uart_bridge_uart_init();
    if (ret != ERRCODE_SUCC) {
        osal_printk("[ble uart bridge] UART init failed: 0x%x\r\n", ret);
        return (int)ret;
    }
#if defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_SERVER_SAMPLE)
    ret = ble_uart_bridge_server_init();
#elif defined(CONFIG_SAMPLE_SUPPORT_BLE_UART_BRIDGE_CLIENT_SAMPLE)
    ret = ble_uart_bridge_client_init();
#else
    return 0;
#endif
    if (ret != ERRCODE_SUCC) {
        return (int)ret;
    }

    while (1) {
        /* Service both directions every pass to prevent either stream from starving. / 每轮同时处理两个方向，避免单向饥饿。 */
        bool uart_active = ble_uart_bridge_process_uart_tx();
        bool ble_active = ble_uart_bridge_process_ble_tx();

        /* Rate-limit overflow reporting by printing only when the cumulative counter changes. / 仅在累计值变化时输出溢出日志。 */
        if (reported_rx_drops != g_uart_rx_dropped_bytes) {
            reported_rx_drops = g_uart_rx_dropped_bytes;
            osal_printk("[ble uart bridge] UART RX overflow, dropped=%u\r\n", reported_rx_drops);
        }
        if (reported_tx_drops != g_uart_tx_dropped_bytes) {
            reported_tx_drops = g_uart_tx_dropped_bytes;
            osal_printk("[ble uart bridge] UART TX overflow, dropped=%u\r\n", reported_tx_drops);
        }
        /* A bounded sleep yields CPU time while keeping queue draining responsive. / 有界休眠兼顾 CPU 让出与队列处理时延。 */
        osal_msleep((uart_active || ble_active) ? BLE_UART_BRIDGE_WORKER_ACTIVE_DELAY_MS :
                                                BLE_UART_BRIDGE_WORKER_DELAY_MS);
    }
}

/**
 * @if Eng
 * @brief Creates and starts the BLE UART bridge worker task.
 * @else
 * @brief 创建并启动 BLE UART 透传工作任务。
 * @endif
 */
static void ble_uart_bridge_entry(void)
{
    osal_task *task_handle = NULL;

    /* Protect task creation and priority setup as one scheduler operation. / 将任务创建与优先级设置作为一次调度器操作保护。 */
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ble_uart_bridge_task, NULL, "ble_uart_bridge",
                                      BLE_UART_BRIDGE_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, BLE_UART_BRIDGE_TASK_PRIO);
        /* The scheduler owns the task after creation; release only the returned wrapper. / 创建后任务归调度器，仅释放返回句柄。 */
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

app_run(ble_uart_bridge_entry);
