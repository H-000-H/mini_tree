# API 兼容性声明

本文档定义 mini_tree 各头文件的接口稳定性等级, 帮助用户工程评估升级风险.

| 等级 | 含义 | 适用范围 |
|------|------|----------|
| **稳定** | 语义和签名向后兼容, 主版本内不破坏 | `osal.h`, `device.h` (`device_find_by_label`/`device_open`/`device_read`/`device_write`/`device_ioctl`), `driver.h` (`DRIVER_REGISTER`), `buffer_pool.h`, `safe_state.h`, `task_manager.h` (`task_manager_create_task`), `system_init.h` (`mini_tree_pre_os_init`/`mini_tree_start_tasks`/`system_init_complete`) |
| **实验性** | 可能在大版本间变更, 会提前一个版本标记 deprecated | `event_bus.hpp` (C++ Singleton), `system_cmd.hpp` (`SystemCmd::dispatch`/`registerCmd`), `system_wdt.h`, `system_scrubber.h`, 各类 `hal/**/*.h`, `bus/**/*.h`, `vfs/**/*.h`, `production_log.h` |
| **内部** | 不对外承诺, 随时可改 | `board_devtable.h`, `board_nodes.h`, `board_handles.h`, `task_config.h` (生成文件), `dt_config_gen.h` (Kconfig/DTS 产物), `config.h` (Kconfig 产物) |

**稳定接口的变更规则**:
- 主版本号递增时可破坏兼容性
- 次版本号递增仅做向后兼容的扩展 (新增函数 / 新增字段在 struct 末尾)
- 补丁版本仅修复 bug, 不修改公开 API 签名和语义

用户工程应只依赖标记为 **稳定** 的接口. 实验性接口可在评估后使用, 升级时需关注 changelog.
