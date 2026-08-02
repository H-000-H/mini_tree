/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file at24c02_drv.c
 * @brief AT24C02 EEPROM 驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_at24c02_pool[AT24C02_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与参数结构见 at24c02_drv.h。
 *
 * 数据流: VFS ioctl → at24c02_cmd_read/write → device_read/write(I2C) → HAL
 */
#include "at24c02_drv.h"

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

#ifndef DTC_GEN_COUNT_ATMEL_AT24C02
#define DTC_GEN_COUNT_ATMEL_AT24C02 1
#endif
#define AT24C02_POOL_COUNT DTC_GEN_COUNT_ATMEL_AT24C02

/** @brief AT24C02 驱动实例（嵌入 fops） */
struct at24c02_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct at24c02_device s_at24c02_pool[AT24C02_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_at24c02_used[AT24C02_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_at24c02_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "at24c02";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160) static void at24c02_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_at24c02_pool_ctrl, s_at24c02_used, AT24C02_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct at24c02_device* at24c02_get_drvdata(struct device* pdev)
{
    return (struct at24c02_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int at24c02_i2c_wr(struct at24c02_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int at24c02_i2c_rd(struct at24c02_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int at24c02_hw_create(struct at24c02_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    {
        int r = device_open(d->i2c_dev, NULL);
        if (r != VFS_OK)
            return r;
    }
    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void at24c02_hw_destroy(struct at24c02_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int at24c02_open(struct device* pdev, void* arg)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(pdev);
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
        ret = at24c02_hw_create(d);
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
static int at24c02_close(struct device* pdev)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        at24c02_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*at24c02_ioctl_fn_t)(struct at24c02_device* d, void* arg, size_t arg_len, uint32_t ms);
struct at24c02_ioctl_map
{
    at24c02_ioctl_fn_t handler;
};

/**
 * @brief AT24C02_CMD_READ 实现：设地址后连续读
 */
static int at24c02_cmd_read(struct at24c02_device* d, void* arg, size_t len, uint32_t to)
{
    struct at24c02_io_arg* io = (struct at24c02_io_arg*)arg;
    uint8_t a;
    if (!d->hw_ready || !io || len != sizeof(*io) || !io->buf || !io->len)
        return VFS_ERR_INVAL;
    if ((uint32_t)io->offset + io->len > AT24C02_SIZE)
        return VFS_ERR_INVAL;
    a = io->offset;
    if (at24c02_i2c_wr(d, &a, 1, to) != VFS_OK)
        return VFS_ERR_IO;
    return at24c02_i2c_rd(d, io->buf, io->len, to);
}
/**
 * @brief AT24C02_CMD_WRITE 实现：按 16B 页分块写（含写周期延时）
 */
static int at24c02_cmd_write(struct at24c02_device* d, void* arg, size_t len, uint32_t to)
{
    struct at24c02_io_arg* io = (struct at24c02_io_arg*)arg;
    uint8_t f[17];
    size_t c, o = 0;
    if (!d->hw_ready || !io || len != sizeof(*io) || !io->buf || !io->len)
        return VFS_ERR_INVAL;
    if ((uint32_t)io->offset + io->len > AT24C02_SIZE)
        return VFS_ERR_INVAL;
    while (o < io->len)
    {
        c = io->len - o;
        if (c > 16U)
            c = 16U;
        f[0] = (uint8_t)(io->offset + o);
        COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&f[1], &io->buf[o], c));
        if (at24c02_i2c_wr(d, f, c + 1U, to) != VFS_OK)
            return VFS_ERR_IO;
        osal_delay_ms(5);
        o += c;
    }
    return VFS_OK;
}
static const struct at24c02_ioctl_map s_at24c02_map[AT24C02_CMD_COUNT] = {
    [AT24C02_CMD_READ - AT24C02_CMD_BASE - 1] = {at24c02_cmd_read},
    [AT24C02_CMD_WRITE - AT24C02_CMD_BASE - 1] = {at24c02_cmd_write},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int at24c02_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)AT24C02_CMD_BASE;
    if (off < 1 || off > AT24C02_CMD_COUNT || !s_at24c02_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_at24c02_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations at24c02_fops = {
    .open = at24c02_open,
    .close = at24c02_close,
    .ioctl = at24c02_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int at24c02_probe(struct device* pdev)
{
    struct at24c02_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_at24c02_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_at24c02_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(pdev);
    if (!d->i2c_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(pdev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = at24c02_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_at24c02_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int at24c02_remove(struct device* pdev)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_at24c02_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    at24c02_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_at24c02_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(at24c02, "atmel,at24c02", at24c02_probe, at24c02_remove)
