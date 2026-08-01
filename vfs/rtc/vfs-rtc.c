/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file        vfs-rtc.c
 * @brief       RTC VFS 实现 — open/close 引用计数 + ioctl 时间/闹钟/唤醒派发
 * @note        DTS 解析 hw-instance/async-prediv/sync-prediv/format-24h; 两层模型无 bus
 */
#define RTC_VFS_IMPL
#include "vfs-rtc.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "status.h"
#include "osal.h"
#include "compiler_compat.h"
#include "system_log.h"

#define RTC_VFS_POOL 2

struct vfs_rtc_priv
{
    struct file_operations ops;     /**< VFS 操作表 */
    struct hal_rtc_dev  rtc;        /**< HAL RTC 设备 */
    int                    pool_idx; /**< 池索引 */
};

static struct vfs_rtc_priv s_pool[RTC_VFS_POOL] COMPAT_ALIGNED(4);
static uint8_t             s_used[RTC_VFS_POOL] COMPAT_ALIGNED(4);
static osal_pool_t         s_pool_ctrl COMPAT_ALIGNED(4);
static const char* const   k_tag = "vfs_rtc";

/**
 * @brief RTC VFS 私有数据池启动初始化
 */
pre_execution(160)
static void vfs_rtc_pool_boot(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_pool_ctrl, s_used, RTC_VFS_POOL));
}

/**
 * @brief RTC 设备打开: 引用计数, 首次打开时调用 hal_rtc_open
 * @param pdev 设备对象指针
 * @param arg 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_rtc_open(struct device* pdev, void* arg)
{
    struct vfs_rtc_priv*  priv;
    struct dev_lifecycle* lc;
    int first, ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_rtc_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = hal_rtc_open(&priv->rtc);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief RTC 设备关闭: 引用计数, 末次关闭时调用 hal_rtc_close
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_rtc_close(struct device* pdev)
{
    struct vfs_rtc_priv*  priv;
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_rtc_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last == 1)
        COMPAT_IGNORE_RESULT(hal_rtc_close(&priv->rtc));
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief RTC 命令: 设置当前时间
 * @param priv RTC 私有数据指针
 * @param arg rtc_time_arg 参数指针
 * @param arg_len 参数长度
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_set_time(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    const struct rtc_time_arg* a = (const struct rtc_time_arg*)arg;
    COMPAT_IGNORE_RESULT(to);
    if (!a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return hal_rtc_set_time(&priv->rtc, &a->time);
}

/**
 * @brief RTC 命令: 读取当前时间
 * @param priv RTC 私有数据指针
 * @param arg rtc_time_arg 输出参数指针
 * @param arg_len 参数长度
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_get_time(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    struct rtc_time_arg* a = (struct rtc_time_arg*)arg;
    COMPAT_IGNORE_RESULT(to);
    if (!a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return hal_rtc_get_time(&priv->rtc, &a->time);
}

/**
 * @brief RTC 命令: 设置闹钟 (回调固定为 NULL)
 * @param priv RTC 私有数据指针
 * @param arg rtc_time_arg 参数指针
 * @param arg_len 参数长度
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_set_alarm(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    const struct rtc_time_arg* a = (const struct rtc_time_arg*)arg;
    COMPAT_IGNORE_RESULT(to);
    if (!a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return hal_rtc_set_alarm(&priv->rtc, &a->time, NULL, NULL);
}

/**
 * @brief RTC 命令: 取消闹钟
 * @param priv RTC 私有数据指针
 * @param arg 未使用
 * @param arg_len 未使用
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_cancel_alarm(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len); COMPAT_IGNORE_RESULT(to);
    return hal_rtc_cancel_alarm(&priv->rtc);
}

/**
 * @brief RTC 命令: 设置唤醒定时器
 * @param priv RTC 私有数据指针
 * @param arg rtc_wakeup_arg 参数指针
 * @param arg_len 参数长度
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_set_wakeup(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    const struct rtc_wakeup_arg* a = (const struct rtc_wakeup_arg*)arg;
    COMPAT_IGNORE_RESULT(to);
    if (!a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return hal_rtc_set_wakeup_timer(&priv->rtc, a->seconds);
}

/**
 * @brief RTC 命令: 取消唤醒定时器
 * @param priv RTC 私有数据指针
 * @param arg 未使用
 * @param arg_len 未使用
 * @param to 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int rtc_cmd_cancel_wakeup(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len); COMPAT_IGNORE_RESULT(to);
    return hal_rtc_cancel_wakeup_timer(&priv->rtc);
}

/**
 * @brief RTC 命令: 全局强制停止 RTC 硬件 (不依赖 priv 实例)
 * @param priv 未使用
 * @param arg 未使用
 * @param arg_len 未使用
 * @param to 未使用
 * @return 成功返回 VFS_OK
 */
static int rtc_cmd_force_stop(struct vfs_rtc_priv* priv, void* arg, size_t arg_len, uint32_t to)
{
    COMPAT_IGNORE_RESULT(priv); COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(arg_len); COMPAT_IGNORE_RESULT(to);
    hal_rtc_force_stop();
    return VFS_OK;
}

typedef int (*rtc_ioctl_fn)(struct vfs_rtc_priv*, void*, size_t, uint32_t);
static const rtc_ioctl_fn s_rtc_ioctl[RTC_CMD_COUNT] = {
    [RTC_CMD_SET_TIME - RTC_CMD_BASE - 1]      = rtc_cmd_set_time,
    [RTC_CMD_GET_TIME - RTC_CMD_BASE - 1]      = rtc_cmd_get_time,
    [RTC_CMD_SET_ALARM - RTC_CMD_BASE - 1]     = rtc_cmd_set_alarm,
    [RTC_CMD_CANCEL_ALARM - RTC_CMD_BASE - 1]  = rtc_cmd_cancel_alarm,
    [RTC_CMD_SET_WAKEUP - RTC_CMD_BASE - 1]    = rtc_cmd_set_wakeup,
    [RTC_CMD_CANCEL_WAKEUP - RTC_CMD_BASE - 1] = rtc_cmd_cancel_wakeup,
    [RTC_CMD_FORCE_STOP - RTC_CMD_BASE - 1]    = rtc_cmd_force_stop,
};

/**
 * @brief RTC ioctl 派发入口
 * @param pdev 设备对象指针
 * @param cmd 控制命令
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 未使用 (透传给子命令)
 * @return 成功返回 VFS_OK, 未知命令返回 VFS_ERR_INVAL, 失败返回负数错误码
 */
static int vfs_rtc_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct vfs_rtc_priv*  priv;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_rtc_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RTC_CMD_BASE;
    if (off < 1 || off > RTC_CMD_COUNT || !s_rtc_ioctl[off - 1])
        ret = VFS_ERR_INVAL;
    else
        ret = s_rtc_ioctl[off - 1](priv, arg, arg_len, timeout_ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations s_rtc_fops = {
    .open  = vfs_rtc_open,
    .close = vfs_rtc_close,
    .ioctl = vfs_rtc_ioctl,
};

/**
 * @brief 解析 RTC DTS 属性, 填入 hal_rtc_config
 * @param pdev 设备对象指针
 * @param cfg 输出的 RTC 配置结构指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_rtc_parse_dts(struct device* pdev, struct hal_rtc_config* cfg)
{
    int v;
    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    if (device_get_prop_int(pdev, "hw-instance", &v) != VFS_OK)
        return VFS_ERR_INVAL;
    cfg->rtc = (uintptr_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "clk-source", &v));
    cfg->clk_source = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "async-prediv", &v));
    cfg->async_prediv = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sync-prediv", &v));
    cfg->sync_prediv = (uint32_t)v;
    v = 1;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "format-24h", &v));
    cfg->format_24h = (uint32_t)v;
    v = -1;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irqn", &v));
    cfg->irqn = v;
    return VFS_OK;
}

/**
 * @brief RTC 设备探测: 解析 DTS, hal_rtc_init, 注册 fops
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_rtc_probe(struct device* pdev)
{
    struct vfs_rtc_priv* priv;
    int idx, ret;

    if (!pdev)
        return VFS_ERR_INVAL;
    idx = osal_pool_claim(&s_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;
    priv = &s_pool[idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = idx;
    ret = vfs_rtc_parse_dts(pdev, &priv->rtc.cfg);
    if (ret != VFS_OK)
        goto err;
    ret = hal_rtc_init(&priv->rtc, &priv->rtc.cfg);
    if (ret != VFS_OK)
        goto err;
    priv->ops = s_rtc_fops;
    pdev->ops = &priv->ops;
    device_lc_bind(pdev);
    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_hal;
    }
    SYS_LOGI(k_tag, "probe OK: %s", device_get_name(pdev));
    return VFS_OK;
err_hal:
    COMPAT_IGNORE_RESULT(hal_rtc_deinit(&priv->rtc));
err:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_pool_ctrl, idx));
    return ret;
}

/**
 * @brief RTC 设备移除: remove_start → 排空 IO → hal_rtc_deinit → 释放私有池
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_rtc_remove(struct device* pdev)
{
    struct vfs_rtc_priv* priv;
    struct dev_lifecycle* lc;
    int idx;

    if (!pdev)
        return VFS_ERR_INVAL;
    priv = (struct vfs_rtc_priv*)device_get_priv(pdev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    COMPAT_IGNORE_RESULT(hal_rtc_deinit(&priv->rtc));
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(vfs_rtc, "rtc", vfs_rtc_probe, vfs_rtc_remove)
