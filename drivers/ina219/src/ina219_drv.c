/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ina219_drv.c
 *@brief INA219 电流/功率监测驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_ina219_pool[INA219_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 ina219_drv.h。
 *   数据流: VFS ioctl → ina219_cmd_read → device_read/write(I2C) → HAL
 */

#include "ina219_drv.h"

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

#ifndef DTC_GEN_COUNT_TI_INA219
#define DTC_GEN_COUNT_TI_INA219 1
#endif
#define INA219_POOL_COUNT DTC_GEN_COUNT_TI_INA219

/** @brief INA219 驱动实例（嵌入 fops） */
struct ina219_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ina219_device s_ina219_pool[INA219_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_ina219_used[INA219_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_ina219_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "ina219";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void ina219_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_ina219_pool_ctrl, s_ina219_used, INA219_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ina219_device* ina219_get_drvdata(struct device* pdev) { return (struct ina219_device*)device_get_priv(pdev); }

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_i2c_wr(struct ina219_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_i2c_rd(struct ina219_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_hw_create(struct ina219_device* dev)
{
    int ret;
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    ret = device_open(dev->i2c_dev, NULL);
    if (ret != VFS_OK)
        return ret;

    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ina219_hw_destroy(struct ina219_device* dev)
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
static int ina219_open(struct device* pdev, void* arg)
{
    struct ina219_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ina219_get_drvdata(pdev);
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
        ret = ina219_hw_create(dev);
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
static int ina219_close(struct device* pdev)
{
    struct ina219_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ina219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ina219_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ina219_ioctl_fn_t)(struct ina219_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct ina219_ioctl_map
{
    ina219_ioctl_fn_t handler;
};

/**
 * @brief 读 16bit 大端寄存器
 * @param[out] out 输出寄存器值
 */
static int ina219_rd16(struct ina219_device* dev, uint8_t reg, int16_t* out, uint32_t timeout_ms)
{
    uint8_t raw[2];
    int ret = ina219_i2c_wr(dev, &reg, 1, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = ina219_i2c_rd(dev, raw, 2, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    *out = (int16_t)((raw[0] << 8) | raw[1]);
    return VFS_OK;
}
/**
 * @brief INA219_CMD_READ_POWER 实现：读总线电压/电流/功率寄存器并换算
 */
static int ina219_cmd_read(struct ina219_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct ina219_sample* o = (struct ina219_sample*)arg;
    int16_t bus, cur, pwr;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    ret = ina219_rd16(dev, 0x02, &bus, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = ina219_rd16(dev, 0x04, &cur, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = ina219_rd16(dev, 0x03, &pwr, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    o->bus_mV = (int16_t)((bus >> 3) * 4);
    o->current_mA = cur;
    o->power_mW = (int16_t)(pwr * 20);
    return VFS_OK;
}

static const struct ina219_ioctl_map s_ina219_map[INA219_CMD_COUNT] = {
    [INA219_CMD_READ_POWER - INA219_CMD_BASE - 1] = {ina219_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ina219_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ina219_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = ina219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)INA219_CMD_BASE;
    if (off < 1 || off > INA219_CMD_COUNT || !s_ina219_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ina219_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ina219_fops = {
    .open = ina219_open,
    .close = ina219_close,
    .ioctl = ina219_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int ina219_probe(struct device* pdev)
{
    struct ina219_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ina219_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_ina219_pool[pool_idx];
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
    dev->ops = ina219_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ina219_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ina219_remove(struct device* pdev)
{
    struct ina219_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = ina219_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_ina219_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ina219_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ina219_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ina219, "ti,ina219", ina219_probe, ina219_remove)
