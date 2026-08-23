/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sn65hvd230_drv.c
 *@brief SN65HVD230 CAN 收发器驱动实现 — 挂在 GPIO 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_sn65hvd230_pool[SN65HVD230_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令见 sn65hvd230_drv.h。
 */

#include "sn65hvd230_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_TI_SN65HVD230
#define DTC_GEN_COUNT_TI_SN65HVD230 1
#endif
#define SN65HVD230_POOL_COUNT DTC_GEN_COUNT_TI_SN65HVD230

/** @brief SN65HVD230 驱动实例（嵌入 fops 与 GPIO 句柄） */
struct sn65hvd230_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* gdev; /**< 待机控制 GPIO 设备 */
    struct vfs_gpio_arg gpio; /**< GPIO 操作参数 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct sn65hvd230_device s_sn65hvd230_pool[SN65HVD230_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_sn65hvd230_used[SN65HVD230_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_sn65hvd230_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "sn65hvd230";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void sn65hvd230_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_sn65hvd230_pool_ctrl, s_sn65hvd230_used, SN65HVD230_POOL_COUNT)); }

/**
 * @brief 从 device 获取驱动私有数据 (drvdata)
 * @param[in] pdev device 指针
 * @return sn65hvd230_device 私有数据指针
 */
static struct sn65hvd230_device* sn65hvd230_get_drvdata(struct device* pdev) { return (struct sn65hvd230_device*)device_get_priv(pdev); }

/**
 * @brief 打开 GPIO 设备并绑定参数（失败回滚关闭）
 */
static int sn65hvd230_gpio_on(struct sn65hvd230_device* dev, struct device* g, struct vfs_gpio_arg* a)
{
    int ret = device_open(g, NULL);
    if (ret != MINI_OK)
        return ret;
    ret = device_ioctl(g, GPIO_CMD_GET_LEVEL, a, sizeof(*a), 0);
    if (ret != MINI_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(g));
        return ret;
    }
    return MINI_OK;
}

/**
 * @brief 首次 open 时打开 GPIO 并绑定参数
 * @return MINI_OK 或 VFS_ERR_*
 */
static int sn65hvd230_hw_create(struct sn65hvd230_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = sn65hvd230_gpio_on(dev, dev->gdev, &dev->gpio);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 GPIO 设备）
 */
static void sn65hvd230_hw_destroy(struct sn65hvd230_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->gdev)
        COMPAT_IGNORE_RESULT(device_close(dev->gdev));
    dev->gpio.obj = NULL;
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int sn65hvd230_open(struct device* pdev, void* arg)
{
    struct sn65hvd230_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sn65hvd230_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = MINI_OK;
    if (first == 1)
    {
        ret = sn65hvd230_hw_create(dev);
        if (ret != MINI_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return MINI_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int sn65hvd230_close(struct device* pdev)
{
    struct sn65hvd230_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sn65hvd230_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sn65hvd230_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

typedef int (*sn65hvd230_ioctl_fn_t)(struct sn65hvd230_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct sn65hvd230_ioctl_map
{
    sn65hvd230_ioctl_fn_t handler;
};

/**
 * @brief SN65HVD230_CMD_SET_STANDBY 实现：待机/正常模式切换
 */
static int sn65hvd230_cmd(struct sn65hvd230_device* dev, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(ms);
    if (!dev->hw_ready || !arg || len != sizeof(int))
        return MINI_ERR_INVAL;
    dev->gpio.level = (*(int*)arg) ? 1 : 0;
    return vfs_gpio_set_level(&dev->gpio);
}

static const struct sn65hvd230_ioctl_map s_sn65hvd230_map[SN65HVD230_CMD_COUNT] = {
    [SN65HVD230_CMD_SET_STANDBY - SN65HVD230_CMD_BASE - 1] = {sn65hvd230_cmd},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int sn65hvd230_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sn65hvd230_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sn65hvd230_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SN65HVD230_CMD_BASE;
    if (off < 1 || off > SN65HVD230_CMD_COUNT || !s_sn65hvd230_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_sn65hvd230_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sn65hvd230_fops = {
    .open = sn65hvd230_open,
    .close = sn65hvd230_close,
    .ioctl = sn65hvd230_ioctl,
};

/**
 * @brief probe：claim 池项、绑定待机 GPIO 并挂 fops
 */
static int sn65hvd230_probe(struct device* pdev)
{
    struct sn65hvd230_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sn65hvd230_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_sn65hvd230_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->gdev = device_get_phandle_dev(pdev, "stb-gpio");
    if (IS_ERR(dev->gdev))
    {
        ret = (int)PTR_ERR(dev->gdev);
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = sn65hvd230_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sn65hvd230_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int sn65hvd230_remove(struct device* pdev)
{
    struct sn65hvd230_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = sn65hvd230_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_sn65hvd230_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    sn65hvd230_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sn65hvd230_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(sn65hvd230, "ti,sn65hvd230", sn65hvd230_probe, sn65hvd230_remove)
