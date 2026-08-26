/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ft5x06_drv.c
 *@brief FT5x06 电容触摸驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_ft5x06_pool[FT5X06_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 ft5x06_drv.h。
 *   数据流: VFS ioctl → ft5x06_cmd_touch → device_read/write(I2C) → HAL
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

static struct ft5x06_device s_ft5x06_pool[FT5X06_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t s_ft5x06_used[FT5X06_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_ft5x06_pool_ctrl MINI_ALIGNED(4);
static const char* const k_tag = "ft5x06";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void ft5x06_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_ft5x06_pool_ctrl, s_ft5x06_used, FT5X06_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ft5x06_device* ft5x06_get_drvdata(struct device* pdev)
{
    return (struct ft5x06_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ft5x06_i2c_wr(struct ft5x06_device* dev, const uint8_t* tx, size_t len,
                         uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return MINI_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}

/**
 * @brief 从 I2C 总线读数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ft5x06_i2c_rd(struct ft5x06_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ft5x06_hw_create(struct ft5x06_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = device_open(dev->i2c_dev, NULL);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ft5x06_hw_destroy(struct ft5x06_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->i2c_dev)
        MINI_IGNORE_RESULT(device_close(dev->i2c_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int ft5x06_open(struct device* pdev, void* arg)
{
    struct ft5x06_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ft5x06_get_drvdata(pdev);
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
        ret = ft5x06_hw_create(dev);
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
static int ft5x06_close(struct device* pdev)
{
    struct ft5x06_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ft5x06_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ft5x06_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ft5x06_ioctl_fn_t)(struct ft5x06_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct ft5x06_ioctl_map
{
    ft5x06_ioctl_fn_t handler;
};

/**
 * @brief FT5X06_CMD_READ_TOUCH 实现：自 0x02 起读 6B 解析触点数量与坐标
 */
static int ft5x06_cmd_touch(struct ft5x06_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const uint8_t reg = 0x02;
    uint8_t raw[6];
    struct ft5x06_touch* touch = (struct ft5x06_touch*)arg;
    if (!dev->hw_ready || !touch || len != sizeof(*touch))
        return MINI_ERR_INVAL;
    if (ft5x06_i2c_wr(dev, &reg, 1, timeout_ms) != MINI_OK)
        return MINI_ERR_IO;
    if (ft5x06_i2c_rd(dev, raw, 6, timeout_ms) != MINI_OK)
        return MINI_ERR_IO;
    touch->points = (raw[0] & 0x0FU);
    touch->pos_x = (uint16_t)(((raw[1] & 0x0FU) << 8) | raw[2]);
    touch->pos_y = (uint16_t)(((raw[3] & 0x0FU) << 8) | raw[4]);
    return MINI_OK;
}
static const struct ft5x06_ioctl_map s_ft5x06_map[FT5X06_CMD_COUNT] = {
    [FT5X06_CMD_READ_TOUCH - FT5X06_CMD_BASE - 1] = {ft5x06_cmd_touch},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ft5x06_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ft5x06_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ft5x06_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)FT5X06_CMD_BASE;
    if (off < 1 || off > FT5X06_CMD_COUNT || !s_ft5x06_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_ft5x06_map[off - 1].handler(dev, arg, arg_len, ms);
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
    struct ft5x06_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ft5x06_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_ft5x06_pool[pool_idx];
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    dev->i2c_dev = device_get_parent(pdev);
    if (!dev->i2c_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = ft5x06_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ft5x06_remove(struct device* pdev)
{
    struct ft5x06_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = ft5x06_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_ft5x06_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    ft5x06_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(ft5x06, "focaltech,ft5x06", ft5x06_probe, ft5x06_remove)
