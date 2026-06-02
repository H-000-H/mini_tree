#include "config.h"
#include "driver.h"
#include "VFS.h"
#include "osal.h"
#include "hal_pwm.h"
#include "hal_cpu.h"
#include "hal_gpio.h"
#include "system_log.hpp"
#include "board_devtable.h"

#include <stdio.h>
#include <string.h>

static const char* kTag = "board_drv";


static volatile int s_shutdown_entered = 0;

/* ═══════════════════════════════════════════════════════════════════
 *  IEC 61508 安全停机子系统
 *  由 Kconfig CONFIG_SAFETY_SHUTDOWN 控制.
 *  关闭时: board_safety_add_pin / register_shutdown 为空操作,
 *          system_safety_hardware_shutdown 仅执行 CPU 停机.
 * ═══════════════════════════════════════════════════════════════════ */
#ifdef CONFIG_SAFETY_SHUTDOWN

typedef struct
{
    hal_pin_t pin;
    int       safe_level;
} safety_pin_t;

static safety_pin_t g_safety_pins[BOARD_MAX_SAFETY_PINS];
static int          g_safety_pin_count;

static safety_shutdown_fn_t g_safety_cbs[BOARD_SAFETY_MAX_CALLBACKS];
static int                  g_safety_cb_count;

void board_safety_add_pin(hal_pin_t pin, int safe_level)
{
    if (g_safety_pin_count < BOARD_MAX_SAFETY_PINS)
    {
        g_safety_pins[g_safety_pin_count].pin        = pin;
        g_safety_pins[g_safety_pin_count].safe_level = safe_level;
        g_safety_pin_count++;
    }
}

void board_safety_register_shutdown(safety_shutdown_fn_t fn)
{
    if (fn && g_safety_cb_count < BOARD_SAFETY_MAX_CALLBACKS)
    {
        g_safety_cbs[g_safety_cb_count++] = fn;
    }
}

/* ── 安全硬件伪驱动: 读取 DTS pin_N / safe_level_N 列表 ── */
static int board_safety_hw_probe(device_t* dev)
{
    int pin;
    int safe_level = 0;
    int idx = 0;
    char pin_prop[16], level_prop[16];
    while (idx < BOARD_MAX_SAFETY_PINS)
    {
        snprintf(pin_prop, sizeof(pin_prop), "pin_%d", idx);
        snprintf(level_prop, sizeof(level_prop), "safe_level_%d", idx);
        if (device_get_prop_int(dev, pin_prop, &pin) != 0) break;
        device_get_prop_int(dev, level_prop, &safe_level);
        board_safety_add_pin(pin, safe_level);
        idx++;
    }
    DRV_LOGI(kTag, "safety-hw: %d shutdown pins registered", g_safety_pin_count);
    return 0;
}

static int board_safety_hw_remove(device_t* dev)
{
    (void)dev;
    g_safety_pin_count = 0;
    g_safety_cb_count  = 0;
    return 0;
}

DRIVER_REGISTER(board_safety_hw, "board,safety-hw",
                board_safety_hw_probe, board_safety_hw_remove);

#else /* !CONFIG_SAFETY_SHUTDOWN */

void board_safety_add_pin(hal_pin_t pin, int safe_level)
{
    (void)pin; (void)safe_level;
}

void board_safety_register_shutdown(safety_shutdown_fn_t fn)
{
    (void)fn;
}

#endif /* CONFIG_SAFETY_SHUTDOWN */

static int device_dependency_not_ready(const device_t* dev)
{
    if (!dev || !dev->node || !dev->node->deps) return 0;

    for (int i = 0; i < dev->node->dep_count; i++)
    {
        device_t* dep = board_dev_get(dev->node->deps[i]);
        if (!dep) return 1;

        /* DIRECT 设备不参与 VFS 生命周期, 视为始终就绪 */
        if (dep->node && (dep->node->flags & DEVICE_FLAG_DIRECT))
            continue;

        device_status_t status = device_get_status(dep);
        if (status == DEVICE_STATUS_ERROR ||
            status == DEVICE_STATUS_REMOVED)
        {
            return 1;
        }
    }
    return 0;
}

static int device_dependency_pending(const device_t* dev)
{
    if (!dev || !dev->node || !dev->node->deps) return 0;

    for (int i = 0; i < dev->node->dep_count; i++)
    {
        device_t* dep = board_dev_get(dev->node->deps[i]);
        if (!dep) return 1;

        /* DIRECT 设备始终就绪 */
        if (dep->node && (dep->node->flags & DEVICE_FLAG_DIRECT))
            continue;

        device_status_t status = device_get_status(dep);
        if (status != DEVICE_STATUS_PROBED &&
            status != DEVICE_STATUS_RUNNING &&
            status != DEVICE_STATUS_SUSPENDED)
        {
            return 1;
        }
    }
    return 0;
}

static void handle_probe_failure(device_t* dev, device_id_t id)
{
    device_criticality_t crit = device_get_criticality(dev);
    switch (crit)
    {
    case DEVICE_CRIT_FATAL:
        DRV_LOGE(kTag, "FATAL: '%s' probe failed — initiating safe shutdown",
                 device_get_name(dev));
        OSAL_PANIC("FATAL device '%s' probe failed", device_get_name(dev));
        break;
    case DEVICE_CRIT_IGNORE:
        break;
    case DEVICE_CRIT_WARNING:
    default:
        DRV_LOGW(kTag, "non-fatal probe failure for '%s'", device_get_name(dev));
        break;
    }
}

static void disable_dependents(device_id_t failed_id)
{
    int count = 0;
    const device_id_t* list = board_cascade_get(failed_id, &count);
    if (!list || count == 0) return;

    for (int i = 0; i < count; i++)
    {
        device_t* child = board_dev_get(list[i]);
        if (!child) continue;
        device_status_t st = device_get_status(child);
        if (st == DEVICE_STATUS_DISABLED || st == DEVICE_STATUS_REMOVED) continue;
        (void)device_set_status(child, DEVICE_STATUS_DISABLED);
        DRV_LOGW(kTag, "cascade: '%s' disabled (dependency '%s' failed)",
                 device_get_name(child), device_get_name(board_dev_get(failed_id)));
    }
}

void system_safety_hardware_shutdown(const char* reason)
{
    if (__sync_val_compare_and_swap(&s_shutdown_entered, 0, 1) != 0)
    {
        return;
    }

#ifdef CONFIG_SAFETY_SHUTDOWN
    (void)reason;

    if (!osal_in_isr())
    {
        for (int i = 0; i < g_safety_cb_count; i++)
        {
            if (g_safety_cbs[i]) g_safety_cbs[i]();
        }
    }

    hal_pwm_force_stop_all();

    /* 注: 所有 GPIO 写操作必须在 hal_cpu_emergency_stop_all_cores() 之前完成,
     * 因为 CPU STOP 后可能冻结外设总线, 后续 GPIO 写将失效. */
    for (int i = 0; i < g_safety_pin_count; i++)
    {
        hal_gpio_set_level(g_safety_pins[i].pin, g_safety_pins[i].safe_level);
    }
    hal_gpio_set_level(BOARD_SAFE_STATE_FAULT_LED_PIN, 1);

    hal_cpu_emergency_stop_all_cores();
#else
    /* 最小安全停机: CONFIG_SAFETY_SHUTDOWN 未启用, 仅停机.
     * 移植阶段用户工程可在此处添加自己的安全逻辑后再 halt. */
    (void)reason;
#endif /* CONFIG_SAFETY_SHUTDOWN */

    while (1)
    { ; }
}

void board_register_all_drivers(void)
{
}

int board_driver_probe_all(void)
{
#ifdef CONFIG_PRODUCTION_LOG
    production_log_init();
#endif

    DRV_LOGI(kTag, "probing all devices from compile-time DTS table ...");
    uint8_t ok = 0, fail = 0;

    const device_id_t* order = board_probe_order();
    int count = board_probe_order_count();

    int deferred_prev = 0;

    for (int pass = 0; pass < 3; pass++)
    {
        int deferred = 0;

        for (int i = 0; i < count; i++)
        {
            device_id_t id = order[i];
            device_t* dev = board_dev_get(id);
            probe_fn_t probe = board_probe_get_fn(id);

            if (!dev || device_get_status(dev) == DEVICE_STATUS_DISABLED)
            {
                continue;
            }
            /* DIRECT 设备不经过 VFS probe, 跳过 */
            if (dev->node && (dev->node->flags & DEVICE_FLAG_DIRECT))
            {
                DRV_LOGV(kTag, "skip probe: '%s' (direct)", device_get_name(dev));
                continue;
            }
            if (device_get_status(dev) == DEVICE_STATUS_PROBED ||
                device_get_status(dev) == DEVICE_STATUS_RUNNING)
            {
                continue;
            }

            if (device_dependency_not_ready(dev))
            {
                if (device_dependency_pending(dev))
                {
                    deferred++;
                    continue;
                }
                (void)device_set_status(dev, DEVICE_STATUS_DISABLED);
                fail++;
                DRV_LOGW(kTag, "skip '%s': dependency permanently unavailable",
                         device_get_name(dev));
                continue;
            }

            if (!probe)
            {
                DRV_LOGW(kTag, "no generated probe for '%s' (compat=%s)",
                         device_get_name(dev), device_get_compatible(dev));
                (void)device_set_status(dev, DEVICE_STATUS_DISABLED);
                handle_probe_failure(dev, id);
                disable_dependents(id);
                fail++;
                continue;
            }

            DRV_LOGI(kTag, "probing '%s' (%s) ...",
                     device_get_name(dev), device_get_compatible(dev));
            int ret = probe(dev);
            if (ret == 0)
            {
                (void)device_set_status(dev, DEVICE_STATUS_PROBED);
                int open_ret = 0;
                if (dev->ops && (dev->ops->open || dev->ops->init))
                {
                    open_ret = device_open(dev, NULL);
                }
                if (open_ret != 0)
                {
                    (void)device_set_status(dev, DEVICE_STATUS_ERROR);
                    handle_probe_failure(dev, id);
                    disable_dependents(id);
                    fail++;
                    DRV_LOGE(kTag, "device_open FAILED: %s (ret=%d)", device_get_name(dev), open_ret);
                }
                else
                {
                    ok++;
                }
            }
            else if (ret == VFS_ERR_DEFER)
            {
                DRV_LOGI(kTag, "DEFER '%s': phandle dependency not yet probed",
                         device_get_name(dev));
                deferred++;
            }
            else
            {
                (void)device_set_status(dev, DEVICE_STATUS_ERROR);
                handle_probe_failure(dev, id);
                disable_dependents(id);
                fail++;
                DRV_LOGE(kTag, "probe FAILED: %s (ret=%d)", device_get_name(dev), ret);
            }
        }

        if (deferred == 0) break;
        if (deferred == deferred_prev)
        {
            DRV_LOGE(kTag, "EPROBE_DEFER stall: %d devices stuck after %d passes",
                     deferred, pass + 1);
            for (int i = 0; i < count; i++)
            {
                device_t* dev = board_dev_get(order[i]);
                if (dev && device_get_status(dev) != DEVICE_STATUS_PROBED &&
                    device_get_status(dev) != DEVICE_STATUS_RUNNING &&
                    device_dependency_pending(dev))
                {
                    (void)device_set_status(dev, DEVICE_STATUS_DISABLED);
                    fail++;
                    DRV_LOGE(kTag, "DEFER stall: '%s' permanently disabled",
                             device_get_name(dev));
                }
            }
            break;
        }
        deferred_prev = deferred;
        DRV_LOGI(kTag, "pass %d: %d deferred, retrying ...", pass + 1, deferred);
    }

    DRV_LOGI(kTag, "probe complete: %d ok, %d fail", ok, fail);
    return fail;
}

int board_driver_remove_all(void)
{
    DRV_LOGI(kTag, "removing all devices (reverse probe order) ...");

    const device_id_t* order = board_probe_order();
    int count = board_probe_order_count();

    for (int i = count - 1; i >= 0; i--)
    {
        device_id_t id = order[i];
        device_t* dev = board_dev_get(id);

        if (!dev)
            continue;

        device_status_t status = device_get_status(dev);
        if (status != DEVICE_STATUS_PROBED &&
            status != DEVICE_STATUS_RUNNING &&
            status != DEVICE_STATUS_SUSPENDED)
        {
            continue;
        }

        remove_fn_t remove_fn = board_remove_get_fn(id);
        if (remove_fn)
        {
            int ret = remove_fn(dev);
            if (ret != 0)
            {
                DRV_LOGE(kTag, "remove FAILED: %s (ret=%d) — keeping ERROR state",
                         device_get_name(dev), ret);
                (void)device_set_status(dev, DEVICE_STATUS_ERROR);
                continue;
            }
        }
        (void)device_set_status(dev, DEVICE_STATUS_READY);
    }
    return 0;
}