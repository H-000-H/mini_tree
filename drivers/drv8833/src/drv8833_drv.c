/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file drv8833_drv.c
 * @brief DRV8833 双路电机驱动实现 — 挂在 GPIO（AIN1/2、BIN1/2）下的 VFS 设备驱动
 *
 * 静态池: s_drv8833_pool[DRV8833_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 drv8833_drv.h。
 */
#include "drv8833_drv.h"

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

#ifndef DTC_GEN_COUNT_TI_DRV8833
#define DTC_GEN_COUNT_TI_DRV8833 1
#endif
#define DRV8833_POOL_COUNT DTC_GEN_COUNT_TI_DRV8833

/** @brief DRV8833 驱动实例（嵌入 fops 与两路 H 桥输入引脚） */
struct drv8833_device
{
    struct file_operations ops;
    struct device* ain1_dev;
    struct device* ain2_dev; /**< A 路输入引脚 GPIO 设备 */
    struct device* bin1_dev;
    struct device* bin2_dev; /**< B 路输入引脚 GPIO 设备 */
    struct vfs_gpio_arg ain1;
    struct vfs_gpio_arg ain2; /**< A 路输入引脚参数 */
    struct vfs_gpio_arg bin1;
    struct vfs_gpio_arg bin2; /**< B 路输入引脚参数 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct drv8833_device s_drv8833_pool[DRV8833_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_drv8833_used[DRV8833_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_drv8833_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "drv8833";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160) static void drv8833_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_drv8833_pool_ctrl, s_drv8833_used, DRV8833_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct drv8833_device* drv8833_get_drvdata(struct device* pdev)
{
    return (struct drv8833_device*)device_get_priv(pdev);
}

/**
 * @brief 首次 open 时打开四路输入 GPIO 并绑定参数
 * @return VFS_OK 或 VFS_ERR_*
 */
static int drv8833_hw_create(struct drv8833_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    {
        struct device* g[4] = {d->ain1_dev, d->ain2_dev, d->bin1_dev, d->bin2_dev};
        struct vfs_gpio_arg* a[4] = {&d->ain1, &d->ain2, &d->bin1, &d->bin2};
        int i, r;
        for (i = 0; i < 4; i++)
        {
            r = device_open(g[i], NULL);
            if (r != VFS_OK)
                return r;
            r = device_ioctl(g[i], GPIO_CMD_GET_LEVEL, a[i], sizeof(*a[i]), 0);
            if (r != VFS_OK)
                return r;
        }
    }
    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭全部 GPIO 设备）
 */
static void drv8833_hw_destroy(struct drv8833_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->ain1_dev)
        COMPAT_IGNORE_RESULT(device_close(d->ain1_dev));
    if (d->ain2_dev)
        COMPAT_IGNORE_RESULT(device_close(d->ain2_dev));
    if (d->bin1_dev)
        COMPAT_IGNORE_RESULT(device_close(d->bin1_dev));
    if (d->bin2_dev)
        COMPAT_IGNORE_RESULT(device_close(d->bin2_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int drv8833_open(struct device* pdev, void* arg)
{
    struct drv8833_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = drv8833_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = drv8833_hw_create(d);
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
static int drv8833_close(struct device* pdev)
{
    struct drv8833_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = drv8833_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        drv8833_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*drv8833_ioctl_fn_t)(struct drv8833_device* d, void* arg, size_t arg_len, uint32_t ms);
struct drv8833_ioctl_map
{
    drv8833_ioctl_fn_t handler;
};

/**
 * @brief 设置单引脚电平（写 GPIO）
 */
static void drv8833_apply(struct vfs_gpio_arg* a, int v)
{
    a->level = v ? 1 : 0;
    COMPAT_IGNORE_RESULT(vfs_gpio_set_level(a));
}
/**
 * @brief DRV8833_CMD_SET_MOTOR 实现：按方向差分驱动两路输入
 */
static int drv8833_cmd_motor(struct drv8833_device* d, void* arg, size_t len, uint32_t ms)
{
    struct drv8833_motor* m = (struct drv8833_motor*)arg;
    COMPAT_IGNORE_RESULT(ms);
    if (!d->hw_ready || !m || len != sizeof(*m))
        return VFS_ERR_INVAL;
    if (m->motor == 0)
    {
        drv8833_apply(&d->ain1, m->fwd);
        drv8833_apply(&d->ain2, !m->fwd);
    }
    else
    {
        drv8833_apply(&d->bin1, m->fwd);
        drv8833_apply(&d->bin2, !m->fwd);
    }
    return VFS_OK;
}
static const struct drv8833_ioctl_map s_drv8833_map[DRV8833_CMD_COUNT] = {
    [DRV8833_CMD_SET_MOTOR - DRV8833_CMD_BASE - 1] = {drv8833_cmd_motor},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int drv8833_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct drv8833_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = drv8833_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DRV8833_CMD_BASE;
    if (off < 1 || off > DRV8833_CMD_COUNT || !s_drv8833_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_drv8833_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations drv8833_fops = {
    .open = drv8833_open,
    .close = drv8833_close,
    .ioctl = drv8833_ioctl,
};

/**
 * @brief probe：claim 池项、绑定四路输入 GPIO 并挂 fops
 */
static int drv8833_probe(struct device* pdev)
{
    struct drv8833_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_drv8833_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_drv8833_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->ain1_dev = device_get_phandle_dev(pdev, "ain1-gpio");
    d->ain2_dev = device_get_phandle_dev(pdev, "ain2-gpio");
    d->bin1_dev = device_get_phandle_dev(pdev, "bin1-gpio");
    d->bin2_dev = device_get_phandle_dev(pdev, "bin2-gpio");
    if (IS_ERR(d->ain1_dev) || IS_ERR(d->ain2_dev) || IS_ERR(d->bin1_dev) || IS_ERR(d->bin2_dev))
    {
        ret = VFS_ERR_INVAL;
        goto err;
    }

    if (device_set_priv(pdev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = drv8833_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_drv8833_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int drv8833_remove(struct device* pdev)
{
    struct drv8833_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = drv8833_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_drv8833_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    drv8833_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_drv8833_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(drv8833, "ti,drv8833", drv8833_probe, drv8833_remove)
