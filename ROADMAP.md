# 项目路线图

> **作者前言**
>
> 本人学习和生活方面依然要兼顾，不可能一直按理论盯着代码哪里会犯病然后去优化。所谓实践出真理——会在移植项目过程中完成架构的综合性修改。

---

## 参考实现：异构多核项目（Heterogeneous-Multicore）

mini_tree 不再维护独立的 `examples/` 展示工程，改为以一个**真实在做的异构多核项目**作为参考实现，在移植过程中完成架构的综合性修改与验证。

| 节点 | 架构基准 | 角色 | 主平台 (Linux) | 也支持 (中间件) |
|------|---------|------|---------------|----------------|
| **STM32F407ZGT6** | ARM Cortex-M4F（通用基准） | 实时控制层（PID/PWM/DMA/CAN 心跳） | Linux / Docker | Windows 原生 |
| **CH32V307** | RISC-V RV32（通用基准） | 网关层（CAN 路由 / Flash OTA / 流量整形） | Linux / Docker | Windows 原生 |
| **ESP32-S3** | Xtensa LX7（异构架构） | USB CDC-ECM 虚拟网卡 + SPI FFT 协处理器 | Linux（不走 Docker） | Windows (ESP-IDF 双端) |
| **i.MX6ULL** | ARM Cortex-A7（应用层） | LVGL UI / MQTT Broker / SocketCAN 网关 | Linux BSP（Yocto） | — |

### 验证目标

- 检验框架对 **ARM / RISC-V / Xtensa** 三类架构的统一适配能力
- ESP32-S3 全速运行（开最高频率），在保证成品效果的前提下尽可能多开任务
- 实测 VFS 表与直接操作寄存器（fast path）**性能天花板在哪，满了多少**
- 高时钟频率、高并发、高实时场景下检验架构**到底能不能经受住检验**
- CAN 总线跨节点通信可靠性、OTA 双区升级、工业级安全与故障自愈

### 构建验证

三节点编译验证以 **Linux** 为主环境，**Windows 原生同样可用**（中间件跨平台）：

```bash
# ARM (STM32F407) / RISC-V (CH32V307) — 主走 Linux 原生 / Docker，Windows 原生也可
./build.sh stm32 -native    # 或 -docker
./build.sh ch32   -native    # 或 -docker

# ESP32-S3 — 主走 Linux 原生，Windows 原生也可（ESP-IDF 官方双端，不走 Docker）
./build.sh esp32  -native
```

### 意义

- 验证 ESP32 异构架构通过 Linux 原生工具链接入的可行性（Windows 也支持），无需 Docker 依赖

