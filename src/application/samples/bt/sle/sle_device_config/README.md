# 参数配置与持久化

仓库中部署了一台环境监测设备B。维护人员使用手持终端A，通过SLE修改B的数据上报周期、温度告警阈值和工作模式。B负责校验输入、拒绝越界值、将合法业务参数写入非易失性存储（NV），并在重启后恢复给后续业务模块。本样例使用两块WS63：A运行Client，B运行Server，通过SLE Service Access Protocol（SSAP）完成这条配置链路。

> 当前样例只实现业务参数的传输、校验、保存和恢复，没有连接真实传感器，也没有实现周期上报、温度告警或低功耗业务。三个字段不是SLE广播间隔、连接参数或发射功率等协议栈设置。

> 建议先完成 [sle_hello](../sle_hello/) 的连接和属性读写实验。本样例不需要传感器、Wi-Fi 或云端账号。

## 功能与角色

| 角色 | 功能 | 成功判据 |
|------|------|----------|
| 设备B Server | 以 `config_server` 广播，校验业务参数并写入 NV ID `0x20A1` | 输出 `config saved`；非法值输出 `status=0xf` |
| 设备A Client | 扫描、连接、配对、发现属性，自动读取和写入设备B | 输出 `valid config accepted`，随后回读值一致 |

Client 自动执行以下流程：

```text
读取设备B当前业务参数
  → 写入合法配置 500 ms / 75.0 ℃ / mode 1
  → 收到成功 Write Confirmation
  → 写后回读
  → 写入非法配置 interval=50 ms
```

非法配置的失败状态在本次实测中只由 Server 端日志稳定呈现，不能把 Client 没有继续输出日志误判为测试失败或成功。真正的 NV 持久化还需要复位 Server 后观察加载日志。

## 8 字节业务参数结构

| 偏移 | 长度 | 字段 | 合法范围 |
|:---:|:---:|------|----------|
| 0 | 2 | `magic` | 固定为 `0x5343` |
| 2 | 2 | `report_interval_ms` | 100～60000 ms |
| 4 | 2 | `alarm_threshold_decicelsius` | -200～1000，单位 0.1 ℃ |
| 6 | 1 | `mode` | 0～1 |
| 7 | 1 | `version` | 当前为 1 |

本样例的两端都是相同工具链构建的 WS63，直接传输 `sle_device_config_t`。迁移到不同处理器或编译器时，应改为显式的逐字节编码和解码，不能依赖结构体布局与本机字节序。

## Kconfig UI 配置

在 SDK 根目录打开 `ws63-liteos-app` 的 Kconfig UI，依次进入：

```text
Application
  → Enable Sample.
    → Enable the Sample of BT.
      → Sample
        → Support SLE Sample.
          → SLE Sample
```

构建 Server 时选择：

```text
Support SLE Device Config Server Sample.
```

构建 Client 时改选：

```text
Support SLE Device Config Client Sample.
```

两个选项位于同一个 `choice`，一次构建只能选择一个角色。`CONFIG_SUPPORT_SLE_PERIPHERAL` 或 `CONFIG_SUPPORT_SLE_CENTRAL` 会由角色自动派生，不需要手工选择。

## 构建并保存双角色固件

先选择 Server，保存 Kconfig UI 后构建：

```powershell
fbb build --clean ws63-liteos-app
New-Item -ItemType Directory -Force .\firmware-snapshots
Copy-Item .\src\output\ws63\fwpkg\ws63-liteos-app\ws63-liteos-app_all.fwpkg `
    .\firmware-snapshots\sle-device-config-server.fwpkg
```

再通过 Kconfig UI 切换为 Client，重新构建并保存：

```powershell
fbb build --clean ws63-liteos-app
Copy-Item .\src\output\ws63\fwpkg\ws63-liteos-app\ws63-liteos-app_all.fwpkg `
    .\firmware-snapshots\sle-device-config-client.fwpkg
```

两个角色共用默认输出路径，第二次构建会覆盖第一次的包，因此必须先保存 Server 快照。

## 烧录与逐级验收

端口以实际枚举结果为准。本次实测使用 COM6 作为 Server、COM8 作为 Client，烧录波特率为 2000000：

```powershell
fbb flash -f .\firmware-snapshots\sle-device-config-server.fwpkg --chip ws63 --port <server_port> --baud 2000000 --json-summary
fbb flash -f .\firmware-snapshots\sle-device-config-client.fwpkg --chip ws63 --port <client_port> --baud 2000000 --json-summary
```

### 关卡一：Server启动并广播

```text
[sle device config server] start announce success.
[sle device config server] init ok
[sle device config server] waiting for connection
```

### 关卡二：Client完成连接和属性发现

```text
[sle device config client] found config_server, stopping seek...
[sle device config client] connected, conn_id=0x00
[sle device config client] pair complete conn_id:0, ... status:0
[sle device config client] sending read request, handle=0x0011
```

### 关卡三：合法写入与非法值拒绝

Client合法写入成功：

```text
[sle device config client] valid config accepted, handle=0x11
[sle device config client] read config: interval=500, threshold=750, mode=1
[sle device config client] persisted config verified
```

源码中的 `persisted config verified` 只表示同一次运行中的写后回读一致，不代表已经完成断电或复位恢复测试。非法值应在 Server 端验收：

```text
[sle device config server] rejected: interval=50, threshold=750, mode=1
[sle device config server] write response status=0xf
```

### 关卡四：复位后验证 NV 恢复

合法配置写入完成后复位 Server，必须看到：

```text
[sle device config server] config loaded from NV: interval=500, threshold=750, mode=1
```

只有这一关通过，才能判定 NV 持久化成功。

## 已知限制

- Client 当前没有稳定收到非法写响应的失败确认，因此不要等待 `invalid config rejected` 或 `test passed` 作为实板判据。
- `persisted config verified` 是现有源码日志名称，含义是写后回读一致。
- 配置协议直接使用 C 结构体，只适用于本案例相同架构、相同工具链的两块 WS63。
- `config_server` 和教学 UUID 仅用于样例，产品应使用自己的设备标识和服务 UUID。
