# 常见问题 FAQ

> 构建、链接、clangd、probe、OSAL 切换中最常踩的坑。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 所有使用者 |
| **相关** | [getting_started.md](getting_started.md) · [problem_summary.md](problem_summary.md) · [osal_switching.md](osal_switching.md) |

---

## clangd / IDE

### 全文件报错，`compiler_compat.h` not found

1. 是否打开了 **mini_tree 根目录**？  
2. 是否有子目录 `compile_flags.txt` 覆盖了根配置？（应删除子目录那份）  
3. 重启 Clangd。  

### `SYS_LOG backend not configured`

`config.h`（或 `ide/stubs/config.h`）需定义 `CONFIG_SYS_LOG_USE_PRINTF` 或其它日志后端。

### `device_id_t` / `DEV_ID_COUNT` 未知

缺少 dtc-lite 生成头。IDE 依赖 `ide/stubs/board_nodes.h`；真机构建把 generated 目录加入 `-I`。

### ETL / `etl/string.h` not found

配置 `ide/third_party/etl` → 本地 ETL 源，或关闭不需要的 C++ 翻译单元。

### 能不能用 Keil 当主 IDE？

**不推荐，作者已不支持。** 若客户强制要工程文件，降级路径是：CMake 生成头之后，用 **Python 自动生成 `.uvprojx`**（远古有过类似做法，现不维护）。日常仍应用 Cursor / VS Code / CLion / Qoder。见 [keil_integration.md](keil_integration.md)。

---

## 构建与链接

### HAL 调用总是 `VFS_ERR_NOTSUPP`

平台强符号未链入，仍在用中间件 weak 空实现。检查目标源文件列表与链接顺序。

### `hal_usb_*` poisoned

实现文件须先 `#define HAL_USB_IMPL` 再 `#include "hal_usb.h"`。

### 调用 `hal_can_*` 等报 poisoned

应走 `can_bus_*`；仅 bus 实现文件定义 `CAN_BUS_IMPL`（或对应宏）。

### `ERR_PTR` / 链接缺 `ERR_SECTION_BASE`

合并 `error_symbols.ld` 或平台提供等价 `PROVIDE(ERR_SECTION_BASE=…)`。

---

## 设备树与 Probe

### `board_driver_probe_all` 失败很多

查 DTS `status`、时钟属性是否展开为 0、compatible 是否与 `DRIVER_REGISTER` 一致、依赖 `deps` 是否先 probe。

### 改了驱动宏但表没变

清理构建目录，确保 CMake 依赖到了该 `.c`，dtc-lite 重新跑。

---

## 运行时

### 裸机无调度

`CONFIG_OSAL_NULL` 下用 `mini_tree_system_loop` + `xtask`，不要调用 `vTaskStartScheduler`。

### 切 RTOS 后优先级行为相反

见 [osal_switching.md](osal_switching.md)：FreeRTOS 与 RT-Thread 优先级数值语义相反。

### 复位后异常、上电正常

查全局构造、WDT、外设时钟门控、链接段；清单见 [problem_summary.md](problem_summary.md)。

---

## 相关文档

- [debug_monitor.md](debug_monitor.md) · [porting_guide.md](porting_guide.md)  
- [../USAGE.md](usage.md)
