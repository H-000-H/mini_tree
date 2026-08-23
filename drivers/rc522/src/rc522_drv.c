/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file rc522_drv.c
 *@brief RC522 RFID 读卡驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *@author H-000-H
 *@details
 *   静态池: s_rc522_pool[RC522_POOL_COUNT]，probe 时 claim、remove 时 release；
 *   ioctl 命令与参数结构见 rc522_drv.h，寄存器定义见 rc522_regs.h。
 *   数据流: VFS ioctl → rc522_cmd_* → rc522_to_card → SPI transfer（vfs-spi）→ HAL
 */

#include "rc522_drv.h"

#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "rc522_regs.h"
#include "status.h"
#include "system_log.h"
#include "vfs-spi.h"
#include <stddef.h>
#include <stdint.h>

#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_NXP_RC522
#define DTC_GEN_COUNT_NXP_RC522 1
#endif
#define RC522_POOL_COUNT DTC_GEN_COUNT_NXP_RC522

/** @brief RC522 驱动实例（嵌入 fops） */
struct rc522_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* spi_dev; /**< 所属 SPI client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct rc522_device s_rc522_pool[RC522_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_rc522_used[RC522_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_rc522_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "rc522";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void rc522_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_rc522_pool_ctrl, s_rc522_used, RC522_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param[in] pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct rc522_device* rc522_get_drvdata(struct device* pdev)
{
    return (struct rc522_device*)device_get_priv(pdev);
}

/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return MINI_OK 或 VFS_ERR_*
 */
static int rc522_spi_xfer(struct rc522_device* dev, const uint8_t* tx, uint8_t* rx, size_t len,
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
static int rc522_hw_create(struct rc522_device* dev)
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
static void rc522_hw_destroy(struct rc522_device* dev)
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
static int rc522_open(struct device* pdev, void* arg)
{
    struct rc522_device* dev;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rc522_get_drvdata(pdev);
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
        ret = rc522_hw_create(dev);
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
static int rc522_close(struct device* pdev)
{
    struct rc522_device* dev;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rc522_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        rc522_hw_destroy(dev);
    dev_lc_close_end(lc);
    return MINI_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*rc522_ioctl_fn_t)(struct rc522_device* dev, void* arg, size_t arg_len, uint32_t ms);
struct rc522_ioctl_map
{
    rc522_ioctl_fn_t handler;
};

/**
 * @brief 写寄存器（地址左移 1bit 对齐 SPI 帧）
 */
static int rc522_wreg(struct rc522_device* dev, uint8_t reg, uint8_t val, uint32_t timeout_ms)
{
    uint8_t tx[2] = {(uint8_t)((reg << 1) & RC522_SPI_ADDR_MASK), val};
    return rc522_spi_xfer(dev, tx, NULL, 2, timeout_ms);
}

/**
 * @brief 读寄存器（带读标志位）
 * @param[in] val 输出寄存器值
 */
static int rc522_rreg(struct rc522_device* dev, uint8_t reg, uint8_t* val, uint32_t timeout_ms)
{
    uint8_t tx[2] = {(uint8_t)(((reg << 1) & RC522_SPI_ADDR_MASK) | RC522_SPI_READ_FLAG), 0};
    uint8_t rx[2] = {0};
    int ret = rc522_spi_xfer(dev, tx, rx, 2, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    *val = rx[1];
    return MINI_OK;
}

/**
 * @brief 置位寄存器位（读-改-写）
 */
static int rc522_set_bits(struct rc522_device* dev, uint8_t reg, uint8_t mask, uint32_t timeout_ms)
{
    uint8_t val = 0;
    int ret = rc522_rreg(dev, reg, &val, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return rc522_wreg(dev, reg, (uint8_t)(val | mask), timeout_ms);
}

/**
 * @brief 清除寄存器位（读-改-写）
 */
static int rc522_clr_bits(struct rc522_device* dev, uint8_t reg, uint8_t mask, uint32_t timeout_ms)
{
    uint8_t val = 0;
    int ret = rc522_rreg(dev, reg, &val, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return rc522_wreg(dev, reg, (uint8_t)(val & (uint8_t)~mask), timeout_ms);
}

/**
 * @brief 与卡片收发（FIFO 装载 → 命令执行 → 等中断 → 读回数据/错误）
 * @param[in] cmd 命令（TRANSCEIVE / MF_AUTHENT）
 * @param[in] send 发送缓冲
 * @param[in] back 回读缓冲（可空）
 * @param[in] back_len 回读比特数（可空）
 */
static int rc522_to_card(struct rc522_device* dev, uint8_t cmd, const uint8_t* send,
                         uint8_t send_len, uint8_t* back, uint8_t* back_len, uint32_t timeout_ms)
{
    uint8_t irq_en = 0;
    uint8_t wait_irq = 0;
    uint8_t count;
    uint8_t last_bits;
    int i;
    int ret;
    if (cmd == RC522_OP_MF_AUTHENT)
    {
        irq_en = RC522_IRQ_AUTH_EN;
        wait_irq = RC522_IRQ_AUTH_WAIT;
    }
    else if (cmd == RC522_OP_TRANSCEIVE)
    {
        irq_en = RC522_IRQ_TXRX_EN;
        wait_irq = RC522_IRQ_TXRX_WAIT;
    }
    ret = rc522_wreg(dev, RC522_REG_COMIEN, (uint8_t)(irq_en | RC522_IRQ_IEN), timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_clr_bits(dev, RC522_REG_COMIRQ, RC522_IRQ_IEN, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_set_bits(dev, RC522_REG_FIFO_LEVEL, RC522_BIT_FLUSH_FIFO, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_COMMAND, RC522_OP_IDLE, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    for (i = 0; i < (int)send_len; i++)
    {
        ret = rc522_wreg(dev, RC522_REG_FIFO_DATA, send[i], timeout_ms);
        if (ret != MINI_OK)
            return ret;
    }
    ret = rc522_wreg(dev, RC522_REG_COMMAND, cmd, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    if (cmd == RC522_OP_TRANSCEIVE)
    {
        ret = rc522_set_bits(dev, RC522_REG_BIT_FRAMING, RC522_BIT_START_SEND, timeout_ms);
        if (ret != MINI_OK)
            return ret;
    }
    i = 2000;
    do
    {
        ret = rc522_rreg(dev, RC522_REG_COMIRQ, &count, timeout_ms);
        if (ret != MINI_OK)
            return ret;
        i--;
    } while (i && !(count & RC522_IRQ_TIMER) && !(count & wait_irq));
    ret = rc522_clr_bits(dev, RC522_REG_BIT_FRAMING, RC522_BIT_START_SEND, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    if (i == 0)
        return MINI_ERR_TIMEOUT;
    ret = rc522_rreg(dev, RC522_REG_ERROR, &count, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    if (count & RC522_IRQ_ERR_MASK)
        return MINI_ERR_IO;
    if (cmd == RC522_OP_TRANSCEIVE && back && back_len)
    {
        ret = rc522_rreg(dev, RC522_REG_FIFO_LEVEL, &count, timeout_ms);
        if (ret != MINI_OK)
            return ret;
        ret = rc522_rreg(dev, RC522_REG_CONTROL, &last_bits, timeout_ms);
        if (ret != MINI_OK)
            return ret;
        last_bits &= RC522_BIT_RX_ALIGN;
        if (last_bits)
            *back_len = (uint8_t)(((count - 1U) * 8U) + last_bits);
        else
            *back_len = (uint8_t)(count * 8U);
        if (count > RC522_FIFO_MAX)
            count = RC522_FIFO_MAX;
        for (i = 0; i < (int)count; i++)
        {
            ret = rc522_rreg(dev, RC522_REG_FIFO_DATA, &back[i], timeout_ms);
            if (ret != MINI_OK)
                return ret;
        }
    }
    return MINI_OK;
}

/**
 * @brief RC522_CMD_INIT 实现：软复位 + 定时器/调制/模式配置 + 开天线
 */
static int rc522_cmd_init(struct rc522_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    int ret;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!dev->hw_ready)
        return MINI_ERR_INVAL;
    ret = rc522_wreg(dev, RC522_REG_COMMAND, RC522_OP_SOFT_RESET, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    osal_delay_ms(50);
    ret = rc522_wreg(dev, RC522_REG_TMODE, RC522_INIT_TMODE, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_TPRESCALER, RC522_INIT_TPRESCALER, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_TRELOAD_H, RC522_INIT_TRELOAD_H, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_TRELOAD_L, RC522_INIT_TRELOAD_L, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_TX_ASK, RC522_INIT_TX_ASK, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_MODE, RC522_INIT_MODE, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    return rc522_set_bits(dev, RC522_REG_TX_CONTROL, RC522_ANTENNA_ON_MASK, timeout_ms);
}

/**
 * @brief RC522_CMD_READ_UID 实现：REQA → 防冲突 → BCC 校验 → 输出 4B UID
 */
static int rc522_cmd_uid(struct rc522_device* dev, void* arg, size_t len, uint32_t timeout_ms)
{
    struct rc522_uid* o = (struct rc522_uid*)arg;
    uint8_t req[1] = {RC522_PICC_REQA};
    uint8_t atqa[2] = {0};
    uint8_t atqa_bits = 0;
    uint8_t anti[2] = {RC522_PICC_ANTICOLL1, RC522_PICC_SELECTNVB};
    uint8_t uid[5] = {0};
    uint8_t uid_bits = 0;
    int ret;
    if (!dev->hw_ready || !o || len != sizeof(*o))
        return MINI_ERR_INVAL;
    ret = rc522_wreg(dev, RC522_REG_BIT_FRAMING, RC522_BIT_TX_LASTBITS7, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_to_card(dev, RC522_OP_TRANSCEIVE, req, 1, atqa, &atqa_bits, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_wreg(dev, RC522_REG_BIT_FRAMING, 0x00, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    ret = rc522_to_card(dev, RC522_OP_TRANSCEIVE, anti, 2, uid, &uid_bits, timeout_ms);
    if (ret != MINI_OK)
        return ret;
    if ((uid_bits / 8U) < 5U)
        return MINI_ERR_IO;
    if ((uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]) != uid[4])
        return MINI_ERR_IO;
    o->uid[0] = uid[0];
    o->uid[1] = uid[1];
    o->uid[2] = uid[2];
    o->uid[3] = uid[3];
    o->len = 4;
    return MINI_OK;
}

static const struct rc522_ioctl_map s_rc522_map[RC522_CMD_COUNT] = {
    [RC522_CMD_INIT - RC522_CMD_BASE - 1] = {rc522_cmd_init},
    [RC522_CMD_READ_UID - RC522_CMD_BASE - 1] = {rc522_cmd_uid},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int rc522_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct rc522_device* dev;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;
    dev = rc522_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != MINI_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RC522_CMD_BASE;
    if (off < 1 || off > RC522_CMD_COUNT || !s_rc522_map[off - 1].handler)
        ret = MINI_ERR_INVAL;
    else
        ret = s_rc522_map[off - 1].handler(dev, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations rc522_fops = {
    .open = rc522_open,
    .close = rc522_close,
    .ioctl = rc522_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int rc522_probe(struct device* pdev)
{
    struct rc522_device* dev;
    int pool_idx, ret;
    if (!pdev)
        return MINI_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_rc522_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;
    dev = &s_rc522_pool[pool_idx];
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
    dev->ops = rc522_fops;
    pdev->ops = &dev->ops;
    SYS_LOGI(k_tag, "probe OK pool=%dev", pool_idx);
    return MINI_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rc522_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int rc522_remove(struct device* pdev)
{
    struct rc522_device* dev;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return MINI_ERR_INVAL;
    dev = rc522_get_drvdata(pdev);
    if (IS_ERR(dev))
        return PTR_ERR(dev);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(dev - s_rc522_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }
    rc522_hw_destroy(dev);
    COMPAT_MEM_SET(dev, 0, sizeof(*dev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rc522_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

DRIVER_REGISTER(rc522, "nxp,rc522", rc522_probe, rc522_remove)
