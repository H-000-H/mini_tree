/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file pcf8574_drv.c
 *@brief PCF8574 GPIO 扩展芯片驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_pcf8574_pool[PCF8574_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令见 pcf8574_drv.h。
 *   数据流: VFS ioctl → pcf8574_cmd_* → device_read/write(I2C) → HAL
 */

#include "pcf8574_drv.h"

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

#ifndef DTC_GEN_COUNT_NXP_PCF8574
#define DTC_GEN_COUNT_NXP_PCF8574 1
#endif
#define PCF8574_POOL_COUNT DTC_GEN_COUNT_NXP_PCF8574

/** @brief PCF8574 驱动实例（嵌入 fops） */
struct pcf8574_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct pcf8574_device s_pcf8574_pool[PCF8574_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_pcf8574_used[PCF8574_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_pcf8574_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "pcf8574";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void pcf8574_pool_boot_init(void) { COMPAT_IGNORE_RESULT(osal_pool_init(&s_pcf8574_pool_ctrl, s_pcf8574_used, PCF8574_POOL_COUNT)); }

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct pcf8574_device* pcf8574_get_drvdata(struct device* pdev) { return (struct pcf8574_device*)device_get_priv(pdev); }

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int pcf8574_i2c_wr(struct pcf8574_device* dev, const uint8_t* tx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(dev->i2c_dev, tx, len, timeout_ms);
}
/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int pcf8574_i2c_rd(struct pcf8574_device* dev, uint8_t* rx, size_t len, uint32_t timeout_ms)
{
    if (!dev || !dev->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(dev->i2c_dev, rx, len, timeout_ms);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int pcf8574_hw_create(struct pcf8574_device* dev)
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
static void pcf8574_hw_destroy(struct pcf8574_device* dev)
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
static int pcf8574_open(struct device* pdev, void* arg)
{
    struct pcf8574_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = pcf8574_get_drvdata(pdev);
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
        ret = pcf8574_hw_create(dev);
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
static int pcf8574_close(struct device* pdev)
{
    struct pcf8574_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = pcf8574_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        pcf8574_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*pcf8574_ioctl_fn_t)(struct pcf8574_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct pcf8574_ioctl_map
{
    pcf8574_ioctl_fn_t handler;
};

/**
 * @brief PCF8574_CMD_WRITE 实现：写 8bit 输出口
 */
static int pcf8574_cmd_write(struct pcf8574_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    uint8_t val;
    if (!dev->hw_ready || !arg || len != sizeof(uint8_t))
        return VFS_ERR_INVAL;
    val = *(uint8_t*)arg;
    return pcf8574_i2c_wr(dev, &val, 1, timeout_ms);
}
/**
 * @brief PCF8574_CMD_READ 实现：读 8bit 输入口
 */
static int pcf8574_cmd_read(struct pcf8574_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    if (!dev->hw_ready || !arg || len != sizeof(uint8_t))
        return VFS_ERR_INVAL;
    return pcf8574_i2c_rd(dev, (uint8_t*)arg, 1, timeout_ms);
}

static const struct pcf8574_ioctl_map s_pcf8574_map[PCF8574_CMD_COUNT] = {
    [PCF8574_CMD_WRITE - PCF8574_CMD_BASE - 1] = {pcf8574_cmd_write},
    [PCF8574_CMD_READ - PCF8574_CMD_BASE - 1] = {pcf8574_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int pcf8574_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct pcf8574_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = pcf8574_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)PCF8574_CMD_BASE;
    if (off < 1 || off > PCF8574_CMD_COUNT || !s_pcf8574_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_pcf8574_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations pcf8574_fops = {
    .open = pcf8574_open,
    .close = pcf8574_close,
    .ioctl = pcf8574_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int pcf8574_probe(struct device* pdev)
{
    struct pcf8574_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_pcf8574_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_pcf8574_pool[pool_idx];
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
    dev->ops = pcf8574_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_pcf8574_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int pcf8574_remove(struct device* pdev)
{
    struct pcf8574_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = pcf8574_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_pcf8574_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    pcf8574_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_pcf8574_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(pcf8574, "nxp,pcf8574", pcf8574_probe, pcf8574_remove)
