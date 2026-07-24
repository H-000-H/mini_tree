# 硬件移植指南

> 把 mini_tree 接到具体 MCU：DTS、HAL 强符号、中断与安全、链接与验收。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 板级 / BSP 工程师 |
| **前置** | [getting_started.md](getting_started.md) |
| **相关** | [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |

---

## 目录

1. [移植目标](#1-移植目标)
2. [步骤总览](#2-步骤总览)
3. [DTS / DTSI](#3-dts--dtsi)
4. [HAL 强符号](#4-hal-强符号)
5. [USB / TinyUSB](#5-usb--tinyusb)
6. [中断与安全](#6-中断与安全)
7. [链接与段](#7-链接与段)
8. [验收与禁止事项](#8-验收与禁止事项)

---

## 1. 移植目标

完成后应满足：

1. 平台工程能配置、生成、链接（通用 CMake：`add_subdirectory`；**ESP-IDF** 见 [esp_idf_cmake.md](esp_idf_cmake.md)）。  
2. 每个用到的外设：DTS 节点 `okay` + 对应 `hal_*_<soc>.c` 已覆盖 weak。  
3. `board_driver_probe_all` 对关键外设返回成功或可接受的 WARNING。  
4. 业务只通过 `device_*` 访问硬件。  

---

## 2. 步骤总览

| # | 动作 | 产出 |
| :---: | :--- | :--- |
| 1 | 选定 OSAL / SYSTEM（`.config`） | `config.h` |
| 2 | 编写 board dts/dtsi | `BOARD_DTS` 指向真实入口 |
| 3 | 配置 `VENDOR_INC_DIRS` | dtsi 宏可展开 |
| 4 | 实现并链接 HAL `.c` | 强符号覆盖 |
| 5 | （可选）`usb_tusb_port` | USB 通路 |
| 6 | 接中断 / safety | 可进 safe_state |
| 7 | 点火 + 冒烟 | UART/GPIO/… |

---

## 3. DTS / DTSI

1. 不要改中间件占位 `board.dts` 当正式板级文件；在**平台树**维护正式 DTS。  
2. 每个外设节点：`compatible` 必须与仓库内 `DRIVER_REGISTER` 字符串一致（见 [driver_guide.md](driver_guide.md) §4）。  
3. 引脚/时钟/DMA/位时序等属性用**厂商宏**；确保 dtc-lite 能 `#include` 到定义它们的头。  
4. `status = "disabled"` 的节点不会进入有效 probe 集（按生成逻辑）。  
5. `chosen`（如调度 tick 定时器）写入后会出现在 `board_handles.h` / `CHOSEN_*`。  

---

## 4. HAL 强符号

| 规则 | 说明 |
| :--- | :--- |
| 签名 | 严格匹配 `hal/<periph>/hal_<periph>.h` |
| 覆盖 | 平台 `.c` 与中间件 weak stub **同名函数**；链接时强符号胜出 |
| 头文件 | 厂商头**只**出现在平台 `.c`，不要改中间件 `.h` 去 include |
| 返回值 | `int` + `VFS_ERR_*`；禁止 `void` 业务 API |
| 配置 | 从 `pdev`/`host` 上已填好的 cfg 读字段，勿再解析 DTS |

建议每外设一个文件：`hal_gpio_<soc>.c`、`hal_uart_<soc>.c`、…  

特殊：`hal_usb` 实现文件需 `#define HAL_USB_IMPL` 再包含头（头内有 poison）。

---

## 5. USB / TinyUSB

完整契约（API 表、生命周期、验收）见 **[usb_tusb_port.md](usb_tusb_port.md)**。摘要：

- 协议栈在 `lib/tinyusb`；板级粘合头 **不要** 同时暴露 TinyUSB osal 与 mini_tree osal 冲突符号。  
- 平台实现 `usb_tusb_port.h` 中全部 `usb_tusb_*`；`bus/usb` 只经此调用。  
- IDE 占位：`ide/stubs/usb_tusb_port.h`。  
- 外设 compatible / ioctl：[peripherals.md](peripherals.md)。  

---

## 6. 中断与安全

| 项 | 建议 |
| :--- | :--- |
| VIRQ | 平台 ISR → 上半部 → 下半部；见 [runtime_services.md](runtime_services.md) |
| 上半部 | 只做清标志 + submit；重活下半部 |
| `hal_platform_safety` / `hal_amp` | 安全策略 + 多核见 [amp.md](amp.md) |
| shutdown 回调 | 仅在 probe 阶段 `board_safety_register_shutdown` |
| CAN 协议扩展 | 弱钩子 [can_hook.md](can_hook.md)，勿改 DTS 当协议层 |

---

## 7. 链接与段

- 加入 `error_symbols.ld` 中 `ERR_SECTION_BASE` 的意图（或平台等价 `PROVIDE`）。  
- 确认 C++ 若启用：按工程要求 `-fno-rtti` / `-fno-exceptions`（根 CMake 在 `SYSTEM_CPP` 时有示例）。  
- FreeRTOS/RT-Thread：堆、钩子、SysTick 端口在平台侧完备。  

---

## 8. 验收与禁止事项

### 验收

- [ ] 任意 `hal_*` 抽测不再永远 `VFS_ERR_NOTSUPP`  
- [ ] `device_find` 找得到关键 label/compatible  
- [ ] 复位多次稳定  
- [ ] clangd 在中间件根目录无系统性缺头  

### 禁止

- 在 `vfs/` / `bus/` 里调用 LL/Cube/ESP API  
- 为图省事去掉 bus 头上的 `poison`  
- 把 SoC 专用 dtsi 提交进中间件默认树冒充通用  

---

## 相关文档

- [driver_guide.md](driver_guide.md) · [osal_switching.md](osal_switching.md) · [ecosystem.md](ecosystem.md)  
- [faq.md](faq.md) · [problem_summary.md](problem_summary.md)  
- [architecture.md](architecture.md)
