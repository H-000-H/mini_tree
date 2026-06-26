## 8. 服务编写规范

mini_tree 应用层采用**异步"邮局"模式**编写业务逻辑：业务命令（强类型 POD）通过 `SystemCmd` 单例路由到对应 handler，handler 只做"投递"——把命令压入领域任务的专属 `osal_queue`，**不碰硬件、不阻塞**。真正的硬件操作在领域任务中执行。

### 8.1 SystemCmd 单例与命令注册

`SystemCmd` 是一个 C++ 单例（封装在 `system_cpp/include/system_cmd.hpp`），提供编译期类型安全的命令注册和运行时派发：

```cpp
// app_cmd_handlers.cpp
#include "system_cmd.hpp"
#include "system_log.h"

static constexpr const char* kTag = "AppCmd";

/* Handler 只做投递, 不碰硬件, 不阻塞 */
static bool handleLedSet(const CmdLedSet& cmd, void* /*ctx*/)
{
    if (osal_queue_send(g_led_queue, &cmd, 0) != true) {
        SYS_LOGW(kTag, "Led queue full — command dropped");
        return false;
    }
    return true;
}

static bool handleFlashErase(const CmdFlashErase& cmd, void* /*ctx*/)
{
    if (osal_queue_send(g_flash_queue, &cmd, 0) != true) {
        SYS_LOGW(kTag, "Flash queue full — command dropped");
        return false;
    }
    return true;
}

void app_cmd_handlers_register(void)
{
    SystemCmd& sys = SystemCmd::getInstance();
    sys.registerCmd<CmdLedSet>("led_set",       handleLedSet);
    sys.registerCmd<CmdFlashErase>("flash_erase", handleFlashErase);
    SYS_LOGI(kTag, "async handlers registered");
}
```

### 8.2 命令结构体定义

每个命令是一个 POD 结构体，由"命令名（null 结尾）+ 参数字节"打包通过 SPI/UART 传入。在 `app_cmd_handlers.hpp` 中集中声明：

```cpp
// app_cmd_handlers.hpp
#pragma once
#include "osal.h"

struct CmdLedSet {
    uint8_t r, g, b;
};

struct CmdFlashErase {
    uint32_t sector_addr;
};

extern osal_queue_handle_t g_led_queue;
extern osal_queue_handle_t g_flash_queue;

void app_cmd_handlers_register(void);
```

### 8.3 生命周期与注册时机

业务命令注册必须在驱动探测之后、`mini_tree_start_tasks()` 之前（ESP-IDF 路径）；或在 `task_rtos_main()` 之前的任意时机（FreeRTOS 路径）：

```c
// ESP32-S3 app_rtos_start (components/app/src/app_freertos.cpp)
mini_tree_pre_os_init();          // Phase 1
board_register_all_drivers();
app_cmd_handlers_register();      // ← 命令注册 (start_tasks 前)
mini_tree_start_tasks();          // Phase 2
app_spi_task_start();             // 业务任务创建
app_led_task_start();
app_flash_task_start();
system_init_complete();
```

CH32V307（FreeRTOS）路径相同，命令注册在 `board_register_all_drivers()` 之后、`mini_tree_start_tasks()` 之前。

### 8.4 命令派发入口

收包任务（如 SPI RX）拿到原始字节后，按"命令名 + 参数"格式解析，调用 `SystemCmd::dispatch()` 路由到 handler：

```cpp
// app_spi_task.cpp
static void spi_rx_task_entry(void* arg)
{
    struct device* dev = static_cast<struct device*>(arg);
    uint8_t buf[128];

    for (;;) {
        system_wdt_feed();

        int len = device_read(dev, buf, sizeof(buf), OSAL_WAIT_FOREVER);
        if (len <= 0) continue;

        // 包格式: 命令名(null结尾) + 参数字节
        const char* cmd_name = reinterpret_cast<const char*>(buf);
        size_t name_len = std::strlen(cmd_name) + 1;
        if (name_len >= sizeof(buf)) {
            SYS_LOGW("SpiTask", "malformed packet");
            continue;
        }

        const void* cmd_args = buf + name_len;
        size_t args_len = static_cast<size_t>(len) - name_len;

        // 路由到 handler — 不传 dev 指针, handler 只做投递
        SystemCmd::getInstance().dispatch(cmd_name, cmd_args, args_len, nullptr);
    }
}
```

`dispatch` 内部根据命令名查表，反序列化参数到对应 `Cmd*` 结构体，调用 handler。

---

## 9. 应用层解耦规范

### 9.1 面向 VFS 设备树编程

业务层**不直接调用芯片 SDK**，而是通过 `device_find_by_label()` / `device_open()` / `device_read()` / `device_write()` / `device_ioctl()` 操作硬件。设备树 `label` 是业务代码与硬件的唯一耦合点：

```cpp
// app_led_task.cpp
static void led_task_entry(void* arg)
{
    struct device* dev = static_cast<struct device*>(arg);
    CmdLedSet cmd;

    for (;;) {
        system_wdt_feed();

        // 阻塞在自己的队列上, 不影响 SPI RX 收包
        if (osal_queue_receive(g_led_queue, &cmd, OSAL_WAIT_FOREVER) != true)
            continue;

        ws2812_color color = { cmd.r, cmd.g, cmd.b };
        int ret = device_ioctl(dev, WS2812_CMD_SET_COLOR, &color, sizeof(color), 100);
        if (ret != VFS_OK) {
            SYS_LOGE("LedTask", "set_color failed: %d", ret);
        }
    }
}

void app_led_task_start(void)
{
    struct device* dev = device_find_by_label("ws2812");
    if (IS_ERR(dev)) {
        SYS_LOGW("LedTask", "ws2812 not found — disabled");
        return;
    }
    if (device_open(dev, nullptr) != VFS_OK) {
        SYS_LOGE("LedTask", "device_open failed");
        return;
    }

    g_led_queue = osal_queue_create(2, sizeof(CmdLedSet));
    if (!g_led_queue) {
        SYS_LOGE("LedTask", "queue create failed");
        return;
    }

    task_manager_create_task("led", 768, 10, led_task_entry, dev, 0);
    SYS_LOGI("LedTask", "started, device=%s", device_get_name(dev));
}
```

更换芯片时，业务代码无需修改——底层 HAL 实现切换由设备树 `compatible` 字段和 `hal/` 平台实现完成。

### 9.2 关键 API

| API | 用途 | 示例 |
|-----|------|------|
| `device_find_by_label("ws2812")` | 按 DTS label 查设备 | 返回 `struct device*`，失败返回 `ERR_PTR` |
| `IS_ERR(dev)` | 检查返回值是否为错误指针 | 在 `device_find_by_label` 之后必须检查 |
| `device_get_status(dev)` | 获取设备状态（DISABLED / READY / PROBED / RUNNING） | DTS 中 `status="disabled"` 的设备会返回 `DEVICE_STATUS_DISABLED` |
| `device_open(dev, nullptr)` | 打开设备（持锁、调 `ops->open`） | 返回 `VFS_OK` 或负错误码 |
| `device_read(dev, buf, len, timeout_ms)` | 阻塞读 | 返回实际读取的字节数（≤0 为错误/超时） |
| `device_write(dev, buf, len, timeout_ms)` | 阻塞写 | 同上 |
| `device_ioctl(dev, cmd, &arg, sizeof(arg), timeout_ms)` | 设备控制命令（如 `WS2812_CMD_SET_COLOR`、`W25Q64_CMD_SECTOR_ERASE`） | 返回 `VFS_OK` 或负错误码 |
| `device_get_name(dev)` | 返回设备名（用于日志） | `SYS_LOGI(kTag, "device=%s", device_get_name(dev))` |
| `task_manager_create_task("name", stack, prio, entry, arg, core_id)` | 创建业务任务并自动订阅 TWDT | `core_id` 为 0 时单核，AMP 双核时指定核心 |

### 9.3 异步"邮局"事件驱动

业务层解耦的核心理念：**命令发布者与硬件执行者通过队列解耦**，两者无需感知对方存在。

```
SPI RX 任务（收包）
   │ SystemCmd::dispatch("led_set", args, len, nullptr)
   ▼
SystemCmd 单例 ──→ handleLedSet() ──→ osal_queue_send(g_led_queue, &cmd, 0)
                                         │
                                         ▼
                                    LED 任务（领域任务）
                                    osal_queue_receive(g_led_queue, ...)
                                    device_ioctl(dev, WS2812_CMD_SET_COLOR, ...)
```

**关键约束**：

- Handler **绝不阻塞**——`osal_queue_send` 的 timeout 传 `0`，队列满则丢弃并返回 false
- Handler **绝不持有设备指针**——`dispatch` 的 `ctx` 参数传 `nullptr`，硬件操作在领域任务内完成
- 每个领域任务拥有**专属队列**，互不影响（Flash 擦除可阻塞 30 秒，但不影响 LED 任务和 SPI 收包）
- 领域任务通过 `task_manager_create_task()` 创建，自动订阅 Task WDT（3 秒未喂狗触发复位）

### 9.4 EventBus 通信（可选）

EventBus 用于框架级事件（如 `EVENT_SYS_READY`、`EVENT_SYS_FAULT`），不适合作为业务命令通道。业务命令走 `SystemCmd`，框架事件走 `EventBus`：

```cpp
// 订阅框架事件
EventBus::getInstance().subscribe(
    EVENT_SYS_READY, EVENT_SYS_READY,
    [](const Event& event, void* user_data) {
        // 系统就绪回调
    }
);

// 发布框架事件
EventBus::getInstance().post(EVENT_SYS_READY, 0);

// 事件 ID 分配:
//   0x0000 - 0x0FFF  框架保留 (EVENT_SYS_BOOT / READY / FAULT / DEVICE_REMOVED)
//   0x1000 - 0xFFFF  用户自定义 (EVENT_USER_BASE + n)
```

> **重要约束**:
> - `subscribe()` 只能在 Phase 2 点火完成前调用，封表后返回 false
> - `post()` 在 Phase 1 完成前静默丢弃事件，防止全局构造函数偷跑
