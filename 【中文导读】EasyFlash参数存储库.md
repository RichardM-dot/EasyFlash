# EasyFlash（Flash 参数存储库）学习导读

> 目录：`02-文件系统与存储/EasyFlash-Flash参数存储库`
> 来源：https://github.com/armink/EasyFlash （压缩包：EasyFlash-master.zip）

## 这是什么

EasyFlash 是 armink 的成名作：**让 MCU 像 "环境变量" 一样存参数**（KV 键值），并内置磨损均衡与掉电保护，还能存日志到 Flash。它是 FlashDB 的前身——作者后来把 KV 部分进化成了 FlashDB，所以**新项目请用 FlashDB，但 EasyFlash 源码更简单、注释更密，更适合作为"第一个读透的国产开源存储库"**。

## 目录导读

| 目录/文件 | 内容 |
| --- | --- |
| `easyflash/src/` | 核心实现：`ef_env.c`（KV 环境，核心）、`ef_log.c`（日志存储）、`ef_utils.c` |
| `easyflash/inc/` | API 头文件（`easyflash.h` 里注释极全） |
| `easyflash/port/` | 移植层（Flash 读写擦接口） |
| `demo/` | 各平台演示：stm32f10x（Keil 工程）、stm32f4xx、RT-Thread、Mbed 等 |
| `docs/` | 中文文档（移植指南、API 说明） |

## 学习路径（建议）

1. 读 `docs/` 中文文档，理解 ENV 模式两种后端：文件模式（靠文件系统）与 **磨损均衡模式（裸 Flash，核心）**。
2. 读 `ef_env.c`：看 KV 如何追加写入、如何做 GC、如何用"最近最久未使用"思想清理。
3. 对比 FlashDB 的 `fdb_kvdb.c`：同一个作者的进化版，看 API 与实现哪里变了——这是非常好的"代码演进阅读"练习。
4. 在 STM32 上跑 demo：`ef_set_env("wifi_ssid", "mywifi")` → 重启 → `ef_get_env("wifi_ssid")` 取回。

## 学习要点

1. **4 字节对齐的 KV 节点格式**：MAGIC+CRC+键值长度+数据，逐字节对照源码看懂。
2. **掉电保护**：写新记录 → CRC 校验通过才算数 → 旧的标记删除，与 littlefs 同哲学。
3. **日志模式**：`ef_log_write` 直接把日志当环形存储写进 Flash，上电可回读排查死机原因。
4. **为什么不直接用结构体 + EEPROM 模拟**：对比"固定地址存结构体"的坏处（改字段全乱、无均衡、无校验），体会 KV 方案价值。

## 建议购买/准备

- **零成本**：读源码 + 看 demo 代码即可（本仓库 demo 主要面向具体板卡）。
- **推荐硬件**：STM32F103C8T6 蓝pill（¥15-30，片内 Flash 即可玩 ENV 模式）或加 W25Q64 模块（¥3-8）。
- ST-Link V2 克隆版（¥15-25）用于烧录调试。

## 学习目标（自测）

- [ ] 能画出 ENV 磨损均衡模式下 Flash 分区示意图
- [ ] 能说清一条 KV 从 set 到 GC 的完整生命周期
- [ ] 知道 EasyFlash 与 FlashDB 的关系，知道新项目该选谁
