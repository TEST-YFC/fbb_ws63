# PMP 代码区保护

> 使用技术：RISC-V PMP（Physical Memory Protection，物理内存保护）、读/写/执行权限、访问异常

本案例验证 WS63 启动阶段配置的 PMP 代码区保护：读取代码区可以成功；开启故障测试后写入代码区会触发 Store Access Fault。

## 学习目标

- 理解 PMP 的基本作用和读、写、执行权限
- 验证平台代码区的读取权限和写保护
- 区分正常验证与故障验证两种运行结果

## 基本概念

### PMP

PMP 是 RISC-V 处理器提供的硬件内存保护机制。软件可以为一段物理地址配置读（R）、写（W）和执行（X）权限。CPU 每次访问受保护地址时都会检查权限；访问不符合规则时，硬件直接触发异常。

WS63 在系统启动阶段调用 `uapi_pmp_config()` 配置代码区、数据区和外设地址空间。本案例不重复改写这些平台保护条目，而是访问样例函数所在的代码区，验证已经生效的保护策略。

### 允许访问与非法访问

代码运行需要读取和执行指令，因此受保护代码区允许读取和执行。写入代码区不符合权限规则，会触发 Store Access Fault。异常证明写操作被 PMP 拦截；它不是普通函数返回值，板端可能停止运行或复位。

### 锁定

PMP 条目可以设置 Lock 位。条目锁定后，在处理器复位前不能再次修改，适合保护启动代码和关键配置。启用锁定前必须确认地址范围和权限正确，否则错误配置也会一直保持到复位。

## 涉及 API

| API | 用途 | 本案例中的位置 |
|-----|------|----------------|
| `uapi_pmp_config()` | 配置 PMP 条目的地址范围和访问权限 | WS63 平台启动阶段调用 |

样例代码位于 `application/samples/peripheral/pmp/pmp_sample.c`。它取得 `pmp_sample_entry()` 的地址，先读取一个字节；只有开启故障测试时才尝试写入该地址。

## 案例说明

默认配置只执行安全检查：

```text
取得样例函数地址 → 读取代码区 → 输出正常启动日志
```

开启 `Trigger a protected write for exception verification.` 后执行故障检查：

```text
取得样例函数地址 → 读取代码区 → 尝试写入代码区 → PMP 触发访问异常
```

故障检查会有意触发异常，只用于验证保护是否生效，不应在普通功能固件中开启。

## 案例操作指导

### 配置样例

打开 `ws63-liteos-app` 的 Kconfig UI，进入：

```text
Application
  → Enable Sample.
    → Enable the Sample of Peripheral.
      → Support PMP Sample.
```

首次验证保持以下选项关闭：

```text
PMP Sample Configuration
  → Trigger a protected write for exception verification.
```

保存配置后构建并烧录：

```powershell
fbb build --clean ws63-liteos-app
fbb flash ws63-liteos-app --port <device_port> --baud 2000000 --json-summary
```

### 正常验证

串口输出如下，地址和首字节会随固件变化：

```text
[pmp] platform code protection check: address=0x34c658
[pmp] protected code read succeeded: 0x28
[pmp] fault test disabled; normal startup continues
```

这组日志证明代码区可读，且系统能够继续运行。

### 故障验证

重新打开 Kconfig UI，启用 `Trigger a protected write for exception verification.`，再构建和烧录。实板输出如下：

```text
[pmp] platform code protection check: address=0x34c658
[pmp] protected code read succeeded: 0x28
[pmp] triggering code write; a store access fault is expected
Store/AMO access fault
PMP access fault
```

出现 `Store/AMO access fault` 和 `PMP access fault`，说明写操作被硬件保护拦截。完成测试后应在 Kconfig UI 中关闭故障选项并重新烧录正常固件。

## 关键配置

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `CONFIG_SAMPLE_SUPPORT_PMP` | `n` | 启用 PMP 样例 |
| `CONFIG_PMP_SAMPLE_TRIGGER_FAULT` | `n` | 尝试写受保护代码区，会有意触发异常 |

## 代码详解

样例以自身入口函数地址作为平台代码区中的已知地址：

```c
volatile uint8_t *code_address =
    (volatile uint8_t *)(uintptr_t)pmp_sample_entry;

osal_printk("[pmp] protected code read succeeded: 0x%02x\r\n",
            code_address[0]);
```

故障测试通过 Kconfig 宏隔离，默认固件不会执行非法写操作：

```c
#if defined(CONFIG_PMP_SAMPLE_TRIGGER_FAULT)
    code_address[0] = PMP_SAMPLE_FAULT_VALUE;
#endif
```

