/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file nrf24l01_drv.c
 *@brief NRF24L01 2.4G 无线驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_nrf24l01_pool[NRF24L01_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 nrf24l01_drv.h，操作码定义见 nrf24l01_regs.h。
 *   数据流: VFS ioctl → nrf24l01_cmd_* → SPI transfer（vfs-spi）→ HAL
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

static struct nrf24l01_device s_nrf24l01_pool[NRF24L01_POOL_COUNT] MINI_ALIGNED(4);
static uint8_t s_nrf24l01_used[NRF24L01_POOL_COUNT] MINI_ALIGNED(4);
static osal_pool_t s_nrf24l01_pool_ctrl MINI_ALIGNED(4);
static const char* const k_tag = "nrf24l01";

/**
 * @brief 驱动池启动初始化（mini_pre_execution 阶段，创建静态对象池）
 */
mini_pre_execution(MINI_PRE_EXEC_PRIO_DRIVER_POOL) static void nrf24l01_pool_boot_init(void)
{
    MINI_IGNORE_RESULT(
        osal_pool_init(&s_nrf24l01_pool_ctrl, s_nrf24l01_used, NRF24L01_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct nrf24l01_device* nrf24l01_get_drvdata(struct device* pdev)
{
    return (struct nrf24l01_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int nrf24l01_spi_xfer(struct nrf24l01_device* dev, const uint8_t* tx, uint8_t* rx,
                             size_t len, uint32_t timeout_ms)
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
static int nrf24l01_hw_create(struct nrf24l01_device* dev)
{
    int ret;
    if (!dev)
        return MINI_ERR_INVAL;
    if (dev->hw_ready)
        return MINI_OK;
    ret = device_open(dev->spi_dev, NULL);
    if (ret != MINI_OK)
        return ret;

    dev->hw_ready = 1;
    return MINI_OK;
}

/**
 * @brief 释放硬件资源（关闭 SPI client）
 */
static void nrf24l01_hw_destroy(struct nrf24l01_device* dev)
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
static int nrf24l01_open(struct device* pdev, void* arg)
{
    struct nrf24l01_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    MINI_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = nrf24l01_get_drvdata(pdev);
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
        ret = nrf24l01_hw_create(dev);
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
static int nrf24l01_close(struct device* pdev)
{
    struct nrf24l01_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        nrf24l01_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*nrf24l01_ioctl_fn_t)(struct nrf24l01_device* dev, void* arg, size_t arg_len,
                                   uint32_t ms);
struct nrf24l01_ioctl_map
{
    nrf24l01_ioctl_fn_t handler;
};

/**
 * @brief NRF24L01_CMD_WRITE_REG 实现：写寄存器
 */
static int nrf24l01_cmd_wreg(struct nrf24l01_device* dev, void* arg, size_t len,
                             uint32_t timeout_ms)
{
    struct nrf24l01_reg* reg_arg = (struct nrf24l01_reg*)arg;
    uint8_t tx[2];
    if (!dev->hw_ready || !reg_arg || len != sizeof(*reg_arg))
        return MINI_ERR_INVAL;
    tx[0] = (uint8_t)(NRF24L01_OP_W_REGISTER | (reg_arg->reg & NRF24L01_REG_ADDR_MASK));
    tx[1] = reg_arg->val;
    return nrf24l01_spi_xfer(dev, tx, NULL, 2, timeout_ms);
}

/**
 * @brief NRF24L01_CMD_READ_REG 实现：读寄存器并回填 val
 */
static int nrf24l01_cmd_rreg(struct nrf24l01_device* dev, void* arg, size_t len,
                             uint32_t timeout_ms)
{
    struct nrf24l01_reg* reg_arg = (struct nrf24l01_reg*)arg;
    uint8_t tx[2] = {0};
    uint8_t rx[2] = {0};
    int ret;
    if (!dev->hw_ready || !reg_arg || len != sizeof(*reg_arg))
        return MINI_ERR_INVAL;
    tx[0] = (uint8_t)(reg_arg->reg & NRF24L01_REG_ADDR_MASK);
    ret = nrf24l01_spi_xfer(dev, tx, rx, 2, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    reg_arg->val = rx[1];
    return MINI_OK;
}

/**
 * @brief NRF24L01_CMD_SEND 实现：写 TX 载荷（超长截断）
 */
static int nrf24l01_cmd_send(struct nrf24l01_device* dev, void* arg, size_t len,
                             uint32_t timeout_ms)
{
    struct nrf24l01_payload* payload = (struct nrf24l01_payload*)arg;
    uint8_t tx[NRF24L01_MAX_PAYLOAD + 1U];
    size_t count;
    if (!dev->hw_ready || !payload || len != sizeof(*payload) || !payload->data ||
        payload->len == 0U)
        return MINI_ERR_INVAL;
    count = payload->len > NRF24L01_MAX_PAYLOAD ? NRF24L01_MAX_PAYLOAD : payload->len;
    tx[0] = NRF24L01_OP_W_TX_PAYLOAD;
    MINI_IGNORE_RESULT(MINI_MEM_COPY(&tx[1], payload->data, count));
    return nrf24l01_spi_xfer(dev, tx, NULL, count + 1U, timeout_ms);
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
    struct nrf24l01_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)NRF24L01_CMD_BASE;
    if (off < 1 || off > NRF24L01_CMD_COUNT || !s_nrf24l01_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_nrf24l01_map[off - 1].handler(dev, arg, arg_len, ms);
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
    struct nrf24l01_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_nrf24l01_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_nrf24l01_pool[pool_idx];
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
    dev->ops = nrf24l01_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int nrf24l01_remove(struct device* pdev)
{
    struct nrf24l01_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = nrf24l01_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_nrf24l01_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    nrf24l01_hw_destroy(dev);
    MINI_MEM_SET(dev, 0, sizeof(*dev));
    MINI_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(nrf24l01, "nordic,nrf24l01", nrf24l01_probe, nrf24l01_remove)
