/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file ina219_drv.c
 * @brief INA219 电流/功率监测驱动实现 — 挂在 I2C 总线 client 下的 VFS 设备驱动
 *
 * 静态池: s_ina219_pool[INA219_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令与采样结构见 ina219_drv.h。
 *
 * 数据流: VFS ioctl → ina219_cmd_read → device_read/write(I2C) → HAL
 */
#include "ina219_drv.h"

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

#ifndef DTC_GEN_COUNT_TI_INA219
#define DTC_GEN_COUNT_TI_INA219 1
#endif
#define INA219_POOL_COUNT DTC_GEN_COUNT_TI_INA219

/** @brief INA219 驱动实例（嵌入 fops） */
struct ina219_device
{
    struct file_operations ops; /**< 挂入 device 的 fops */
    struct device* i2c_dev; /**< 所属 I2C client 设备 */

    int hw_ready; /**< 硬件已初始化标志 */
};

static struct ina219_device s_ina219_pool[INA219_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_ina219_used[INA219_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_ina219_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_tag = "ina219";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160) static void ina219_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ina219_pool_ctrl, s_ina219_used, INA219_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param pdev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ina219_device* ina219_get_drvdata(struct device* pdev)
{
    return (struct ina219_device*)device_get_priv(pdev);
}

/**
 * @brief 向 I2C 总线写数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_i2c_wr(struct ina219_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}
/**
 * @brief 从 I2C 总线读数据
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_i2c_rd(struct ina219_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}

/**
 * @brief 首次 open 时打开 I2C 总线（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ina219_hw_create(struct ina219_device* d)
{
    int r;
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    r = device_open(d->i2c_dev, NULL);
    if (r != VFS_OK)
        return r;

    d->hw_ready = 1;
    return VFS_OK;
}

/**
 * @brief 释放硬件资源（关闭 I2C client）
 */
static void ina219_hw_destroy(struct ina219_device* d)
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
static int ina219_open(struct device* pdev, void* arg)
{
    struct ina219_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ina219_get_drvdata(pdev);
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
        ret = ina219_hw_create(d);
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
static int ina219_close(struct device* pdev)
{
    struct ina219_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ina219_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ina219_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ina219_ioctl_fn_t)(struct ina219_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ina219_ioctl_map
{
    ina219_ioctl_fn_t handler;
};

/**
 * @brief 读 16bit 大端寄存器
 * @param out 输出寄存器值
 */
static int ina219_rd16(struct ina219_device* d, uint8_t reg, int16_t* out, uint32_t to)
{
    uint8_t raw[2];
    int r = ina219_i2c_wr(d, &reg, 1, to);
    if (r != VFS_OK)
        return r;
    r = ina219_i2c_rd(d, raw, 2, to);
    if (r != VFS_OK)
        return r;
    *out = (int16_t)((raw[0] << 8) | raw[1]);
    return VFS_OK;
}
/**
 * @brief INA219_CMD_READ_POWER 实现：读总线电压/电流/功率寄存器并换算
 */
static int ina219_cmd_read(struct ina219_device* d, void* arg, size_t len, uint32_t to)
{
    struct ina219_sample* o = (struct ina219_sample*)arg;
    int16_t bus, cur, pwr;
    int r;
    if (!d->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    r = ina219_rd16(d, 0x02, &bus, to);
    if (r != VFS_OK)
        return r;
    r = ina219_rd16(d, 0x04, &cur, to);
    if (r != VFS_OK)
        return r;
    r = ina219_rd16(d, 0x03, &pwr, to);
    if (r != VFS_OK)
        return r;
    o->bus_mV = (int16_t)((bus >> 3) * 4);
    o->current_mA = cur;
    o->power_mW = (int16_t)(pwr * 20);
    return VFS_OK;
}

static const struct ina219_ioctl_map s_ina219_map[INA219_CMD_COUNT] = {
    [INA219_CMD_READ_POWER - INA219_CMD_BASE - 1] = {ina219_cmd_read},
};

/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ina219_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ina219_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;
    d = ina219_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)INA219_CMD_BASE;
    if (off < 1 || off > INA219_CMD_COUNT || !s_ina219_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ina219_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ina219_fops = {
    .open = ina219_open,
    .close = ina219_close,
    .ioctl = ina219_ioctl,
};

/**
 * @brief probe：claim 池项、绑定父 I2C 设备并挂 fops
 */
static int ina219_probe(struct device* pdev)
{
    struct ina219_device* d;
    int pool_idx, ret;
    if (!pdev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ina219_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ina219_pool[pool_idx];
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
    d->ops = ina219_fops;
    pdev->ops = &d->ops;
    SYS_LOGI(k_tag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    pdev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ina219_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ina219_remove(struct device* pdev)
{
    struct ina219_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!pdev)
        return VFS_ERR_INVAL;
    d = ina219_get_drvdata(pdev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ina219_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ina219_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ina219_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ina219, "ti,ina219", ina219_probe, ina219_remove)
