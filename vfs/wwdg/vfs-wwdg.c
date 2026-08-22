/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-wwdg.c
 *@brief WWDG VFS 实现 — open 启动窗口看门狗, ioctl 窗口内喂狗
 *@author H-000-H

 */

#define WWDG_VFS_IMPL
#include "vfs-wwdg.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

struct vfs_wwdg_priv
{
    struct file_operations ops; /**< VFS 操作表 */
    struct hal_wwdg_dev wwdg; /**< HAL WWDG 设备 */
};
static struct vfs_wwdg_priv s_priv;
static const char* k_tag = "vfs_wwdg";

/**
 * @brief WWDG 打开: 引用计数, 首次打开时调用 hal_wwdg_start 启动窗口看门狗
 * @param[in] pdev 设备对象指针
 * @param[in] arg 未使用
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_wwdg_open(struct device* pdev, void* arg)
{
    struct vfs_wwdg_priv* priv;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_wwdg_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = hal_wwdg_start(&priv->wwdg);
        if (ret != VFS_OK)
            dev_lc_open_abort(lc);
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief WWDG 关闭: 仅递减 lifecycle 引用, 不调用 hal 停止 (硬件可能仍在运行)
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_wwdg_close(struct device* pdev)
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
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief WWDG ioctl: 窗口内喂狗 (WWDG_CMD_FEED)
 * @param[in] pdev 设备对象指针
 * @param[in] cmd 控制命令
 * @param[in] arg 未使用
 * @param[in] arg_len 未使用
 * @param[in] to 未使用
 * @return 成功返回 VFS_OK, 未知命令返回 VFS_ERR_INVAL, 失败返回负数错误码
 */
static int vfs_wwdg_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t to)
{
    struct vfs_wwdg_priv* priv;
    struct dev_lifecycle* lc;
    int ret;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(arg_len);
    COMPAT_IGNORE_RESULT(to);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    priv = container_of(pdev->ops, struct vfs_wwdg_priv, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    ret = (cmd == WWDG_CMD_FEED) ? hal_wwdg_feed(&priv->wwdg) : VFS_ERR_INVAL;
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations s_fops = {
    .open = vfs_wwdg_open,
    .close = vfs_wwdg_close,
    .ioctl = vfs_wwdg_ioctl,
};

/**
 * @brief WWDG 设备探测: 解析 window/counter/prescaler, hal_wwdg_init, 注册 fops
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_wwdg_probe(struct device* pdev)
{
    struct hal_wwdg_config cfg = {.window = 0x50, .counter = 0x7F, .prescaler = 1, .ewi_enable = 0};
    int v, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "window", &v));
    if (v)
        cfg.window = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "counter", &v));
    if (v)
        cfg.counter = (uint32_t)v;
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "prescaler", &v));
    cfg.prescaler = (uint32_t)v;
    COMPAT_MEM_SET(&s_priv, 0, sizeof(s_priv));
    ret = hal_wwdg_init(&s_priv.wwdg, &cfg);
    if (ret != VFS_OK)
        return ret;
    s_priv.ops = s_fops;
    pdev->ops = &s_priv.ops;
    device_lc_bind(pdev);
    if (device_set_priv(pdev, &s_priv) != VFS_OK)
        return VFS_ERR_IO;
    SYS_LOGI(k_tag, "probe OK");
    return VFS_OK;
}

/**
 * @brief WWDG 设备移除: 注销 fops (不停止硬件, 不做 lifecycle drain)
 * @param[in] pdev 设备对象指针
 * @return 成功返回 VFS_OK
 */
static int vfs_wwdg_remove(struct device* pdev)
{
    device_ops_unregister(pdev);
    return VFS_OK;
}

DRIVER_REGISTER(vfs_wwdg, "wwdg", vfs_wwdg_probe, vfs_wwdg_remove)
