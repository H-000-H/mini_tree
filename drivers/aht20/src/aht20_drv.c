/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file aht20_drv.c
 *@brief AHT20 温湿度传感器驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_aht20_pool[AHT20_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与采样结构见 aht20_drv.h。
 *   数据流: VFS ioctl → aht20_cmd_read → device_read/write(I2C) → HAL
 */

#include "aht20_drv.h"

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

#ifndef DTC_GEN_COUNT_AOSONG_AHT20
#define DTC_GEN_COUNT_AOSONG_AHT20 1
#endif
#define AHT20_POOL_COUNT DTC_GEN_COUNT_AOSONG_AHT20

/** @brief AHT20 驱动实例（嵌入 fops） */
struct aht20_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct aht20_device s_aht20_pool[AHT20_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_aht20_used[AHT20_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_aht20_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "aht20";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void aht20_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_aht20_pool_ctrl, s_aht20_used, AHT20_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct aht20_device* aht20_get_drvdata(struct device* pdev)
{
    return (struct aht20_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return MINI_OK 或 VFS_ERR_*
 */
static int aht20_i2c_wr(struct aht20_device* dev, const uint8_t* tx, size_t len,
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
static int aht20_i2c_rd(struct aht20_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return MINI_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int aht20_hw_create(struct aht20_device* dev)
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
static void aht20_hw_destroy(struct aht20_device* dev)
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
static int aht20_open(struct device* pdev, void* arg)
{
    struct aht20_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = aht20_get_drvdata(pdev);
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
        ret = aht20_hw_create(dev);
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
static int aht20_close(struct device* pdev)
{
    struct aht20_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = aht20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        aht20_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*aht20_ioctl_fn_t)(struct aht20_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct aht20_ioctl_map
{
    aht20_ioctl_fn_t handler;
};

/**
 * @brief AHT20_CMD_READ_TEMP_RH 实现：触发测量（80ms）并换算 T/RH
 */
static int aht20_cmd_read(struct aht20_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    const uint8_t trig[3] = {0xAC, 0x33, 0x00};
    uint8_t raw[6];
    struct aht20_sample* o = (struct aht20_sample*)arg;
    int ret;
    uint32_t rh, t;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return MINI_ERR_INVAL;
    ret = aht20_i2c_wr(dev, trig, 3, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    osal_delay_ms(80);
    ret = aht20_i2c_rd(dev, raw, 6, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    rh = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    t = (((uint32_t)raw[3] & 0x0FU) << 16) | ((uint32_t)raw[4] << 8) | raw[5];
    o->rh_x100 = (uint16_t)((rh * 10000U) / 1048576U);
    o->temp_c_x100 = (int16_t)(((int32_t)t * 20000) / 1048576 - 5000);
    return MINI_OK;
}

static const struct aht20_ioctl_map s_aht20_map[AHT20_CMD_COUNT] = {
    [AHT20_CMD_READ_TEMP_RH - AHT20_CMD_BASE - 1] = {aht20_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int aht20_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct aht20_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = aht20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)AHT20_CMD_BASE;
    if (off < 1 || off > AHT20_CMD_COUNT || !s_aht20_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_aht20_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations aht20_fops = {
    .open = aht20_open,
    .close = aht20_close,
    .ioctl = aht20_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int aht20_probe(struct device* pdev)
{
    struct aht20_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_aht20_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_aht20_pool[pool_idx];
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
    dev->ops = aht20_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_aht20_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int aht20_remove(struct device* pdev)
{
    struct aht20_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = aht20_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_aht20_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    aht20_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_aht20_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(aht20, "aosong,aht20", aht20_probe, aht20_remove)
