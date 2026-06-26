#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

/* ── 框架级事件 ID (框架内部使用, 不涉及任何业务语义) ── */
#define EVENT_SYS_BOOT   0x0000     /* 系统冷启动完成 */
#define EVENT_SYS_READY  0x0001     /* 所有框架任务已就绪 */
#define EVENT_SYS_FAULT  0x0002     /* 系统级故障, 进入安全状态 */
#define EVENT_SYS_DEVICE_REMOVED 0x0003  /* 设备从设备树中移除 */

/* ── 用户事件基线 ──
 * 用户工程在业务代码中基于此值定义自有事件:
 *   #define EVENT_MY_FEATURE  (EVENT_USER_BASE + 0)
 *   #define EVENT_MY_TIMER    (EVENT_USER_BASE + 1)
 * 框架只搬运事件 ID, 不解释其含义.
 */
#define EVENT_USER_BASE  0x1000u

struct Event
{
    uint32_t id;            /* 事件 ID (框架级或用户定义) */
    uintptr_t arg;
};

/* 事件回调类型 */
typedef void (*event_callback_t)(const struct Event* event, void* user_data);

/* ── EventBus C API ── */
bool event_bus_init(void) COMPAT_WARN_UNUSED_RESULT;
bool event_bus_subscribe(uint32_t id_min, uint32_t id_max,
                         event_callback_t callback, void* user_data) COMPAT_WARN_UNUSED_RESULT;
bool event_bus_post(uint32_t id, uintptr_t arg) COMPAT_WARN_UNUSED_RESULT;
bool event_bus_post_from_isr(uint32_t id, uintptr_t arg, bool* px_yield_required)
    COMPAT_WARN_UNUSED_RESULT;
void event_bus_start(void);
void event_bus_stop(void);
void event_bus_seal(void);
size_t event_bus_dropped_count(void);

#ifdef __cplusplus
}
#endif

#endif /* EVENT_BUS_H */

