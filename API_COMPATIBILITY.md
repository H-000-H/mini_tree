# API 兼容性声明

本文档定义 mini_tree 各头文件的接口稳定性等级, 帮助用户工程评估升级风险.

| 等级 | 含义 | 适用范围 |
|------|------|----------|
| **稳定** | 语义和签名向后兼容, 主版本内不破坏 | `osal.h`, `device.h`, `driver.h`, `VFS.h`, `event_bus.hpp` (C 封装), `buffer_pool.h`, `safe_state.h` |
| **实验性** | 可能在大版本间变更, 会提前一个版本标记 deprecated | `task_manager.h`, `system_wdt.h`, `system_scrubber.h`, 各类 `hal_if/*.h`, `production_log.h` |
| **内部** | 不对外承诺, 随时可改 | `board_devtable.h`, `board_nodes.h`, `board_handles.h`, `task_config.h` (生成文件), `config.h` (Kconfig 产物) |

**稳定接口的变更规则**:
- 主版本号递增时可破坏兼容性
- 次版本号递增仅做向后兼容的扩展 (新增函数 / 新增字段在 struct 末尾)
- 补丁版本仅修复 bug, 不修改公开 API 签名和语义

用户工程应只依赖标记为 **稳定** 的接口. 实验性接口可在评估后使用, 升级时需关注 changelog.

### 后续规划

计划基于 mini_tree v1.0.0 标准接口推出两个参考工程：

- **mini_tree_bare_metal_demo** — 基于 `osal_null.c` 的纯裸机工程示例，展示在无 RTOS 条件下使用 dtc-lite 静态拓扑和位掩码环形队列构建前后台系统
- **mini_tree_rtos_fully_decoupled** — 基于 FreeRTOS/RT-Thread 双后端的参考工程示例，展示音频 Service、GUI Service 与 ConfigStore 之间通过 EventBus 异步通信的完整模式
