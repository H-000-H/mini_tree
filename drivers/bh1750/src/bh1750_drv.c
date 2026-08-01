/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file bh1750_drv.c
 * @brief BH1750 光照传感器驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_bh1750_pool[BH1750_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令见 bh1750_drv.h。
 *
 * 数据流: VFS ioctl → bh1750_cmd_lux → device_read/write(I2C) → HAL
 */
#include "bh1750_drv.h"
#include "vfs-i2c.h"
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

#ifndef DTC_GEN_COUNT_ROHM_BH1750
#define DTC_GEN_COUNT_ROHM_BH1750  1
#endif
#define BH1750_POOL_COUNT  DTC_GEN_COUNT_ROHM_BH1750

/** @brief BH1750 驱动实例（嵌入 fops） */
struct bh1750_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device*         i2c_dev;  /**< 所属 I2C client 设备 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct bh1750_device s_bh1750_pool[BH1750_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_bh1750_used[BH1750_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_bh1750_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "bh1750";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void bh1750_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_bh1750_pool_ctrl, s_bh1750_used, BH1750_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct bh1750_device* bh1750_get_drvdata(struct device* dev)
{
    return (struct bh1750_device*)device_get_priv(dev);
}


/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int bh1750_i2c_wr(struct bh1750_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int bh1750_i2c_rd(struct bh1750_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int bh1750_hw_create(struct bh1750_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->i2c_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void bh1750_hw_destroy(struct bh1750_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev) COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;

}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int bh1750_open(struct device* dev, void* arg)
{
    struct bh1750_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bh1750_get_drvdata(dev);
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
        ret = bh1750_hw_create(d);
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
static int bh1750_close(struct device* dev)
{
    struct bh1750_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bh1750_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        bh1750_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*bh1750_ioctl_fn_t)(struct bh1750_device* d, void* arg, size_t arg_len, uint32_t ms);
struct bh1750_ioctl_map { bh1750_ioctl_fn_t handler; };


/**
 * @brief BH1750_CMD_READ_LUX 实现：上电 + 连续 H 模式（120ms）读 lux
 */
static int bh1750_cmd_lux(struct bh1750_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t on=0x01, cont=0x10; uint8_t raw[2]; int* lux=(int*)arg;
    if(!d->hw_ready||!lux||len!=sizeof(int)) return VFS_ERR_INVAL;
    if(bh1750_i2c_wr(d, &on, 1, to)!=VFS_OK||bh1750_i2c_wr(d, &cont, 1, to)!=VFS_OK) return VFS_ERR_IO;
    osal_delay_ms(120);
    if(bh1750_i2c_rd(d, raw, 2, to)!=VFS_OK) return VFS_ERR_IO;
    *lux=(int)(((uint16_t)((raw[0]<<8)|raw[1])*12)/10); return VFS_OK;
}
static const struct bh1750_ioctl_map s_bh1750_map[BH1750_CMD_COUNT] = {
    [BH1750_CMD_READ_LUX - BH1750_CMD_BASE - 1] = { bh1750_cmd_lux },
};


/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int bh1750_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct bh1750_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bh1750_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)BH1750_CMD_BASE;
    if (off < 1 || off > BH1750_CMD_COUNT || !s_bh1750_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_bh1750_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations bh1750_fops =
{
    .open  = bh1750_open,
    .close = bh1750_close,
    .ioctl = bh1750_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int bh1750_probe(struct device* dev)
{
    struct bh1750_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_bh1750_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_bh1750_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = bh1750_fops;
    dev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bh1750_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int bh1750_remove(struct device* dev)
{
    struct bh1750_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = bh1750_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_bh1750_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    bh1750_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bh1750_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(bh1750, "rohm,bh1750", bh1750_probe, bh1750_remove)
