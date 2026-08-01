/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file rc522_drv.c
 * @brief RC522 RFID 读卡驱动实现 — 挂在 SPI 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_rc522_pool[RC522_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 rc522_drv.h，寄存器定义见 rc522_regs.h。
 *
 * 数据流: VFS ioctl → rc522_cmd_* → rc522_to_card → SPI transfer（vfs-spi）→ HAL
 */
#include "rc522_drv.h"
#include "rc522_regs.h"
#include "vfs-spi.h"

#include "device.h"
#include "driver.h"
#include "dev_lifecycle.h"
#include "status.h"
#include "dt_config_gen.h"
#include "compiler_compat.h"
#include "osal.h"
#include "system_log.h"
#include <stddef.h>
#include <stdint.h>
#include "compiler_compat_poison.h"

#ifndef DTC_GEN_COUNT_NXP_RC522
#define DTC_GEN_COUNT_NXP_RC522  1
#endif
#define RC522_POOL_COUNT  DTC_GEN_COUNT_NXP_RC522

/** @brief RC522 驱动实例（嵌入 fops） */
struct rc522_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device*         spi_dev;  /**< 所属 SPI client 设备 */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct rc522_device s_rc522_pool[RC522_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_rc522_used[RC522_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_rc522_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "rc522";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void rc522_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_rc522_pool_ctrl, s_rc522_used, RC522_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct rc522_device* rc522_get_drvdata(struct device* dev)
{
    return (struct rc522_device*)device_get_priv(dev);
}


/**
 * @brief SPI 全双工传输（AUTO 模式）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int rc522_spi_xfer(struct rc522_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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
static int rc522_hw_create(struct rc522_device* d)
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
static void rc522_hw_destroy(struct rc522_device* d)
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
static int rc522_open(struct device* dev, void* arg)
{
    struct rc522_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rc522_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;
    ret = VFS_OK;
    if (first == 1)
    {
        ret = rc522_hw_create(d);
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
static int rc522_close(struct device* dev)
{
    struct rc522_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rc522_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        rc522_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*rc522_ioctl_fn_t)(struct rc522_device* d, void* arg, size_t arg_len, uint32_t ms);
struct rc522_ioctl_map { rc522_ioctl_fn_t handler; };


/**
 * @brief 写寄存器（地址左移 1bit 对齐 SPI 帧）
 */
static int rc522_wreg(struct rc522_device* d, uint8_t reg, uint8_t val, uint32_t to)
{
    uint8_t tx[2] = {(uint8_t)((reg << 1) & RC522_SPI_ADDR_MASK), val};
    return rc522_spi_xfer(d, tx, NULL, 2, to);
}

/**
 * @brief 读寄存器（带读标志位）
 * @param val 输出寄存器值
 */
static int rc522_rreg(struct rc522_device* d, uint8_t reg, uint8_t* val, uint32_t to)
{
    uint8_t tx[2] = {(uint8_t)(((reg << 1) & RC522_SPI_ADDR_MASK) | RC522_SPI_READ_FLAG), 0};
    uint8_t rx[2] = {0};
    int r = rc522_spi_xfer(d, tx, rx, 2, to);
    if (r != VFS_OK)
        return r;
    *val = rx[1];
    return VFS_OK;
}

/**
 * @brief 置位寄存器位（读-改-写）
 */
static int rc522_set_bits(struct rc522_device* d, uint8_t reg, uint8_t mask, uint32_t to)
{
    uint8_t v = 0;
    int r = rc522_rreg(d, reg, &v, to);
    if (r != VFS_OK)
        return r;
    return rc522_wreg(d, reg, (uint8_t)(v | mask), to);
}

/**
 * @brief 清除寄存器位（读-改-写）
 */
static int rc522_clr_bits(struct rc522_device* d, uint8_t reg, uint8_t mask, uint32_t to)
{
    uint8_t v = 0;
    int r = rc522_rreg(d, reg, &v, to);
    if (r != VFS_OK)
        return r;
    return rc522_wreg(d, reg, (uint8_t)(v & (uint8_t)~mask), to);
}

/**
 * @brief 与卡片收发（FIFO 装载 → 命令执行 → 等中断 → 读回数据/错误）
 * @param cmd 命令（TRANSCEIVE / MF_AUTHENT）
 * @param send 发送缓冲
 * @param back 回读缓冲（可空）
 * @param back_len 回读比特数（可空）
 */
static int rc522_to_card(struct rc522_device* d, uint8_t cmd, const uint8_t* send, uint8_t send_len, uint8_t* back, uint8_t* back_len, uint32_t to)
{
    uint8_t irq_en = 0;
    uint8_t wait_irq = 0;
    uint8_t n;
    uint8_t last_bits;
    int i;
    int r;
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
    r = rc522_wreg(d, RC522_REG_COMIEN, (uint8_t)(irq_en | RC522_IRQ_IEN), to);
    if (r != VFS_OK)
        return r;
    r = rc522_clr_bits(d, RC522_REG_COMIRQ, RC522_IRQ_IEN, to);
    if (r != VFS_OK)
        return r;
    r = rc522_set_bits(d, RC522_REG_FIFO_LEVEL, RC522_BIT_FLUSH_FIFO, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_COMMAND, RC522_OP_IDLE, to);
    if (r != VFS_OK)
        return r;
    for (i = 0; i < (int)send_len; i++)
    {
        r = rc522_wreg(d, RC522_REG_FIFO_DATA, send[i], to);
        if (r != VFS_OK)
            return r;
    }
    r = rc522_wreg(d, RC522_REG_COMMAND, cmd, to);
    if (r != VFS_OK)
        return r;
    if (cmd == RC522_OP_TRANSCEIVE)
    {
        r = rc522_set_bits(d, RC522_REG_BIT_FRAMING, RC522_BIT_START_SEND, to);
        if (r != VFS_OK)
            return r;
    }
    i = 2000;
    do
    {
        r = rc522_rreg(d, RC522_REG_COMIRQ, &n, to);
        if (r != VFS_OK)
            return r;
        i--;
    } while (i && !(n & RC522_IRQ_TIMER) && !(n & wait_irq));
    r = rc522_clr_bits(d, RC522_REG_BIT_FRAMING, RC522_BIT_START_SEND, to);
    if (r != VFS_OK)
        return r;
    if (i == 0)
        return VFS_ERR_TIMEOUT;
    r = rc522_rreg(d, RC522_REG_ERROR, &n, to);
    if (r != VFS_OK)
        return r;
    if (n & RC522_IRQ_ERR_MASK)
        return VFS_ERR_IO;
    if (cmd == RC522_OP_TRANSCEIVE && back && back_len)
    {
        r = rc522_rreg(d, RC522_REG_FIFO_LEVEL, &n, to);
        if (r != VFS_OK)
            return r;
        r = rc522_rreg(d, RC522_REG_CONTROL, &last_bits, to);
        if (r != VFS_OK)
            return r;
        last_bits &= RC522_BIT_RX_ALIGN;
        if (last_bits)
            *back_len = (uint8_t)(((n - 1U) * 8U) + last_bits);
        else
            *back_len = (uint8_t)(n * 8U);
        if (n > RC522_FIFO_MAX)
            n = RC522_FIFO_MAX;
        for (i = 0; i < (int)n; i++)
        {
            r = rc522_rreg(d, RC522_REG_FIFO_DATA, &back[i], to);
            if (r != VFS_OK)
                return r;
        }
    }
    return VFS_OK;
}

/**
 * @brief RC522_CMD_INIT 实现：软复位 + 定时器/调制/模式配置 + 开天线
 */
static int rc522_cmd_init(struct rc522_device* d, void* arg, size_t len, uint32_t to)
{
    int r;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!d->hw_ready)
        return VFS_ERR_INVAL;
    r = rc522_wreg(d, RC522_REG_COMMAND, RC522_OP_SOFT_RESET, to);
    if (r != VFS_OK)
        return r;
    osal_delay_ms(50);
    r = rc522_wreg(d, RC522_REG_TMODE, RC522_INIT_TMODE, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_TPRESCALER, RC522_INIT_TPRESCALER, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_TRELOAD_H, RC522_INIT_TRELOAD_H, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_TRELOAD_L, RC522_INIT_TRELOAD_L, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_TX_ASK, RC522_INIT_TX_ASK, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_MODE, RC522_INIT_MODE, to);
    if (r != VFS_OK)
        return r;
    return rc522_set_bits(d, RC522_REG_TX_CONTROL, RC522_ANTENNA_ON_MASK, to);
}

/**
 * @brief RC522_CMD_READ_UID 实现：REQA → 防冲突 → BCC 校验 → 输出 4B UID
 */
static int rc522_cmd_uid(struct rc522_device* d, void* arg, size_t len, uint32_t to)
{
    struct rc522_uid* o = (struct rc522_uid*)arg;
    uint8_t req[1] = {RC522_PICC_REQA};
    uint8_t atqa[2] = {0};
    uint8_t atqa_bits = 0;
    uint8_t anti[2] = {RC522_PICC_ANTICOLL1, RC522_PICC_SELECTNVB};
    uint8_t uid[5] = {0};
    uint8_t uid_bits = 0;
    int r;
    if (!d->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    r = rc522_wreg(d, RC522_REG_BIT_FRAMING, RC522_BIT_TX_LASTBITS7, to);
    if (r != VFS_OK)
        return r;
    r = rc522_to_card(d, RC522_OP_TRANSCEIVE, req, 1, atqa, &atqa_bits, to);
    if (r != VFS_OK)
        return r;
    r = rc522_wreg(d, RC522_REG_BIT_FRAMING, 0x00, to);
    if (r != VFS_OK)
        return r;
    r = rc522_to_card(d, RC522_OP_TRANSCEIVE, anti, 2, uid, &uid_bits, to);
    if (r != VFS_OK)
        return r;
    if ((uid_bits / 8U) < 5U)
        return VFS_ERR_IO;
    if ((uint8_t)(uid[0] ^ uid[1] ^ uid[2] ^ uid[3]) != uid[4])
        return VFS_ERR_IO;
    o->uid[0] = uid[0];
    o->uid[1] = uid[1];
    o->uid[2] = uid[2];
    o->uid[3] = uid[3];
    o->len = 4;
    return VFS_OK;
}


static const struct rc522_ioctl_map s_rc522_map[RC522_CMD_COUNT] = {
    [RC522_CMD_INIT - RC522_CMD_BASE - 1] = { rc522_cmd_init },
    [RC522_CMD_READ_UID - RC522_CMD_BASE - 1] = { rc522_cmd_uid },
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int rc522_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct rc522_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = rc522_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)RC522_CMD_BASE;
    if (off < 1 || off > RC522_CMD_COUNT || !s_rc522_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_rc522_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations rc522_fops = {
    .open  = rc522_open,
    .close = rc522_close,
    .ioctl = rc522_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 SPI 设备并挂 fops
 */
static int rc522_probe(struct device* dev)
{
    struct rc522_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_rc522_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_rc522_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = rc522_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rc522_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int rc522_remove(struct device* dev)
{
    struct rc522_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = rc522_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_rc522_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    rc522_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_rc522_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(rc522, "nxp,rc522", rc522_probe, rc522_remove)
