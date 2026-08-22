/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file mpu6050_drv.c
 *@brief MPU6050 六轴 IMU 驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_mpu6050_pool[MPU6050_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 mpu6050_drv.h。
 *   数据流: VFS ioctl → mpu6050_cmd_read → device_read/write(I2C) → HAL
 */

#include "mpu6050_drv.h"

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

#ifndef DTC_GEN_COUNT_INVENSENSE_MPU6050
#define DTC_GEN_COUNT_INVENSENSE_MPU6050 1
#endif
#define MPU6050_POOL_COUNT DTC_GEN_COUNT_INVENSENSE_MPU6050

/** @brief MPU6050 驱动实例（嵌入 fops） */
struct mpu6050_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct mpu6050_device s_mpu6050_pool[MPU6050_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_mpu6050_used[MPU6050_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_mpu6050_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "mpu6050";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void mpu6050_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_mpu6050_pool_ctrl, s_mpu6050_used, MPU6050_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct mpu6050_device* mpu6050_get_drvdata(struct device* pdev) { return (struct mpu6050_device*)device_get_priv(pdev); }

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int mpu6050_i2c_wr(struct mpu6050_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}

/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int mpu6050_i2c_rd(struct mpu6050_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int mpu6050_hw_create(struct mpu6050_device* dev)
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
static void mpu6050_hw_destroy(struct mpu6050_device* dev)
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
static int mpu6050_open(struct device* pdev, void* arg)
{
    struct mpu6050_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = mpu6050_get_drvdata(pdev);
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
        ret = mpu6050_hw_create(dev);
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
static int mpu6050_close(struct device* pdev)
{
    struct mpu6050_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = mpu6050_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        mpu6050_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*mpu6050_ioctl_fn_t)(struct mpu6050_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct mpu6050_ioctl_map
{
    mpu6050_ioctl_fn_t handler;
};

/**
 * @brief MPU6050_CMD_READ_ACCEL_GYRO 实现：自 0x3B 起连续读 14B 六轴原始值
 */
static int mpu6050_cmd_read(struct mpu6050_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const uint8_t reg = 0x3B;
    uint8_t raw[14];
    struct mpu6050_sample* o = (struct mpu6050_sample*)arg;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    ret = mpu6050_i2c_wr(dev, &reg, 1, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = mpu6050_i2c_rd(dev, raw, 14, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    o->ax = (int16_t)((raw[0] << 8) | raw[1]);
    o->ay = (int16_t)((raw[2] << 8) | raw[3]);
    o->az = (int16_t)((raw[4] << 8) | raw[5]);
    o->gx = (int16_t)((raw[8] << 8) | raw[9]);
    o->gy = (int16_t)((raw[10] << 8) | raw[11]);
    o->gz = (int16_t)((raw[12] << 8) | raw[13]);
    return VFS_OK;
}
static const struct mpu6050_ioctl_map s_mpu6050_map[MPU6050_CMD_COUNT] = {
    [MPU6050_CMD_READ_ACCEL_GYRO - MPU6050_CMD_BASE - 1] = {mpu6050_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int mpu6050_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct mpu6050_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = mpu6050_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)MPU6050_CMD_BASE;
    if (off < 1 || off > MPU6050_CMD_COUNT || !s_mpu6050_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_mpu6050_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations mpu6050_fops = {
    .open = mpu6050_open,
    .close = mpu6050_close,
    .ioctl = mpu6050_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int mpu6050_probe(struct device* pdev)
{
    struct mpu6050_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_mpu6050_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_mpu6050_pool[pool_idx];
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
    dev->ops = mpu6050_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_mpu6050_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int mpu6050_remove(struct device* pdev)
{
    struct mpu6050_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = mpu6050_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_mpu6050_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    mpu6050_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_mpu6050_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(mpu6050, "invensense,mpu6050", mpu6050_probe, mpu6050_remove)
