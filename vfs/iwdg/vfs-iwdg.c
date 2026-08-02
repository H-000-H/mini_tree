/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file        vfs-iwdg.c
 * @brief       IWDG VFS 实现 — open 启动看门狗, ioctl 喂狗/超时; close 不关硬件
 */
#define IWDG_VFS_IMPL
#include "vfs-iwdg.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

struct vfs_iwdg_priv
{
    struct file_operations ops; /**< VFS 操作表 */
    struct hal_iwdg_dev iwdg; /**< HAL IWDG 设备 */
    int pool_idx; /**< 池索引 */
};
static struct vfs_iwdg_priv s_priv;
static uint8_t s_used;
static osal_pool_t s_pool;
static const char* k_tag = "vfs_iwdg";

/**
 * @brief IWDG VFS 私有数据池启动初始化
 */
pre_execution(160) static void boot(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_pool, &s_used, 1));
}

/**
 * @brief IWDG 打开: 引用计数, 首次打开时调用 hal_iwdg_start 启动独立看门狗
 * @param pdev 设备对象指针
 * @param arg 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_iwdg_open(struct device* pdev, void* arg)
{
    struct vfs_iwdg_priv* priv;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_iwdg_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = hal_iwdg_start(&priv->iwdg);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief IWDG 关闭: 仅递减 lifecycle 引用, 不停止硬件 (IWDG 启动后不可关闭)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_iwdg_close(struct device* pdev)
{
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    /* IWDG 一旦启动不能真正关闭, 仅释放 lifecycle */
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief IWDG ioctl: 喂狗, 设置/恢复超时
 * @param pdev 设备对象指针
 * @param cmd 控制命令 (IWDG_CMD_*)
 * @param arg 命令参数指针 (IWDG_CMD_SET_TIMEOUT 时为 iwdg_timeout_arg)
 * @param arg_len 参数长度
 * @param to 未使用
 * @return 成功返回 VFS_OK, 未知命令返回 VFS_ERR_INVAL, 失败返回负数错误码
 */
static int vfs_iwdg_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t to)
{
    struct vfs_iwdg_priv* priv;
    struct dev_lifecycle* lc;
    int ret;
    COMPAT_IGNORE_RESULT(to);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_iwdg_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    switch (cmd)
    {
    case IWDG_CMD_FEED:
        ret = hal_iwdg_feed(&priv->iwdg);
        break;
    case IWDG_CMD_SET_TIMEOUT:
    {
        const struct iwdg_timeout_arg* a = arg;
        ret = (!a || arg_len != sizeof(*a)) ? VFS_ERR_INVAL :
                                              hal_iwdg_set_timeout_ms(&priv->iwdg, a->timeout_ms);
        break;
    }
    case IWDG_CMD_SET_LONG:
        ret = hal_iwdg_set_long_timeout(&priv->iwdg);
        break;
    case IWDG_CMD_RESTORE:
        ret = hal_iwdg_restore_timeout(&priv->iwdg);
        break;
    default:
        ret = VFS_ERR_INVAL;
        break;
    }
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations s_fops = {
    .open = vfs_iwdg_open,
    .close = vfs_iwdg_close,
    .ioctl = vfs_iwdg_ioctl,
};

/**
 * @brief IWDG 设备探测: 解析 timeout-ms, hal_iwdg_init, 注册 fops
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_iwdg_probe(struct device* pdev)
{
    struct hal_iwdg_config cfg = {.timeout_ms = 8000, .prer = 0xFFFFFFFFU, .rlr = 0xFFFFFFFFU};
    int v, idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    idx = osal_pool_claim(&s_pool);
    if (idx < 0)
        return VFS_ERR_NOMEM;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "timeout-ms", &v));
    if (v > 0)
        cfg.timeout_ms = (uint32_t)v;
    COMPAT_MEM_SET(&s_priv, 0, sizeof(s_priv));
    s_priv.pool_idx = idx;
    ret = hal_iwdg_init(&s_priv.iwdg, &cfg);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_pool, idx));
        return ret;
    }
    s_priv.ops = s_fops;
    pdev->ops = &s_priv.ops;
    device_lc_bind(pdev);
    if (device_set_priv(pdev, &s_priv) != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_pool, idx));
        return VFS_ERR_IO;
    }
    SYS_LOGI(k_tag, "probe OK");
    return VFS_OK;
}

/**
 * @brief IWDG 设备移除: 注销 fops 并释放私有池 (不停止硬件, 不做 lifecycle drain)
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK
 */
static int vfs_iwdg_remove(struct device* pdev)
{
    COMPAT_IGNORE_RESULT(pdev);
    device_ops_unregister(pdev);
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_pool, 0));
    return VFS_OK;
}

DRIVER_REGISTER(vfs_iwdg, "iwdg", vfs_iwdg_probe, vfs_iwdg_remove)
