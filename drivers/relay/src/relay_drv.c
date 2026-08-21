/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file relay_drv.c
 *@brief 继电器驱动实现 — 挂在 GPIO 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_relay_pool[RELAY_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令见 relay_drv.h。
 */

#include "relay_drv.h"

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

#ifndef DTC_GEN_COUNT_GPIO_RELAY
#define DTC_GEN_COUNT_GPIO_RELAY 1
#endif
#define RELAY_POOL_COUNT DTC_GEN_COUNT_GPIO_RELAY

/** @brief 继电器驱动实例（嵌入 fops 与 GPIO 句柄） */
struct relay_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* gdev; /**< GPIO 设备（phandle: relay-gpio） */
    struct vfs_gpio_arg gpio; /**< GPIO 操作参数 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct relay_device s_relay_pool[RELAY_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_relay_used[RELAY_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_relay_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "relay";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void relay_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_relay_pool_ctrl, s_relay_used, RELAY_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct relay_device* relay_get_drvdata(struct device* pdev)
{
    return (struct relay_device*)device_get_priv(pdev);
}

/**
 * @brief 打开 GPIO 设备并绑定参数（失败回滚关闭）
 */
static int relay_gpio_on(struct relay_device* dev, struct device* g, struct vfs_gpio_arg* a)
{
    int ret = device_open(g, NULL);
    if (ret != VFS_OK)
        return ret;
    ret = device_ioctl(g, GPIO_CMD_GET_LEVEL, a, sizeof(*a), 0);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(g));
        return ret;
    }
    return VFS_OK;
}

/**
 * @brief 首次 open 时打开 GPIO 并绑定参数
 * @return VFS_OK 或 VFS_ERR_*
 */
static int relay_hw_create(struct relay_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret = relay_gpio_on(dev, dev->gdev, &dev->gpio);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 GPIO 设备）
 */
static void relay_hw_destroy(struct relay_device* dev)
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
static int relay_open(struct device* pdev, void* arg)
{
    struct relay_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = relay_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = relay_hw_create(dev);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

/**
 * @brief fops.close：引用计数关闭，末次调用释放硬件
 */
static int relay_close(struct device* pdev)
{
    struct relay_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = relay_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        relay_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*relay_ioctl_fn_t)(struct relay_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct relay_ioctl_map
{
    relay_ioctl_fn_t handler;
};

/**
 * @brief RELAY_CMD_SET 实现：GPIO 电平控制吸合/断开
 */
static int relay_cmd(struct relay_device* dev, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(ms);
    if (!dev->hw_ready || !arg || len != sizeof(int))
        return VFS_ERR_INVAL;
    dev->gpio.level = (*(int*)arg) ? 1 : 0;
    return vfs_gpio_set_level(&dev->gpio);
}

static const struct relay_ioctl_map s_relay_map[RELAY_CMD_COUNT] = {
    [RELAY_CMD_SET - RELAY_CMD_BASE - 1] = {relay_cmd},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int relay_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct relay_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = relay_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RELAY_CMD_BASE;
    if (off < 1 || off > RELAY_CMD_COUNT || !s_relay_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_relay_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations relay_fops = {
    .open = relay_open,
    .close = relay_close,
    .ioctl = relay_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 relay-gpio 并挂 fops
 */
static int relay_probe(struct device* pdev)
{
    struct relay_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_relay_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_relay_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->gdev = device_get_phandle_dev(pdev, "ctl-gpio");
    if (IS_ERR(dev->gdev))
    {
        ret = (int)PTR_ERR(dev->gdev);
        goto err;
    }

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = relay_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_relay_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int relay_remove(struct device* pdev)
{
    struct relay_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = relay_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_relay_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    relay_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_relay_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(relay, "gpio-relay", relay_probe, relay_remove)
