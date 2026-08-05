# RTC 时间基准

> 使用技术：RTC 支撑的系统时间基准、单调计时、周期任务

本案例读取 WS63 的系统时间基准，等待设定周期后再次读取时间，计算并输出实际经过的毫秒数。

## 学习目标

- 理解 RTC、单调时间和日历时间的区别
- 使用 `uapi_systick_get_ms()` 读取毫秒时间基准
- 比较配置周期与实际经过时间

## 基本概念

### RTC

RTC（Real-Time Clock，实时时钟）是持续计数的硬件时钟。它可以为系统提供稳定的时间基准，也可以在支持的低功耗场景中承担定时和唤醒功能。

### 单调时间与日历时间

单调时间表示设备启动后持续经过了多久，只会向前增加，适合计算超时和两个事件之间的间隔。日历时间表示年、月、日、时、分、秒，通常还需要设置初始日期或通过网络校时。

本案例验证的是 RTC 支撑的单调时间基准，不设置日历日期，也不验证深度睡眠唤醒。

### 延时与时间测量

`osal_msleep()` 让当前任务至少等待指定时间，但任务恢复还会受到系统调度影响。因此实际经过时间可能比配置值略大。样例在延时前后读取时间并相减，可以直接观察这部分差异。

## 涉及 API

| API | 用途 |
|-----|------|
| `uapi_systick_init()` | 初始化系统时间基准接口 |
| `uapi_systick_get_ms()` | 读取启动后的毫秒计数 |
| `osal_msleep()` | 让样例任务等待配置的周期 |

## 案例说明

样例创建独立任务，并按以下流程循环运行：

```text
读取开始时间 → 等待配置周期 → 读取结束时间 → 输出时间差
```

默认周期为 5000 ms。它用于验证运行期间的连续计时，不代表日历时钟、硬件闹钟或低功耗唤醒功能。

## 案例操作指导

### 配置样例

打开 `ws63-liteos-app` 的 Kconfig UI，进入：

```text
Application
  → Enable Sample.
    → Enable the Sample of Peripheral.
      → Support RTC Sample.
```

在 `RTC Sample Configuration` 中可修改 `RTC sample period in milliseconds.`，保存配置后构建并烧录：

```powershell
fbb build --clean ws63-liteos-app
fbb flash ws63-liteos-app --port <device_port> --baud 2000000 --json-summary
```

### 运行结果

默认 5000 ms 周期下，实板连续输出：

```text
[rtc] time base ready: start=...
[rtc] period #1 elapsed=5000ms
[rtc] period #2 elapsed=5010ms
```

`period` 序号持续增加，且 `elapsed` 接近 5000 ms，说明时间基准和周期任务均正常工作。实际值可因任务调度略大于配置值。

## 关键配置

| 配置项 | 默认值 | 范围 | 说明 |
|--------|--------|------|------|
| `CONFIG_SAMPLE_SUPPORT_RTC` | `n` | `y/n` | 启用 RTC 时间基准样例 |
| `CONFIG_RTC_SAMPLE_PERIOD_MS` | `5000` | 100～60000 ms | 每次时间测量的等待周期 |

## 代码详解

任务用两次时间读数计算实际经过时间：

```c
uint64_t start_ms = uapi_systick_get_ms();
uint64_t end_ms;

(void)osal_msleep(CONFIG_RTC_SAMPLE_PERIOD_MS);
end_ms = uapi_systick_get_ms();
osal_printk("[rtc] period #%u elapsed=%llums\r\n",
            period_count, end_ms - start_ms);
```

这段代码运行在样例任务中，可以阻塞等待。若业务需要硬件闹钟或深度睡眠唤醒，应使用目标芯片支持的 RTC/低功耗接口另行验证。

