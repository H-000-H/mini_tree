/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file ads1115_drv.c
 *@brief ADS1115 16bit ADC 驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_ads1115_pool[ADS1115_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 ads1115_drv.h。
 *   数据流: VFS ioctl → ads1115_cmd_read → device_read/write(I2C) → HAL
 */

#include "ads1115_drv.h"

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

#ifndef DTC_GEN_COUNT_TI_ADS1115
#define DTC_GEN_COUNT_TI_ADS1115 1
#endif
#define ADS1115_POOL_COUNT DTC_GEN_COUNT_TI_ADS1115

/** @brief ADS1115 驱动实例（嵌入 fops） */
struct ads1115_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ads1115_device s_ads1115_pool[ADS1115_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_ads1115_used[ADS1115_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_ads1115_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "ads1115";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void ads1115_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ads1115_pool_ctrl, s_ads1115_used, ADS1115_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ads1115_device* ads1115_get_drvdata(struct device* pdev)
{
    return (struct ads1115_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ads1115_i2c_wr(struct ads1115_device* dev, const uint8_t* tx, size_t len,
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
static int ads1115_i2c_rd(struct ads1115_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int ads1115_hw_create(struct ads1115_device* dev)
{
    int ret;
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->i2c_dev, NULL);
    if (ret != MINI_OK)
        return ret;

    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ads1115_hw_destroy(struct ads1115_device* dev)
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
static int ads1115_open(struct device* pdev, void* arg)
{
    struct ads1115_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ads1115_get_drvdata(pdev);
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
        ret = ads1115_hw_create(dev);
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
static int ads1115_close(struct device* pdev)
{
    struct ads1115_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ads1115_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ads1115_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ads1115_ioctl_fn_t)(struct ads1115_device* dev, void* arg, size_t arg_len,
                                  uint32_t ms);
struct ads1115_ioctl_map
{
    ads1115_ioctl_fn_t handler;
};

/**
 * @brief ADS1115_CMD_READ_CHANNEL 实现：配置 MUX + 单次模式，延时后读转换值
 */
static int ads1115_cmd_read(struct ads1115_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct ads1115_sample* o = (struct ads1115_sample*)arg;
    uint8_t cfg[3];
    uint8_t ptr = 0x00;
    uint8_t raw[2];
    uint16_t mux;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o) || o->channel < 0 || o->channel > 3)
        return MINI_ERR_INVAL;
    mux = (uint16_t)(0x8000U | ((uint16_t)(o->channel + 4) << 12) | 0x0200U | 0x0100U);
    cfg[0] = 0x01;
    cfg[1] = (uint8_t)(mux >> 8);
    cfg[2] = (uint8_t)mux;
    ret = ads1115_i2c_wr(dev, cfg, 3, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    osal_delay_ms(10);
    ret = ads1115_i2c_wr(dev, &ptr, 1, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = ads1115_i2c_rd(dev, raw, 2, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    o->raw = (int16_t)((raw[0] << 8) | raw[1]);
    return MINI_OK;
}

static const struct ads1115_ioctl_map s_ads1115_map[ADS1115_CMD_COUNT] = {
    [ADS1115_CMD_READ_CHANNEL - ADS1115_CMD_BASE - 1] = {ads1115_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ads1115_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ads1115_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = ads1115_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)ADS1115_CMD_BASE;
    if (off < 1 || off > ADS1115_CMD_COUNT || !s_ads1115_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_ads1115_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ads1115_fops = {
    .open = ads1115_open,
    .close = ads1115_close,
    .ioctl = ads1115_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int ads1115_probe(struct device* pdev)
{
    struct ads1115_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ads1115_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_ads1115_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
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
    dev->ops = ads1115_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ads1115_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ads1115_remove(struct device* pdev)
{
    struct ads1115_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = ads1115_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_ads1115_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    ads1115_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ads1115_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(ads1115, "ti,ads1115", ads1115_probe, ads1115_remove)
