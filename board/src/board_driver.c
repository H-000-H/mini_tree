/* SPDX-License-Identifier: Apache-2.0 */
/*
 * board_driver.c — 板级驱动核心实现
 *
 * board_driver_probe_all: 3 趟 deferred probe, 按依赖拓扑顺序匹配驱动,
 *   失败按 criticality 分级 (FATAL 触发 OSAL_PANIC, WARNING 告警, IGNORE 静默).
 * board_driver_remove_all: 逆 probe 顺序卸载, 失败保留 ERROR 状态.
 * 实现安全停机子系统 (safety pin + 回调 + emergency_stop_all_cores).
 */
#include "board_devtable.h"
#include "compiler_compat.h"
#include "config.h"
#include "driver.h"
#include "hal_amp.h"
#include "hal_gpio.h"
#include "hal_platform_safety.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include <stdio.h>
#include <string.h>

#include "compiler_compat_poison.h"

static const char* k_tag = "board_drv";

static volatile int s_shutdown_entered = 0;

/* ═══════════════════════════════════════════════════════════════════
 *  安全停机子系统
 *  由 Kconfig CONFIG_SAFETY_SHUTDOWN 控制.
 *  关闭时: board_safety_add_pin / register_shutdown 为空操作,
 *          system_safety_hardware_shutdown 仅执行 CPU 停机.
 * ═══════════════════════════════════════════════════════════════════ */
#ifdef CONFIG_SAFETY_SHUTDOWN

struct safety_pin
{
    int pin; /**< GPIO 引脚编号 */
    int safe_level; /**< 安全停机时的目标电平 */
};

static struct safety_pin g_safety_pins[BOARD_MAX_SAFETY_PINS];
static int g_safety_pin_count;

static safety_shutdown_fn_t g_safety_cbs[BOARD_SAFETY_MAX_CALLBACKS];
static int g_safety_cb_count;

/**
 * @brief 注册安全停机 GPIO 引脚及安全电平
 * @param pin GPIO 引脚编号
 * @param safe_level 安全态电平 (0/1)
 */
void board_safety_add_pin(int pin, int safe_level)
{
    if (g_safety_pin_count < BOARD_MAX_SAFETY_PINS)
    {
        g_safety_pins[g_safety_pin_count].pin = pin;
        g_safety_pins[g_safety_pin_count].safe_level = safe_level;
        g_safety_pin_count++;
    }
}

/**
 * @brief 注册安全停机回调 (probe 前调用)
 * @param fn 停机回调函数, NULL 忽略
 */
void board_safety_register_shutdown(safety_shutdown_fn_t fn)
{
    if (fn && g_safety_cb_count < BOARD_SAFETY_MAX_CALLBACKS)
        g_safety_cbs[g_safety_cb_count++] = fn;
}

/* ── 安全硬件伪驱动: 读取 DTS pin_N / safe_level_N 列表 ── */
/**
 * @brief 从 DTS 属性注册安全停机 GPIO 引脚
 * @param pdev 安全硬件 device 指针
 * @return 成功返回 VFS_OK
 */
static int board_safety_hw_probe(struct device* pdev)
{
    int pin;
    int safe_level = 0;
    int idx = 0;
    char pin_prop[16], level_prop[16];
    while (idx < BOARD_MAX_SAFETY_PINS)
    {
        snprintf(pin_prop, sizeof(pin_prop), "pin_%d", idx);
        snprintf(level_prop, sizeof(level_prop), "safe_level_%d", idx);
        if (device_get_prop_int(pdev, pin_prop, &pin) != VFS_OK)
            break;
        device_get_prop_int(pdev, level_prop, &safe_level);
        board_safety_add_pin(pin, safe_level);
        idx++;
    }
    DRV_LOGI(k_tag, "safety-hw: %d shutdown pins registered", g_safety_pin_count);
    return VFS_OK;
}

/**
 * @brief 清除已注册的安全停机引脚与回调
 * @param pdev device 指针 (未使用)
 * @return 成功返回 VFS_OK
 */
static int board_safety_hw_remove(struct device* pdev)
{
    (void)pdev;
    g_safety_pin_count = 0;
    g_safety_cb_count = 0;
    return VFS_OK;
}

DRIVER_REGISTER(board_safety_hw, "board,safety-hw", board_safety_hw_probe, board_safety_hw_remove);

#else /* !CONFIG_SAFETY_SHUTDOWN */

/**
 * @brief 注册安全停机 GPIO 引脚 (CONFIG_SAFETY_SHUTDOWN 未启用时为 no-op)
 * @param pin GPIO 引脚编号
 * @param safe_level 安全态电平
 */
void board_safety_add_pin(int pin, int safe_level)
{
    (void)pin;
    (void)safe_level;
}

/**
 * @brief 注册安全停机回调 (CONFIG_SAFETY_SHUTDOWN 未启用时为 no-op)
 * @param fn 停机回调函数
 */
void board_safety_register_shutdown(safety_shutdown_fn_t fn) { (void)fn; }

#endif /* CONFIG_SAFETY_SHUTDOWN */

/**
 * @brief 检查设备依赖是否不可用或已出错
 * @param pdev 待检查 device 指针
 * @return 1 表示存在缺失/ERROR/REMOVED 依赖, 0 表示依赖均可用
 */
static int device_dependency_not_ready(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev) || !pdev->node || !pdev->node->deps)
        return 0;

    for (int i = 0; i < pdev->node->dep_count; i++)
    {
        struct device* dep = board_dev_get(pdev->node->deps[i]);
        if (IS_ERR_OR_NULL(dep))
            return 1;

        /* DIRECT 设备不参与 VFS 生命周期, 视为始终就绪 */
        if (dep->node && (dep->node->flags & DEVICE_FLAG_DIRECT))
            continue;

        enum device_status status = device_get_status(dep);
        if (status == DEVICE_STATUS_ERROR || status == DEVICE_STATUS_REMOVED)
            return 1;
    }
    return 0;
}

/**
 * @brief 检查设备依赖是否尚未 probe 完成
 * @param pdev 待检查 device 指针
 * @return 1 表示存在缺失或未 PROBED/RUNNING/SUSPENDED 的依赖, 0 表示依赖均已就绪
 */
static int device_dependency_pending(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev) || !pdev->node || !pdev->node->deps)
        return 0;

    for (int i = 0; i < pdev->node->dep_count; i++)
    {
        struct device* dep = board_dev_get(pdev->node->deps[i]);
        if (IS_ERR_OR_NULL(dep))
            return 1;

        /* DIRECT 设备始终就绪 */
        if (dep->node && (dep->node->flags & DEVICE_FLAG_DIRECT))
            continue;

        enum device_status status = device_get_status(dep);
        if (status != DEVICE_STATUS_PROBED && status != DEVICE_STATUS_RUNNING &&
            status != DEVICE_STATUS_SUSPENDED)
        {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief 按设备 criticality 分级处理 probe 失败
 * @param pdev probe 失败的 device 指针
 * @param id 设备 ID (未使用, 保留供扩展)
 */
static void handle_probe_failure(struct device* pdev, device_id_t id)
{
    enum device_criticality crit = device_get_criticality(pdev);
    switch (crit)
    {
    case DEVICE_CRIT_FATAL:
        DRV_LOGE(k_tag, "FATAL: '%s' probe failed — initiating safe shutdown",
                 device_get_name(pdev));
        OSAL_PANIC("FATAL device '%s' probe failed", device_get_name(pdev));
        break;
    case DEVICE_CRIT_IGNORE:
        break;
    case DEVICE_CRIT_WARNING:
    default:
        DRV_LOGW(k_tag, "non-fatal probe failure for '%s'", device_get_name(pdev));
        break;
    }
}

/**
 * @brief 级联禁用依赖失败设备的所有子设备
 * @param failed_id probe 失败的设备 ID
 */
static void disable_dependents(device_id_t failed_id)
{
    int count = 0;
    const device_id_t* list = board_cascade_get(failed_id, &count);
    if (!list || count == 0)
        return;

    for (int i = 0; i < count; i++)
    {
        struct device* child = board_dev_get(list[i]);
        if (IS_ERR_OR_NULL(child))
            continue;
        enum device_status st = device_get_status(child);
        if (st == DEVICE_STATUS_DISABLED || st == DEVICE_STATUS_REMOVED)
            continue;
        COMPAT_IGNORE_RESULT(device_set_status(child, DEVICE_STATUS_DISABLED));
        DRV_LOGW(k_tag, "cascade: '%s' disabled (dependency '%s' failed)", device_get_name(child),
                 device_get_name(board_dev_get(failed_id)));
    }
}

/**
 * @brief 系统安全硬件停机 (回调/PWM/GPIO/CPU 紧急停机)
 * @param reason 停机原因字符串 (可为 NULL)
 */
void system_safety_hardware_shutdown(const char* reason)
{
    if (__sync_val_compare_and_swap(&s_shutdown_entered, 0, 1) != 0)
        return;

#ifdef CONFIG_SAFETY_SHUTDOWN
    (void)reason;

    if (!osal_in_isr())
    {
        for (int i = 0; i < g_safety_cb_count; i++)
            if (g_safety_cbs[i])
                g_safety_cbs[i]();
    }

    hal_pwm_force_stop_all();

    /* 注: 所有 GPIO 写操作必须在 hal_cpu_emergency_stop_all_cores() 之前完成,
     * 因为 CPU STOP 后可能冻结外设总线, 后续 GPIO 写将失效. */
    for (int i = 0; i < g_safety_pin_count; i++)
        hal_gpio_set_level(g_safety_pins[i].pin, g_safety_pins[i].safe_level);
    hal_gpio_set_level(BOARD_SAFE_STATE_FAULT_LED_PIN, 1);

    hal_cpu_emergency_stop_all_cores();
#else
    /* 最小安全停机: CONFIG_SAFETY_SHUTDOWN 未启用, 仅停机.
     * 移植阶段用户工程可在此处添加自己的安全逻辑后再 halt. */
    (void)reason;
#endif /* CONFIG_SAFETY_SHUTDOWN */

    while (1)
        ;
}

/**
 * @brief 注册所有板级驱动 (当前由 DRIVER_REGISTER 宏静态注册, 此处为空)
 */
void board_register_all_drivers(void) {}

/**
 * @brief 按索引取 probe 顺序中的 device_id (独立函数, 返回值走 a2)
 */
static device_id_t board_probe_order_at(int index)
{
    const device_id_t* order = board_probe_order();
    int count = board_probe_order_count();
    if (!order || index < 0 || index >= count)
        return (device_id_t)DEV_ID_COUNT;
    return order[index];
}

/**
 * @brief 按 probe 顺序探测所有设备 (最多 3 趟 deferred)
 * @return probe 失败设备数量
 *
 * @note Xtensa windowed ABI: call8 被调方写 a10-a15 即覆盖调用方 a2-a7.
 *       循环状态必须落在栈上 (volatile), 否则 handle_probe_failure 等
 *       日志路径会打坏 count/i/order, 表现为只 probe 第一个设备后提前结束.
 */
int board_driver_probe_all(void)
{
#ifdef CONFIG_PRODUCTION_LOG
    production_log_init();
#endif

    DRV_LOGI(k_tag, "probing all devices from compile-time DTS table ...");
    volatile uint8_t ok = 0;
    volatile uint8_t fail = 0;
    volatile int count = board_probe_order_count();
    volatile int deferred_prev = 0;

    for (volatile int pass = 0; pass < 3; pass++)
    {
        volatile int deferred = 0;

        for (volatile int i = 0; i < count; i++)
        {
            device_id_t id = board_probe_order_at(i);
            struct device* pdev = board_dev_get(id);
            probe_fn_t probe = board_probe_get_fn(id);

            if ((int)id < 0 || (int)id >= board_dev_count())
                continue;
            if (IS_ERR_OR_NULL(pdev) || device_get_status(pdev) == DEVICE_STATUS_DISABLED)
                continue;
            /* DIRECT 设备不经过 VFS probe, 跳过 */
            if (pdev->node && (pdev->node->flags & DEVICE_FLAG_DIRECT))
            {
                DRV_LOGV(k_tag, "skip probe: '%s' (direct)", device_get_name(pdev));
                continue;
            }
            if (device_get_status(pdev) == DEVICE_STATUS_PROBED ||
                device_get_status(pdev) == DEVICE_STATUS_RUNNING)
                continue;

            if (device_dependency_not_ready(pdev))
            {
                if (device_dependency_pending(pdev))
                {
                    deferred++;
                    continue;
                }
                COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_DISABLED));
                fail++;
                DRV_LOGW(k_tag, "skip '%s': dependency permanently unavailable",
                         device_get_name(pdev));
                continue;
            }

            if (!probe)
            {
                const char* name = device_get_name(pdev);
                /* 板级根节点等无名容器: 无驱动属预期, 静默禁用 */
                if (!name || !name[0])
                {
                    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_DISABLED));
                    continue;
                }
                DRV_LOGW(k_tag, "no generated probe for '%s' (compat=%s)", name,
                         device_get_compatible(pdev));
                COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_DISABLED));
                handle_probe_failure(pdev, id);
                disable_dependents(id);
                fail++;
                continue;
            }

            DRV_LOGI(k_tag, "probing '%s' (%s) ...", device_get_name(pdev),
                     device_get_compatible(pdev));
            int ret = probe(pdev);
            if (ret == VFS_OK)
            {
                COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_PROBED));
                int open_ret = VFS_OK;
                if (pdev->ops && (pdev->ops->open || pdev->ops->init))
                    open_ret = device_open(pdev, NULL);
                if (open_ret != VFS_OK)
                {
                    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_ERROR));
                    DRV_LOGE(k_tag, "device_open FAILED: %s (ret=%d)", device_get_name(pdev),
                             open_ret);
                    handle_probe_failure(pdev, id);
                    disable_dependents(id);
                    fail++;
                    continue;
                }
                ok++;
            }
            else if (ret == VFS_ERR_DEFER)
            {
                DRV_LOGI(k_tag, "DEFER '%s': phandle dependency not yet probed",
                         device_get_name(pdev));
                deferred++;
            }
            else
            {
                COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_ERROR));
                DRV_LOGE(k_tag, "probe FAILED: %s (ret=%d)", device_get_name(pdev), ret);
                handle_probe_failure(pdev, id);
                disable_dependents(id);
                fail++;
            }
        }

        if (deferred == 0)
            break;
        if (deferred == deferred_prev)
        {
            DRV_LOGE(k_tag, "EPROBE_DEFER stall: %d devices stuck after %d passes", (int)deferred,
                     (int)pass + 1);
            for (volatile int i = 0; i < count; i++)
            {
                device_id_t id = board_probe_order_at(i);
                struct device* pdev = board_dev_get(id);
                if (!IS_ERR_OR_NULL(pdev) && device_get_status(pdev) != DEVICE_STATUS_PROBED &&
                    device_get_status(pdev) != DEVICE_STATUS_RUNNING &&
                    device_dependency_pending(pdev))
                {
                    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_DISABLED));
                    fail++;
                    DRV_LOGE(k_tag, "DEFER stall: '%s' permanently disabled",
                             device_get_name(pdev));
                }
            }
            break;
        }
        deferred_prev = deferred;
        DRV_LOGI(k_tag, "pass %d: %d deferred, retrying ...", (int)pass + 1, (int)deferred);
    }

    DRV_LOGI(k_tag, "probe complete: %d ok, %d fail", (int)ok, (int)fail);
    return (int)fail;
}

/**
 * @brief 按逆 probe 顺序卸载所有设备
 * @return 成功返回 VFS_OK
 */
int board_driver_remove_all(void)
{
    DRV_LOGI(k_tag, "removing all devices (reverse probe order) ...");

    int count = board_probe_order_count();

    for (int i = count - 1; i >= 0; i--)
    {
        device_id_t id = board_probe_order()[i];
        struct device* pdev = board_dev_get(id);

        if (IS_ERR_OR_NULL(pdev))
            continue;

        enum device_status status = device_get_status(pdev);
        if (status != DEVICE_STATUS_PROBED && status != DEVICE_STATUS_RUNNING &&
            status != DEVICE_STATUS_SUSPENDED)
        {
            continue;
        }

        remove_fn_t remove_fn = board_remove_get_fn(id);
        if (remove_fn)
        {
            int ret = remove_fn(pdev);
            if (ret != VFS_OK)
            {
                DRV_LOGE(k_tag, "remove FAILED: %s (ret=%d) — keeping ERROR state",
                         device_get_name(pdev), ret);
                COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_ERROR));
                continue;
            }
        }
        COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_READY));
    }
    return VFS_OK;
}