/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file nrf24l01_drv.c
 * @brief NRF24L01 2.4G 无线驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_nrf24l01_pool[NRF24L01_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 nrf24l01_drv.h，操作码定义见 nrf24l01_regs.h。
 *
 * 数据流: VFS ioctl → nrf24l01_cmd_* → SPI transfer（vfs-spi）→ HAL
 */
#include "nrf24l01_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "nrf24l01_regs.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_NORDIC_NRF24L01
#define DTC_GEN_COUNT_NORDIC_NRF24L01 1
#endif
#define NRF24L01_POOL_COUNT DTC_GEN_COUNT_NORDIC_NRF24L01

/** @brief NRF24L01 驱动实例（嵌入 fops） */
struct nrf24l01_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct nrf24l01_device s_nrf24l01_pool[NRF24L01_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_nrf24l01_used[NRF24L01_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_nrf24l01_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "nrf24l01";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160) static void nrf24l01_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_nrf24l01_pool_ctrl, s_nrf24l01_used, NRF24L01_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct nrf24l01_device* nrf24l01_get_drvdata(struct device* pdev)
{
    return (struct nrf24l01_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int nrf24l01_spi_xfer(struct nrf24l01_device* d, const uint8_t* tx, uint8_t* rx, size_t len,
                             uint32_t to)
{
    struct spi_transfer_arg arg;
    if (!d || !d->spi_dev || len == 0U)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.len = len;
    arg.xfer_mode = SPI_XFER_AUTO;
    return device_ioctl(d->spi_dev, SPI_CMD_TRANSFER, &arg, sizeof(arg), to);
}

/**
 * @brief 首次 open 时打开 SPI 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int nrf24l01_hw_create(struct nrf24l01_device* d)
{
    int r;
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    r = device_open(d->spi_dev, NULL);
    if (r != VFS_OK)
        return r;

    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 SPI client）
 */
static void nrf24l01_hw_destroy(struct nrf24l01_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int nrf24l01_open(struct device* pdev, void* arg)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = nrf24l01_hw_create(d);
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
static int nrf24l01_close(struct device* pdev)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        nrf24l01_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*nrf24l01_ioctl_fn_t)(struct nrf24l01_device* d, void* arg, size_t arg_len,
                                   uint32_t ms);
struct nrf24l01_ioctl_map
{
    nrf24l01_ioctl_fn_t handler;
};

/**
 * @brief NRF24L01_CMD_WRITE_REG 实现：写寄存器
 */
static int nrf24l01_cmd_wreg(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_reg* a = (struct nrf24l01_reg*)arg;
    uint8_t tx[2];
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    tx[0] = (uint8_t)(NRF24L01_OP_W_REGISTER | (a->reg & NRF24L01_REG_ADDR_MASK));
    tx[1] = a->val;
    return nrf24l01_spi_xfer(d, tx, NULL, 2, to);
}

/**
 * @brief NRF24L01_CMD_READ_REG 实现：读寄存器并回填 val
 */
static int nrf24l01_cmd_rreg(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_reg* a = (struct nrf24l01_reg*)arg;
    uint8_t tx[2] = {0};
    uint8_t rx[2] = {0};
    int r;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    tx[0] = (uint8_t)(a->reg & NRF24L01_REG_ADDR_MASK);
    r = nrf24l01_spi_xfer(d, tx, rx, 2, to);
    if (r != VFS_OK)
        return r;
    a->val = rx[1];
    return VFS_OK;
}

/**
 * @brief NRF24L01_CMD_SEND 实现：写 TX 载荷（超长截断）
 */
static int nrf24l01_cmd_send(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_payload* p = (struct nrf24l01_payload*)arg;
    uint8_t tx[NRF24L01_MAX_PAYLOAD + 1U];
    size_t n;
    if (!d->hw_ready || !p || len != sizeof(*p) || !p->data || p->len == 0U)
        return VFS_ERR_INVAL;
    n = p->len > NRF24L01_MAX_PAYLOAD ? NRF24L01_MAX_PAYLOAD : p->len;
    tx[0] = NRF24L01_OP_W_TX_PAYLOAD;
    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&tx[1], p->data, n));
    return nrf24l01_spi_xfer(d, tx, NULL, n + 1U, to);
}

static const struct nrf24l01_ioctl_map s_nrf24l01_map[NRF24L01_CMD_COUNT] = {
    [NRF24L01_CMD_WRITE_REG - NRF24L01_CMD_BASE - 1] = {nrf24l01_cmd_wreg},
    [NRF24L01_CMD_READ_REG - NRF24L01_CMD_BASE - 1] = {nrf24l01_cmd_rreg},
    [NRF24L01_CMD_SEND - NRF24L01_CMD_BASE - 1] = {nrf24l01_cmd_send},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int nrf24l01_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)NRF24L01_CMD_BASE;
    if (off < 1 || off > NRF24L01_CMD_COUNT || !s_nrf24l01_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_nrf24l01_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations nrf24l01_fops = {
    .open = nrf24l01_open,
    .close = nrf24l01_close,
    .ioctl = nrf24l01_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int nrf24l01_probe(struct device* pdev)
{
    struct nrf24l01_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_nrf24l01_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_nrf24l01_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(pdev);
    if (!d->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = nrf24l01_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int nrf24l01_remove(struct device* pdev)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_nrf24l01_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    nrf24l01_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(nrf24l01, "nordic,nrf24l01", nrf24l01_probe, nrf24l01_remove)
