# SLE RSSI 测距案例

本案例使用两块 WS63 开发板演示基于连接态 RSSI 的粗粒度测距。Server 负责广播并接受连接；Client 精确匹配广播名称，连接后每秒读取一次 RSSI，依次经过 7 点中值滤波、指数移动平均（EMA）和对数距离路径损耗模型，输出估算距离及 `near`、`middle`、`far` 分区。

> RSSI 测距容易受到遮挡、多径、天线方向和设备差异影响，只适合接近检测、趋势观察和粗粒度分区，不能替代 HADM 等高精度测距技术。

## 目录结构

```text
sle_rssi_ranging/
├── CMakeLists.txt
├── Kconfig
├── README.md
├── sle_rssi_ranging.c
├── sle_rssi_ranging_client/
│   ├── CMakeLists.txt
│   └── src/
│       ├── sle_rssi_ranging_client.c
│       └── sle_rssi_ranging_client.h
└── sle_rssi_ranging_server/
    ├── CMakeLists.txt
    └── src/
        ├── sle_rssi_ranging_server.c
        ├── sle_rssi_ranging_server.h
        ├── sle_rssi_ranging_server_adv.c
        └── sle_rssi_ranging_server_adv.h
```

## Kconfig 选项

- Server：`CONFIG_SAMPLE_SUPPORT_SLE_RSSI_RANGING_SERVER_SAMPLE=y`
- Client：`CONFIG_SAMPLE_SUPPORT_SLE_RSSI_RANGING_CLIENT_SAMPLE=y`
- 1 米参考 RSSI：`CONFIG_SLE_RSSI_RANGING_RSSI_AT_1M=-45`
- 路径损耗指数乘以 10：`CONFIG_SLE_RSSI_RANGING_PATH_LOSS_TENTHS=20`，即 `n=2.0`

Server 和 Client 位于同一个 Kconfig `choice` 中，应分别构建并烧录。

## 算法

Client 使用如下对数距离路径损耗模型：

```text
d = 1 m × 10 ^ ((A - RSSI_filtered) / (10 × n))
```

其中 `A` 是 1 米处标定的 RSSI，`n` 是环境路径损耗指数。测距前先用 7 点中值滤波抑制突发异常值，再用 EMA 平滑短时抖动。默认分区为：

- `near`：不大于 150 cm
- `middle`：150～500 cm
- `far`：大于 500 cm

## 构建与验证

在 SDK 根目录分别构建两种角色：

```shell
fbb config set CONFIG_ENABLE_BT_SAMPLE=y --target ws63-liteos-app
fbb config set CONFIG_SAMPLE_SUPPORT_SLE_SAMPLE=y --target ws63-liteos-app
fbb config set CONFIG_SAMPLE_SUPPORT_SLE_RSSI_RANGING_SERVER_SAMPLE=y --target ws63-liteos-app
fbb build ws63-liteos-app --clean

fbb config set CONFIG_SAMPLE_SUPPORT_SLE_RSSI_RANGING_CLIENT_SAMPLE=y --target ws63-liteos-app
fbb build ws63-liteos-app --clean
```

Client 的典型输出如下：

```text
[sle rssi client] found sle_rssi_server, scan_rssi=-37 dBm, stop seek
[sle rssi client] connected, conn_id=0x00, calibration=-45 dBm@1m, path_loss=2.0
[sle rssi client] range: raw=-36 dBm, median=-36 dBm, filtered=-36.0 dBm, samples=7, distance=35 cm, zone=near
```

`35 cm` 是默认教学标定参数下的模型估算值，不是独立测量得到的真实距离。
