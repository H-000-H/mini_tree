/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file ft5x06_drv.c
 * @brief FT5x06 电容触摸驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_ft5x06_pool[FT5X06_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与采样结构见 ft5x06_drv.h。
 *
 * 数据流: VFS ioctl → ft5x06_cmd_touch → device_read/write(I2C) → HAL
 */
#include "ft5x06_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-i2c.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_FOCALTECH_FT5X06
#define DTC_GEN_COUNT_FOCALTECH_FT5X06 1
#endif
#define FT5X06_POOL_COUNT DTC_GEN_COUNT_FOCALTECH_FT5X06

/** @brief FT5x06 驱动实例（嵌入 fops） */
struct ft5x06_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ft5x06_device s_ft5x06_pool[FT5X06_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_ft5x06_used[FT5X06_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_ft5x06_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "ft5x06";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void ft5x06_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ft5x06_pool_ctrl, s_ft5x06_used, FT5X06_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ft5x06_device* ft5x06_get_drvdata(struct device* pdev)
{
    return (struct ft5x06_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ft5x06_i2c_wr(struct ft5x06_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ft5x06_i2c_rd(struct ft5x06_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ft5x06_hw_create(struct ft5x06_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    {
        int r = device_open(d->i2c_dev, NULL);
        if (r != VFS_OK)
            return r;
    }
    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ft5x06_hw_destroy(struct ft5x06_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int ft5x06_open(struct device* pdev, void* arg)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(pdev);
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
        ret = ft5x06_hw_create(d);
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
static int ft5x06_close(struct device* pdev)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ft5x06_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ft5x06_ioctl_fn_t)(struct ft5x06_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ft5x06_ioctl_map
{
    ft5x06_ioctl_fn_t handler;
};

/**
 * @brief FT5X06_CMD_READ_TOUCH 实现：自 0x02 起读 6B 解析触点数量与坐标
 */
static int ft5x06_cmd_touch(struct ft5x06_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t reg = 0x02;
    uint8_t raw[6];
    struct ft5x06_touch* t = (struct ft5x06_touch*)arg;
    if (!d->hw_ready || !t || len != sizeof(*t))
        return VFS_ERR_INVAL;
    if (ft5x06_i2c_wr(d, &reg, 1, to) != VFS_OK)
        return VFS_ERR_IO;
    if (ft5x06_i2c_rd(d, raw, 6, to) != VFS_OK)
        return VFS_ERR_IO;
    t->points = (raw[0] & 0x0FU);
    t->x = (uint16_t)(((raw[1] & 0x0FU) << 8) | raw[2]);
    t->y = (uint16_t)(((raw[3] & 0x0FU) << 8) | raw[4]);
    return VFS_OK;
}
static const struct ft5x06_ioctl_map s_ft5x06_map[FT5X06_CMD_COUNT] = {
    [FT5X06_CMD_READ_TOUCH - FT5X06_CMD_BASE - 1] = {ft5x06_cmd_touch},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ft5x06_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)FT5X06_CMD_BASE;
    if (off < 1 || off > FT5X06_CMD_COUNT || !s_ft5x06_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ft5x06_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ft5x06_fops = {
    .open = ft5x06_open,
    .close = ft5x06_close,
    .ioctl = ft5x06_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int ft5x06_probe(struct device* pdev)
{
    struct ft5x06_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ft5x06_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ft5x06_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(pdev);
    if (!d->i2c_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = ft5x06_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ft5x06_remove(struct device* pdev)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ft5x06_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ft5x06_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ft5x06, "focaltech,ft5x06", ft5x06_probe, ft5x06_remove)
