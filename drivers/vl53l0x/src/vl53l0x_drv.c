/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vl53l0x_drv.c
 *@brief VL53L0X 激光测距传感器驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_vl53l0x_pool[VL53L0X_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 vl53l0x_drv.h。
 *   数据流: VFS ioctl → vl53l0x_cmd_read → device_read/write(I2C) → HAL
 *   注: 采用 Pololu/ST 精简 dataInit 片段（非完整 ST API 校准）
 */

#include "vl53l0x_drv.h"

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

#ifndef DTC_GEN_COUNT_ST_VL53L0X
#define DTC_GEN_COUNT_ST_VL53L0X 1
#endif
#define VL53L0X_POOL_COUNT DTC_GEN_COUNT_ST_VL53L0X

/** @brief VL53L0X 驱动实例（嵌入 fops 与测距状态） */
struct vl53l0x_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */
    uint8_t stop_variable; /**< dataInit 阶段保存的 stop_variable */
    int hw_ready; /**< 硬件已初始化标志 */
};

static struct vl53l0x_device s_vl53l0x_pool[VL53L0X_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_vl53l0x_used[VL53L0X_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_vl53l0x_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "vl53l0x";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void vl53l0x_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_vl53l0x_pool_ctrl, s_vl53l0x_used, VL53L0X_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct vl53l0x_device* vl53l0x_get_drvdata(struct device* pdev)
{
    return (struct vl53l0x_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int vl53l0x_i2c_wr(struct vl53l0x_device* dev, const uint8_t* tx, size_t len,
                          uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int vl53l0x_i2c_rd(struct vl53l0x_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 写 1B 寄存器（reg + val 一次传输）
 */
static int vl53l0x_wr8(struct vl53l0x_device* dev, uint8_t reg, uint8_t val, uint32_t timeout_ms)
{
    uint8_t tx[2] = {reg, val};
    return vl53l0x_i2c_wr(dev, tx, 2, timeout_ms);
}

/**
 * @brief 读 1B 寄存器
 * @param val 输出寄存器值
 */
static int vl53l0x_rd8(struct vl53l0x_device* dev, uint8_t reg, uint8_t* val, uint32_t timeout_ms)
{
    int ret = vl53l0x_i2c_wr(dev, &reg, 1, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    return vl53l0x_i2c_rd(dev, val, 1, timeout_ms);
}

/**
 * @brief 读 16bit 大端寄存器
 * @param val 输出寄存器值
 */
static int vl53l0x_rd16(struct vl53l0x_device* dev, uint8_t reg, uint16_t* val, uint32_t timeout_ms)
{
    uint8_t raw[2];
    int ret = vl53l0x_i2c_wr(dev, &reg, 1, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_i2c_rd(dev, raw, 2, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    *val = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
    return VFS_OK;
}

/**
 * @brief 首次 open 时初始化硬件：软复位 + 模型校验 + dataInit 片段
 * @return VFS_OK 或 VFS_ERR_*
 */
static int vl53l0x_hw_create(struct vl53l0x_device* dev)
{
    uint8_t model = 0;
    int ret;
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    ret = device_open(dev->i2c_dev, NULL);
    if (ret != VFS_OK)
        return ret;
    /* soft reset */
    ret = vl53l0x_wr8(dev, 0xBF, 0x00, 100);
    if (ret != VFS_OK)
        goto fail;
    osal_delay_ms(1);
    ret = vl53l0x_wr8(dev, 0xBF, 0x01, 100);
    if (ret != VFS_OK)
        goto fail;
    osal_delay_ms(10);
    ret = vl53l0x_rd8(dev, 0xC0, &model, 100);
    if (ret != VFS_OK)
        goto fail;
    if (model != 0xEE)
    {
        SYS_LOGE(k_tag, "bad model id 0x%02x", model);
        ret = VFS_ERR_NODEV;
        goto fail;
    }
    /* Pololu/ST 精简 dataInit 片段：保存 stop_variable，供单次测距 */
    ret = vl53l0x_wr8(dev, 0x88, 0x00, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0x80, 0x01, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0xFF, 0x01, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0x00, 0x00, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_rd8(dev, 0x91, &dev->stop_variable, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0x00, 0x01, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0xFF, 0x00, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0x80, 0x00, 100);
    if (ret != VFS_OK)
        goto fail;
    ret = vl53l0x_wr8(dev, 0x01, 0xFF, 100); /* SYSTEM_SEQUENCE_CONFIG */
    if (ret != VFS_OK)
        goto fail;
    dev->hw_ready = 1;
    return VFS_OK;
fail:
    COMPAT_IGNORE_RESULT(device_close(dev->i2c_dev));
    return ret;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void vl53l0x_hw_destroy(struct vl53l0x_device* dev)
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
static int vl53l0x_open(struct device* pdev, void* arg)
{
    struct vl53l0x_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = vl53l0x_get_drvdata(pdev);
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
        ret = vl53l0x_hw_create(dev);
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
static int vl53l0x_close(struct device* pdev)
{
    struct vl53l0x_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = vl53l0x_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        vl53l0x_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*vl53l0x_ioctl_fn_t)(struct vl53l0x_device* dev, void* arg, size_t arg_len,
                                  uint32_t ms);
struct vl53l0x_ioctl_map
{
    vl53l0x_ioctl_fn_t handler;
};

/**
 * @brief VL53L0X_CMD_READ_DISTANCE 实现：单次测距启动 → 等待完成 → 读毫米值
 */
static int vl53l0x_cmd_read(struct vl53l0x_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct vl53l0x_sample* o = (struct vl53l0x_sample*)arg;
    uint8_t st = 0;
    uint16_t mm = 0;
    int i;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    /* 单次测距启动序列（对齐常见开源 VL53L0X 驱动，非完整 ST API 校准） */
    ret = vl53l0x_wr8(dev, 0x80, 0x01, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0xFF, 0x01, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0x00, 0x00, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0x91, dev->stop_variable, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0x00, 0x01, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0xFF, 0x00, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0x80, 0x00, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = vl53l0x_wr8(dev, 0x00, 0x01, timeout_ms); /* SYSRANGE_START */
    if (ret != VFS_OK)
        return ret;
    for (i = 0; i < 100; i++)
    {
        ret = vl53l0x_rd8(dev, 0x00, &st, timeout_ms);
        if (ret != VFS_OK)
            return ret;
        if ((st & 0x01) == 0)
            break;
        osal_delay_ms(1);
    }
    for (i = 0; i < 100; i++)
    {
        ret = vl53l0x_rd8(dev, 0x13, &st, timeout_ms); /* RESULT_INTERRUPT_STATUS */
        if (ret != VFS_OK)
            return ret;
        if (st & 0x07)
            break;
        osal_delay_ms(1);
    }
    if ((st & 0x07) == 0)
        return VFS_ERR_TIMEOUT;
    ret = vl53l0x_rd16(dev, 0x1E, &mm, timeout_ms); /* RESULT_RANGE_MILLIMETER */
    if (ret != VFS_OK)
        return ret;
    COMPAT_IGNORE_RESULT(vl53l0x_wr8(dev, 0x0B, 0x01, timeout_ms)); /* clear interrupt */
    o->mm = mm;
    return VFS_OK;
}

static const struct vl53l0x_ioctl_map s_vl53l0x_map[VL53L0X_CMD_COUNT] = {
    [VL53L0X_CMD_READ_DISTANCE - VL53L0X_CMD_BASE - 1] = {vl53l0x_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int vl53l0x_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct vl53l0x_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = vl53l0x_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)VL53L0X_CMD_BASE;
    if (off < 1 || off > VL53L0X_CMD_COUNT || !s_vl53l0x_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_vl53l0x_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations vl53l0x_fops = {
    .open = vl53l0x_open,
    .close = vl53l0x_close,
    .ioctl = vl53l0x_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int vl53l0x_probe(struct device* pdev)
{
    struct vl53l0x_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_vl53l0x_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_vl53l0x_pool[pool_idx];
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
    dev->ops = vl53l0x_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_vl53l0x_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int vl53l0x_remove(struct device* pdev)
{
    struct vl53l0x_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = vl53l0x_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_vl53l0x_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    vl53l0x_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_vl53l0x_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(vl53l0x, "st,vl53l0x", vl53l0x_probe, vl53l0x_remove)
