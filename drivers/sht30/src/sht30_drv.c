/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sht30_drv.c
 *@brief SHT30 温湿度传感器驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_sht30_pool[SHT30_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 sht30_drv.h。
 *   数据流: VFS ioctl → sht30_cmd_read → device_read/write(I2C) → HAL
 */

#include "sht30_drv.h"

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

#ifndef DTC_GEN_COUNT_SENSIRION_SHT30
#define DTC_GEN_COUNT_SENSIRION_SHT30 1
#endif
#define SHT30_POOL_COUNT DTC_GEN_COUNT_SENSIRION_SHT30

/** @brief SHT30 驱动实例（嵌入 fops） */
struct sht30_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct sht30_device s_sht30_pool[SHT30_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_sht30_used[SHT30_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_sht30_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "sht30";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void sht30_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_sht30_pool_ctrl, s_sht30_used, SHT30_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct sht30_device* sht30_get_drvdata(struct device* pdev) { return (struct sht30_device*)device_get_priv(pdev); }

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int sht30_i2c_wr(struct sht30_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}

/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int sht30_i2c_rd(struct sht30_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int sht30_hw_create(struct sht30_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret = device_open(dev->i2c_dev, NULL);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void sht30_hw_destroy(struct sht30_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int sht30_open(struct device* pdev, void* arg)
{
    struct sht30_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = sht30_get_drvdata(pdev);
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
        ret = sht30_hw_create(dev);
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
static int sht30_close(struct device* pdev)
{
    struct sht30_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = sht30_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sht30_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*sht30_ioctl_fn_t)(struct sht30_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct sht30_ioctl_map
{
    sht30_ioctl_fn_t handler;
};

/**
 * @brief SHT30_CMD_READ_TEMP_RH 实现：触发测量（20ms）并换算 T/RH
 */
static int sht30_cmd_read(struct sht30_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const uint8_t cmd[2] = {0x24, 0x00};
    uint8_t raw[6];
    struct sht30_sample* o = (struct sht30_sample*)arg;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    ret = sht30_i2c_wr(dev, cmd, 2, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    osal_delay_ms(20);
    ret = sht30_i2c_rd(dev, raw, 6, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    {
        uint16_t raw_t = (uint16_t)((raw[0] << 8) | raw[1]);
        uint16_t raw_h = (uint16_t)((raw[3] << 8) | raw[4]);
        o->temp_c_x100 = (int16_t)((((int32_t)raw_t * 17500) / 65535) - 4500);
        o->rh_x100 = (uint16_t)(((uint32_t)raw_h * 10000U) / 65535U);
    }
    return VFS_OK;
}
static const struct sht30_ioctl_map s_sht30_map[SHT30_CMD_COUNT] = {
    [SHT30_CMD_READ_TEMP_RH - SHT30_CMD_BASE - 1] = {sht30_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int sht30_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sht30_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = sht30_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SHT30_CMD_BASE;
    if (off < 1 || off > SHT30_CMD_COUNT || !s_sht30_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_sht30_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sht30_fops = {
    .open = sht30_open,
    .close = sht30_close,
    .ioctl = sht30_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int sht30_probe(struct device* pdev)
{
    struct sht30_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sht30_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_sht30_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->i2c_dev = device_get_parent(pdev);
    if (!dev->i2c_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = sht30_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sht30_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int sht30_remove(struct device* pdev)
{
    struct sht30_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = sht30_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_sht30_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    sht30_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sht30_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(sht30, "sensirion,sht30", sht30_probe, sht30_remove)
