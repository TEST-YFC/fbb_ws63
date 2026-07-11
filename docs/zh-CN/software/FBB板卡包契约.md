# FBB 板卡包契约

本文定义 WS63 SDK 与 `hs-fbb-cli` 组件管理器之间的板卡包契约。板卡包不是独立市场，而是统一组件注册中心中的一种资产类型：`kind = "board"`。

## 目标

- 让开发者通过 `fbb board list/info/select` 发现并选择板卡。
- 让伙伴板卡以标准包形式发布到统一注册中心。
- 让 samples 和 solution 能声明自己适配的板卡，而不是把板卡知识散落在 README 中。

## SDK 侧职责

SDK 负责提供芯片、target、构建系统和官方/伙伴板卡的基础事实。WS63 的板卡包入口文件为：

```text
src/tools/board_packages/ws63/boards.json
```

该文件登记 SDK 已知的板卡包候选项。首期登记：

- `nearlink-vip/hihope-nearlink-dk-ws63e-v03`
- `nearlink-vip/bearpi-pico-h3863`

## 板卡包 manifest

板卡包必须包含 `fbb-package.toml`，并满足以下最小结构：

```toml
[package]
name = "nearlink-vip/hihope-nearlink-dk-ws63e-v03"
version = "0.1.0"
kind = "board"
description = "HiHope NearLink DK WS63E V03 board package for WS63."
certification = "partner"

[compat]
chips = ["ws63"]
targets = ["ws63-liteos-app"]

[build]
component = "hihope-nearlink-dk-ws63e-v03"
kconfig = "Kconfig"

[board]
id = "hihope-nearlink-dk-ws63e-v03"
vendor = "HiHope"
chip = "ws63"
target = "ws63-liteos-app"
status = "partner"
```

约束：

- `[package].kind` 必须为 `board`。
- `[compat].chips` 必须包含 `ws63`。
- `[compat].targets` 应声明可构建 target。
- `[board].id` 是稳定板卡 ID，供 samples 和 solution 引用。
- 若板卡包提供 Kconfig、头文件或适配代码，应作为普通 FBB 组件安装到项目 `components/` 下。

## CLI 行为

- `fbb board list --chip ws63` 列出注册中心和本地索引中的 WS63 板卡包。
- `fbb board info <ns/name>` 展示板卡 metadata、compat、certification。
- `fbb board select <ns/name>` 将板卡包加入 `[dependencies]`，并在 `[project]` 写入 `board = "<ns/name>"`。

## samples 与 solution 的引用

sample 包使用 `[sample].boards` 声明适配板卡：

```toml
[package]
kind = "sample"

[sample]
id = "ws63-led-blink"
boards = ["hihope-nearlink-dk-ws63e-v03"]
```

solution 包使用 `[solution].boards`、`[solution].samples`、`[solution].components` 声明组合关系：

```toml
[package]
kind = "solution"

[solution]
boards = ["hihope-nearlink-dk-ws63e-v03"]
samples = ["nearlink-vip/ws63-led-blink"]
components = ["nearlink-vip/ringbuf"]
```

