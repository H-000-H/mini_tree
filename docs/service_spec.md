# 服务编写规范 · 应用层解耦

> 业务代码允许依赖什么、禁止依赖什么；如何挂到两段式启动上。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 应用 / 服务作者 |
| **前置** | [getting_started.md](getting_started.md) § 点火 |
| **相关** | [fast_path.md](fast_path.md) · [peripherals.md](peripherals.md) · [api_compatibility.md](api_compatibility.md) |

---

## 目录

1. [允许的依赖](#1-允许的依赖)
2. [禁止的依赖](#2-禁止的依赖)
3. [I/O 与错误处理](#3-io-与错误处理)
4. [任务与同步](#4-任务与同步)
5. [与启动顺序的关系](#5-与启动顺序的关系)
6. [检查清单](#6-检查清单)

---

## 1. 允许的依赖

| 头 / 模块 | 用途 |
| :--- | :--- |
| `device.h` | 查找设备、open/read/write/ioctl |
| `status.h` | `VFS_OK` / `VFS_ERR_*` |
| `osal.h` | 任务、锁、队列、延时、日志级别配合 |
| `event_bus.h` / `event_bus.hpp` | 发布订阅 |
| `buffer_pool.h` · `algorithm/buffer` | 缓冲 |
| `system_log.h` | `SYS_LOGI/W/E` |
| 业务自身头 | — |

---

## 2. 禁止的依赖

| 禁止 | 原因 |
| :--- | :--- |
| `hal_*.h`（业务里） | 破坏分层；应走 device/vfs |
| `FreeRTOS.h` / `rtthread.h`（业务里） | 应走 OSAL，便于切后端 |
| 厂商寄存器头 | 不可移植 |
| 随意 `malloc` / `printf` / `memset` | 可能被 `compiler_compat_poison` 毒杀；用 `COMPAT_MEM_*` / 池 |

---

## 3. I/O 与错误处理

```c
struct device *dev = device_find("uart0"); /* 名以 DTS 为准 */
if (!dev)
    return VFS_ERR_NODEV;

int ret = device_open(dev, NULL);
if (ret != VFS_OK)
    return ret;

ret = device_write(dev, buf, len, 100 /* ms */);
(void)device_close(dev);
return ret;
```

约定：

- 所有 HAL/VFS/bus 风格 API：`int` 返回值，**禁止**忽略（注意 `COMPAT_WARN_UNUSED_RESULT`）。  
- 超时用毫秒；`OSAL_WAIT_FOREVER` 仅在明确可接受阻塞处使用。  
- ioctl 的 `cmd` 与参数布局由各 `vfs-*.h` 定义；汇总见 [peripherals.md](peripherals.md)。  

---

## 4. 任务与同步

- 创建任务：`osal_task_create` / `osal_task_create_handle`（不要直接 `xTaskCreate`）。  
- 注意 **优先级数值语义随 OSAL 后端变化**（见 [osal_switching.md](osal_switching.md)）。  
- 设备锁由 `device_*` 包装持有；业务勿对同一设备再叠一层易死锁的锁顺序。  
- ISR 里只做短工作；其余投递 EventBus / 下半部 / 任务。  

---

## 5. 与启动顺序的关系

| 时机 | 适合做的事 |
| :--- | :--- |
| `pre_os_init` 之前 | 仅平台极早期（时钟） |
| `pre_os` 后、`start_tasks` 前 | 静态服务构造、无依赖 probe 的准备 |
| `start_tasks` 后 | 依赖已 probe 设备的业务、`device_find` |
| `system_init_complete` 后 | 允许中断；再启动调度器 |

---

## 6. 检查清单

- [ ] 业务 `.c` 无 `hal_` / 厂商 / 原生 RTOS 头  
- [ ] 所有 `device_*` 返回值有处理  
- [ ] 无在 ISR 打日志或拿 mutex  
- [ ] 切 OSAL 后重测优先级与栈  

---

## 相关文档

- [fast_path.md](fast_path.md) · [osal_switching.md](osal_switching.md)  
- [../ARCHITECTURE.md](architecture.md)
