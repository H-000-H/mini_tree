# mini_tree 使用手册

> **适用于：** mini_tree 通用嵌入式中间件框架
> **快速入口：** [README.md](README.md) — 架构总览与构建说明

---

## 目录

1. [术语说明](#1-术语说明)
2. [快速开始 — 构建与工程集成](docs/getting_started.md#2-快速开始)
3. [配置系统](docs/getting_started.md#3-配置系统)
4. [用户工程集成](docs/getting_started.md#4-用户工程集成)
5. [点火时序](docs/getting_started.md#5-点火时序)
6. [硬件移植](docs/porting_guide.md)
7. [设备树与驱动](docs/driver_guide.md)
8. [服务编写规范](docs/service_spec.md#8-服务编写规范)
9. [应用层解耦规范](docs/service_spec.md#9-应用层解耦规范)
10. [调试与监控](docs/debug_monitor.md)
11. [红线区 — 硬实时 Fast Path](docs/fast_path.md)
12. [常见问题](docs/faq.md)
13. [问题总结：RT-Thread FPU 踩坑](docs/problem_summary.md)
14. [OSAL 后端切换注意事项](docs/osal_switching.md)

---

## 1. 术语说明

| 术语 | 含义 |
|------|------|
| **OSAL** | 操作系统抽象层，统一封装 FreeRTOS / RT-Thread / 裸机接口 |
| **EventBus** | 发布-订阅事件总线，模块间解耦通信 |
| **BufferPool** | 基于位图的无锁内存池，零拷贝消息传递 |
| **DTS** | 设备树源文件 (.dts)，描述硬件拓扑与依赖关系 |
| **VFS** | 拟物化文件系统，设备树的运行时抽象视图 |
| **Phase 1** | RTOS 启动前的早期初始化（看门狗、EventBus 预置） |
| **Phase 2** | RTOS 启动后的驱动探针与任务创建 |
| **hal/** | 硬件抽象层接口与通用实现（`hal/hal_if_dummy.c`、`hal/cpu/`、`hal/pwm/` 等），具体芯片实现由项目通过 `HAL_SRCS` 变量提供 |
| **bus/** | 总线层（`bus/spi/`、`bus/uart/`、`bus/dma/`），介于 `hal/` 与 `vfs/` 之间 |
| **vfs/** | VFS 设备节点实现（`vfs/spi/`、`vfs/uart/`、`vfs/gpio/`），提供 `device_*` API |
| **DRIVER_REGISTER** | 驱动注册宏，编译期由 dtc-lite 扫描收录到 probe 表，运行时自动匹配 DTS `compatible` |
| **device_find_by_label** | 按 DTS `label` 查找设备，业务层与硬件的唯一耦合点 |
| **task_manager_create_task** | 业务任务创建入口（封装 `osal_task_create_handle` + 自动 TWDT 订阅） |
| **SystemCmd** | 异步"邮局"模式命令路由器，handler 只投递不阻塞 |
| **Scrubber** | 闪存巡检任务，检测 Flash Bit-Rot |
| **TWDT** | 任务看门狗，监控任务是否按时喂狗 |
