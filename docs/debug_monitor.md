# 调试与监控

> 日志、生成物检查、反汇编、clangd，以及建议的调试顺序。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 定位启动 / probe / I/O 问题的人 |
| **相关** | [faq.md](faq.md) · [problem_summary.md](problem_summary.md) · [tools/README.md](../tools/README.md) |

---

## 目录

1. [日志](#1-日志)
2. [查看生成物](#2-查看生成物)
3. [反汇编](#3-反汇编)
4. [clangd](#4-clangd)
5. [建议调试顺序](#5-建议调试顺序)

---

## 1. 日志

| API | 头 | 说明 |
| :--- | :--- | :--- |
| `SYS_LOGI/W/E` | `system_log.h` | 系统级；后端由 Kconfig 选择 |
| `DRV_LOG*` | 同左（经 production_log） | 驱动路径；注意池与性能 |

后端：

- `CONFIG_SYS_LOG_USE_PRINTF`  
- `CONFIG_SYS_LOG_USE_OSAL`  
- （可选）ESP 路径仅在对应宏下编译  

热路径不要刷 INFO；见 [fast_path.md](fast_path.md)。

---

## 2. 查看生成物

构建目录（名称因工程而异）中确认：

| 文件 | 看什么 |
| :--- | :--- |
| `config.h` | OSAL/SYSTEM/LOG 宏是否正确 |
| `board_nodes.h` | `DEV_ID_COUNT`、各 `DEV_ID_*`、chosen |
| `dt_config_gen.h` | `DTC_GEN_COUNT_*`、时钟容量 |
| `board_probe.c` | 是否包含你新写的 `board_driver_probe_*` |

IDE 无构建时用 `ide/stubs/*` 对齐符号名，但 **ID 数量是占位**，以生成物为准。

---

## 3. 反汇编

`CONFIG_BUILD_DISASM=y` 时配合 `cmake/disasm.cmake`（平台工程启用）生成 `.lst`，用于审查：

- 是否仍链到 weak 空 HAL  
- ISR 是否意外拉入重量级函数  

---

## 4. clangd

| 项 | 正确做法 |
| :--- | :--- |
| 工作区根 | mini_tree 仓库根 |
| 编译数据库 | 根 `compile_flags.txt` |
| 禁止 | 子目录再放 `compile_flags.txt` |
| 占位头 | `ide/stubs/` |
| 改配置后 | `Clangd: Restart language server` |

---

## 5. 建议调试顺序

1. `config.h` 宏  
2. dtc-lite 是否重跑、compatible 是否匹配  
3. 链接符号：`nm`/`objdump` 看 `hal_*` 归属  
4. `board_driver_probe_all` 返回与日志  
5. 单外设读写  
6. 中断 / WDT / 复位场景  

---

## 相关文档

- [faq.md](faq.md) · [porting_guide.md](porting_guide.md)  
- [architecture.md](architecture.md)
