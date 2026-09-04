/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file sx1278_drv.c
 *@brief SX1278 LoRa 模块驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_sx1278_pool[SX1278_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 sx1278_drv.h。
 *   数据流: VFS ioctl → sx1278_cmd_* → SPI transfer（vfs-spi）→ HAL
 */

#include "sx1278_drv.h"

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

#ifndef DTC_GEN_COUNT_SEMTECH_SX1278
#define DTC_GEN_COUNT_SEMTECH_SX1278 1
#endif
#define SX1278_POOL_COUNT DTC_GEN_COUNT_SEMTECH_SX1278

/** @brief SX1278 驱动实例（嵌入 fops 与操作模式状态） */
struct sx1278_device
{
    struct file_operations ops;     /**< 挂入 device 的 fops */
    struct device*         spi_dev; /**< 所属 SPI client 设备 */
    uint8_t                opmode;  /**< 当前工作模式（OPMODE 寄存器缓存） */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct sx1278_device           s_sx1278_pool[SX1278_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t                        s_sx1278_used[SX1278_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_sx1278_pool_ctrl MINI_ALIGNED(4);
static const char* const              k_tag = "sx1278";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void sx1278_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(osal_pool_init(&s_sx1278_pool_ctrl, s_sx1278_used, SX1278_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct sx1278_device* sx1278_get_drvdata(struct device* pdev) { return (struct sx1278_device*)device_get_priv(pdev); }

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int sx1278_spi_xfer(struct sx1278_device* dev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms)
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
static int sx1278_hw_create(struct sx1278_device* dev)
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
static void sx1278_hw_destroy(struct sx1278_device* dev)
{
    if (!dev || !dev->hw_ready)
        return;
    if (dev->spi_dev)
        MINI_IGNORE_RESULT(device_close(dev->spi_dev));
    dev->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int sx1278_open(struct device* pdev, void* arg)
{
    struct sx1278_device* dev;
    struct dev_lifecycle* lc;
    int                   first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sx1278_get_drvdata(pdev);
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
        ret = sx1278_hw_create(dev);
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
static int sx1278_close(struct device* pdev)
{
    struct sx1278_device* dev;
    struct dev_lifecycle* lc;
    int                   last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sx1278_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sx1278_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*sx1278_ioctl_fn_t)(struct sx1278_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct sx1278_ioctl_map
{
    sx1278_ioctl_fn_t handler;
};

/**
 * @brief 写寄存器（写标志位 + 地址 + 值）
 */
static int sx1278_wr_reg(struct sx1278_device* dev, uint8_t reg, uint8_t val, uint32_t timeout_ms)
{
    uint8_t tx[2] = {(uint8_t)(reg | 0x80U), val};
    return sx1278_spi_xfer(dev, tx, NULL, 2, timeout_ms);
}
/**
 * @brief SX1278_CMD_RESET 实现：复位到睡眠模式
 */
static int sx1278_cmd_reset(struct sx1278_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    MINI_IGNORE_RESULT(arg);
    MINI_IGNORE_RESULT(len);
    if (sx1278_wr_reg(dev, 0x01, 0x00, timeout_ms) != MINI_OK)
        return MINI_ERR_IO;
    dev->opmode = 0;
    return MINI_OK;
}
/**
 * @brief SX1278_CMD_SET_FREQ 实现：按 Frf 公式换算并写频点寄存器
 */
static int sx1278_cmd_freq(struct sx1278_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    uint32_t hz;
    uint8_t  frf[3];
    uint64_t freq;
    if (!arg || len != sizeof(uint32_t))
        return MINI_ERR_INVAL;
    hz = *(uint32_t*)arg;
    freq = ((uint64_t)hz << 19) / 32000000ULL;
    frf[0] = (uint8_t)(freq >> 16);
    frf[1] = (uint8_t)(freq >> 8);
    frf[2] = (uint8_t)freq;
    {
        uint8_t tx[4] = {0x06 | 0x80U, frf[0], frf[1], frf[2]};
        if (sx1278_spi_xfer(dev, tx, NULL, 4, timeout_ms) != MINI_OK)
            return MINI_ERR_IO;
    }
    return MINI_OK;
}
/**
 * @brief SX1278_CMD_SEND 实现：切 TX 模式并写 FIFO 载荷（截断至 255B）
 */
static int sx1278_cmd_send(struct sx1278_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct sx1278_payload* pl = (struct sx1278_payload*)arg;
    if (!pl || len != sizeof(*pl) || !pl->data || !pl->len)
        return MINI_ERR_INVAL;
    if (sx1278_wr_reg(dev, 0x01, 0x83, timeout_ms) != MINI_OK)
        return MINI_ERR_IO;
    {
        uint8_t tx[257];
        size_t  count = pl->len;
        if (count > 255U)
            count = 255U;
        tx[0] = 0x80;
        MINI_IGNORE_RESULT(MINI_MEM_COPY(&tx[1], pl->data, count));
        if (sx1278_spi_xfer(dev, tx, NULL, count + 1U, timeout_ms) != MINI_OK)
            return MINI_ERR_IO;
    }
    return MINI_OK;
}
/**
 * @brief SX1278_CMD_RECV 实现：读取 FIFO 首字节（简化单字节接收）
 */
static int sx1278_cmd_recv(struct sx1278_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct sx1278_payload* pl = (struct sx1278_payload*)arg;
    uint8_t                tx[2] = {0, 0};
    uint8_t                rx[2] = {0, 0};
    uint8_t*               out;

    MINI_IGNORE_RESULT(timeout_ms);
    if (!pl || len != sizeof(*pl) || !pl->data || pl->len == 0U)
        return MINI_ERR_INVAL;
    if (sx1278_spi_xfer(dev, tx, rx, 2, 50) != MINI_OK)
        return MINI_ERR_IO;
    out = (uint8_t*)(uintptr_t)pl->data;
    out[0] = rx[1];
    pl->len = 1U;
    return MINI_OK;
}
static const struct sx1278_ioctl_map s_sx1278_map[SX1278_CMD_COUNT] = {
    [SX1278_CMD_RESET - SX1278_CMD_BASE - 1] = {sx1278_cmd_reset},
    [SX1278_CMD_SET_FREQ - SX1278_CMD_BASE - 1] = {sx1278_cmd_freq},
    [SX1278_CMD_SEND - SX1278_CMD_BASE - 1] = {sx1278_cmd_send},
    [SX1278_CMD_RECV - SX1278_CMD_BASE - 1] = {sx1278_cmd_recv},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int sx1278_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sx1278_device* dev;
    struct dev_lifecycle* lc;
    int32_t               off;
    int                   ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = sx1278_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SX1278_CMD_BASE;
    if (off < 1 || off > SX1278_CMD_COUNT || !s_sx1278_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_sx1278_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sx1278_fops = {
    .open = sx1278_open,
    .close = sx1278_close,
    .ioctl = sx1278_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int sx1278_probe(struct device* pdev)
{
    struct sx1278_device* dev;
    int                   pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sx1278_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_sx1278_pool[pool_idx];
    MINI_MEM_SET(dev, 0, sizeof(*dev));
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
    dev->ops = sx1278_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_sx1278_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int sx1278_remove(struct device* pdev)
{
    struct sx1278_device* dev;
    struct dev_lifecycle* lc;
    int                   idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = sx1278_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_sx1278_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    sx1278_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_sx1278_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(sx1278, "semtech,sx1278", sx1278_probe, sx1278_remove)
