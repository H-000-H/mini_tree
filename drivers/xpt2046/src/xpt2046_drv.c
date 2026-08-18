/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file xpt2046_drv.c
 * @brief XPT2046 电阻触摸驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_xpt2046_pool[XPT2046_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与采样结构见 xpt2046_drv.h。
 *
 * 数据流: VFS ioctl → xpt2046_cmd_xy → SPI transfer（vfs-spi）→ HAL
 */
#include "xpt2046_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-gpio.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_TI_XPT2046
#define DTC_GEN_COUNT_TI_XPT2046 1
#endif
#define XPT2046_POOL_COUNT DTC_GEN_COUNT_TI_XPT2046

/** @brief XPT2046 驱动实例（嵌入 fops 与 SPI/IRQ 句柄） */
struct xpt2046_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */
    struct device* irq_dev; /**< 中断引脚 GPIO 设备（phandle: irq-gpio，可选） */
    struct vfs_gpio_arg irq_gpio; /**< 中断引脚 GPIO 参数 */
    int has_irq; /**< 已配置 irq-gpio */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct xpt2046_device s_xpt2046_pool[XPT2046_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_xpt2046_used[XPT2046_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_xpt2046_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "xpt2046";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void xpt2046_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_xpt2046_pool_ctrl, s_xpt2046_used, XPT2046_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct xpt2046_device* xpt2046_get_drvdata(struct device* pdev)
{
    return (struct xpt2046_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int xpt2046_spi_xfer(struct xpt2046_device* dev, const uint8_t* tx, uint8_t* rx, size_t len,
                            uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;
    if (!dev || !dev->spi_dev || len == 0U)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(dev->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 首次 open 时打开 SPI 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int xpt2046_hw_create(struct xpt2046_device* dev)
{
    if (!dev)
        return VFS_ERR_INVAL;
    if (dev->hw_ready)
        return VFS_OK;
    {
        int ret = device_open(dev->spi_dev, NULL);
        if (ret != VFS_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 SPI client）
 */
static void xpt2046_hw_destroy(struct xpt2046_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(dev->spi_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int xpt2046_open(struct device* pdev, void* arg)
{
    struct xpt2046_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = xpt2046_get_drvdata(pdev);
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
        ret = xpt2046_hw_create(dev);
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
static int xpt2046_close(struct device* pdev)
{
    struct xpt2046_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = xpt2046_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        xpt2046_hw_destroy(dev);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*xpt2046_ioctl_fn_t)(struct xpt2046_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct xpt2046_ioctl_map
{
    xpt2046_ioctl_fn_t handler;
};

/**
 * @brief XPT2046_CMD_READ_XY 实现：SPI 读 X（0x90）与 Y（0xD0）通道各 12bit
 */
static int xpt2046_cmd_xy(struct xpt2046_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    uint8_t tx[3] = {0x90, 0, 0}, rx[3] = {0};
    struct xpt2046_xy* p = (struct xpt2046_xy*)arg;
    if (!dev->hw_ready || !p || len != sizeof(*p))
        return VFS_ERR_INVAL;
    if (xpt2046_spi_xfer(dev, tx, rx, 3, timeout_ms) != VFS_OK)
        return VFS_ERR_IO;
    p->pos_x = (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
    tx[0] = 0xD0;
    if (xpt2046_spi_xfer(dev, tx, rx, 3, timeout_ms) != VFS_OK)
        return VFS_ERR_IO;
    p->pos_y = (uint16_t)(((rx[1] << 8) | rx[2]) >> 3);
    p->pressed = 1;
    return VFS_OK;
}
static const struct xpt2046_ioctl_map s_xpt2046_map[XPT2046_CMD_COUNT] = {
    [XPT2046_CMD_READ_XY - XPT2046_CMD_BASE - 1] = {xpt2046_cmd_xy},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int xpt2046_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct xpt2046_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    dev = xpt2046_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)XPT2046_CMD_BASE;
    if (off < 1 || off > XPT2046_CMD_COUNT || !s_xpt2046_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_xpt2046_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations xpt2046_fops = {
    .open = xpt2046_open,
    .close = xpt2046_close,
    .ioctl = xpt2046_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备与可选 irq-gpio 并挂 fops
 */
static int xpt2046_probe(struct device* pdev)
{
    struct xpt2046_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_xpt2046_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    dev = &s_xpt2046_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->spi_dev = device_get_parent(pdev);
    if (!dev->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }
    dev->irq_dev = device_get_phandle_dev(pdev, "irq-gpio");
    if (IS_ERR(dev->irq_dev))
    {
        dev->irq_dev = NULL;
        dev->has_irq = 0;
    }
    else
        dev->has_irq = 1;

    if (device_set_priv(pdev, dev) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    dev->ops = xpt2046_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_xpt2046_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int xpt2046_remove(struct device* pdev)
{
    struct xpt2046_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    dev = xpt2046_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_xpt2046_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    xpt2046_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_xpt2046_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(xpt2046, "ti,xpt2046", xpt2046_probe, xpt2046_remove)
