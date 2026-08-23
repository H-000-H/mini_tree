/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file w25qxx_drv.c
 *@brief W25Qxx SPI NOR Flash 驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_w25qxx_pool[W25QXX_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 w25qxx_drv.h。
 *   数据流: VFS ioctl → w25qxx_cmd_jedec → SPI transfer（vfs-spi）→ HAL
 */

#include "w25qxx_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_WINBOND_W25QXX
#define DTC_GEN_COUNT_WINBOND_W25QXX 1
#endif
#define W25QXX_POOL_COUNT DTC_GEN_COUNT_WINBOND_W25QXX

/** @brief W25Qxx 驱动实例（嵌入 fops） */
struct w25qxx_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct w25qxx_device s_w25qxx_pool[W25QXX_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_w25qxx_used[W25QXX_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_w25qxx_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "w25qxx";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void w25qxx_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_w25qxx_pool_ctrl, s_w25qxx_used, W25QXX_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct w25qxx_device* w25qxx_get_drvdata(struct device* pdev)
{
    return (struct w25qxx_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int w25qxx_spi_xfer(struct w25qxx_device* dev, const uint8_t* tx, uint8_t* rx, size_t len,
                           uint32_t timeout_ms)
{
    struct spi_transfer_arg arg;
    if (!dev || !dev->spi_dev || len == 0U)
        return MINI_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(dev->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), timeout_ms);
}

/**
 * @brief 首次 open 时打开 SPI 总线（空实现，仅确保 hw_ready）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int w25qxx_hw_create(struct w25qxx_device* dev)
{
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    {
        int ret = device_open(dev->spi_dev, NULL);
        if (ret != MINI_OK)
            return ret;
    }
    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 SPI client）
 */
static void w25qxx_hw_destroy(struct w25qxx_device* dev)
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
static int w25qxx_open(struct device* pdev, void* arg)
{
    struct w25qxx_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = w25qxx_get_drvdata(pdev);
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
        ret = w25qxx_hw_create(dev);
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
static int w25qxx_close(struct device* pdev)
{
    struct w25qxx_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = w25qxx_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        w25qxx_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*w25qxx_ioctl_fn_t)(struct w25qxx_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct w25qxx_ioctl_map
{
    w25qxx_ioctl_fn_t handler;
};

/**
 * @brief W25QXX_CMD_READ_JEDEC_ID 实现：发 0x9F 读 3B 厂商 ID
 */
static int w25qxx_cmd_jedec(struct w25qxx_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    uint8_t tx[4] = {0x9F, 0, 0, 0}, rx[4] = {0};
    struct w25qxx_jedec* j = (struct w25qxx_jedec*)arg;
    if (!dev->hw_ready || !j || len != sizeof(*j))
        return MINI_ERR_INVAL;
    if (w25qxx_spi_xfer(dev, tx, rx, 4, timeout_ms) != MINI_OK)
        return MINI_ERR_IO;
    j->id[0] = rx[1];
    j->id[1] = rx[2];
    j->id[2] = rx[3];
    return MINI_OK;
}
static const struct w25qxx_ioctl_map s_w25qxx_map[W25QXX_CMD_COUNT] = {
    [W25QXX_CMD_READ_JEDEC_ID - W25QXX_CMD_BASE - 1] = {w25qxx_cmd_jedec},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int w25qxx_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct w25qxx_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = w25qxx_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)W25QXX_CMD_BASE;
    if (off < 1 || off > W25QXX_CMD_COUNT || !s_w25qxx_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_w25qxx_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations w25qxx_fops = {
    .open = w25qxx_open,
    .close = w25qxx_close,
    .ioctl = w25qxx_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int w25qxx_probe(struct device* pdev)
{
    struct w25qxx_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_w25qxx_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_w25qxx_pool[pool_idx];
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    dev->spi_dev = device_get_parent(pdev);
    if (!dev->spi_dev)
    {
        ret = MINI_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, dev) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err;
    }
    dev->ops = w25qxx_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_w25qxx_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int w25qxx_remove(struct device* pdev)
{
    struct w25qxx_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = w25qxx_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_w25qxx_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    w25qxx_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_w25qxx_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(w25qxx, "winbond,w25qxx", w25qxx_probe, w25qxx_remove)
