/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file sg90_drv.c
 * @brief SG90 舵机驱动实现 — 挂在 TIM（PWM）下的 VFS 设备驱动
 *
 * 静态池: s_sg90_pool[SG90_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令见 sg90_drv.h。
 */
#include "sg90_drv.h"
#include "vfs-tim.h"
#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "status.h"
#include "dt_config_gen.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_TOWERPRO_SG90
#define DTC_GEN_COUNT_TOWERPRO_SG90  1
#endif
#define SG90_POOL_COUNT  DTC_GEN_COUNT_TOWERPRO_SG90

/** @brief SG90 驱动实例（嵌入 fops 与 PWM 参数） */
struct sg90_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device* tim_dev;          /**< PWM TIM 设备 */
    struct vfs_tim_arg tim;          /**< PWM 参数（快路径） */
    uint32_t ch;                     /**< PWM 通道 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct sg90_device s_sg90_pool[SG90_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_sg90_used[SG90_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_sg90_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "sg90";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void sg90_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sg90_pool_ctrl, s_sg90_used, SG90_POOL_COUNT));
}

static struct sg90_device* sg90_get_drvdata(struct device* dev)
{
    return (struct sg90_device*)device_get_priv(dev);
}



/**
 * @brief 首次 open 时打开 TIM 并下发初始 PWM
 * @return VFS_OK 或 VFS_ERR_*
 */
static int sg90_hw_create(struct sg90_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->tim_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->tim_dev, TIM_CMD_PWM_UPDATE, &d->tim, sizeof(d->tim), 0);
      if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

/**
 * @brief 释放硬件资源（关闭 TIM 设备）
 */
static void sg90_hw_destroy(struct sg90_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->tim_dev)
        COMPAT_IGNORE_RESULT(device_close(d->tim_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int sg90_open(struct device* dev, void* arg)
{
    struct sg90_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sg90_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = sg90_hw_create(d);
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
static int sg90_close(struct device* dev)
{
    struct sg90_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sg90_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sg90_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*sg90_ioctl_fn_t)(struct sg90_device* d, void* arg, size_t arg_len, uint32_t ms);
struct sg90_ioctl_map { sg90_ioctl_fn_t handler; };


/**
 * @brief SG90_CMD_SET_ANGLE 实现：角度映射占空比并经 TIM 快路径下发
 */
static int sg90_cmd_angle(struct sg90_device* d, void* arg, size_t len, uint32_t ms)
{
    int deg;
    COMPAT_IGNORE_RESULT(ms);
    if(!d->hw_ready||!arg||len!=sizeof(int)) return VFS_ERR_INVAL;
    deg=*(int*)arg; if(deg<0)deg=0; if(deg>180)deg=180;
    d->tim.channel=d->ch; d->tim.arr=20000U; d->tim.ccr=1000U+(uint32_t)deg*1000U/180U;
    return device_ioctl(d->tim_dev, TIM_CMD_PWM_UPDATE, &d->tim, sizeof(d->tim), 100);
}
static const struct sg90_ioctl_map s_sg90_map[SG90_CMD_COUNT] = {
    [SG90_CMD_SET_ANGLE - SG90_CMD_BASE - 1] = { sg90_cmd_angle },
};


/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int sg90_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sg90_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sg90_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SG90_CMD_BASE;
    if (off < 1 || off > SG90_CMD_COUNT || !s_sg90_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_sg90_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sg90_fops =
{
    .open  = sg90_open,
    .close = sg90_close,
    .ioctl = sg90_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 TIM 设备并挂 fops
 */
static int sg90_probe(struct device* dev)
{
    struct sg90_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sg90_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_sg90_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->tim_dev = device_get_phandle_dev(dev, "pwm");
    if (IS_ERR(d->tim_dev)) { ret = PTR_ERR(d->tim_dev); goto err; }
    d->ch = 1U;
    {
        int ch = 1;
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "channel", &ch));
        if (ch >= 1)
            d->ch = (uint32_t)ch;
    }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = sg90_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sg90_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int sg90_remove(struct device* dev)
{
    struct sg90_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = sg90_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_sg90_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    sg90_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sg90_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(sg90, "towerpro,sg90", sg90_probe, sg90_remove)
