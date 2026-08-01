/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file ds18b20_drv.c
 * @brief DS18B20 单总线温度传感器驱动实现 — 挂在 GPIO 单总线（OW）下的 VFS 设备驱动
 *
 * 静态池: s_ds18b20_pool[DS18B20_POOL_COUNT]，probe 时 claim、remove 时 release；
 * ioctl 命令见 ds18b20_drv.h，单总线命令定义见 ds18b20_regs.h。
 *
 * 数据流: VFS ioctl → ds18b20_cmd_temp → GPIO 位时序（vfs_gpio_*）→ HAL
 */
#include "ds18b20_drv.h"
#include "ds18b20_regs.h"
#include "vfs-gpio.h"
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

#ifndef DTC_GEN_COUNT_MAXIM_DS18B20
#define DTC_GEN_COUNT_MAXIM_DS18B20  1
#endif
#define DS18B20_POOL_COUNT  DTC_GEN_COUNT_MAXIM_DS18B20

/** @brief DS18B20 驱动实例（嵌入 fops 与 GPIO 操作参数） */
struct ds18b20_device
{
    struct file_operations ops;      /**< 挂入 device 的 fops */
    struct device* data_dev;         /**< data 引脚所属 GPIO 设备（phandle: data-gpio） */
    struct vfs_gpio_arg data_gpio;   /**< GPIO 操作参数（引脚号 + 电平） */

    int                    hw_ready; /**< 硬件已初始化标志 */
};

static struct ds18b20_device s_ds18b20_pool[DS18B20_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_ds18b20_used[DS18B20_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_ds18b20_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "ds18b20";

/**
 * @brief 驱动池启动初始化（pre_execution 阶段，创建静态对象池）
 */
pre_execution(160)
static void ds18b20_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ds18b20_pool_ctrl, s_ds18b20_used, DS18B20_POOL_COUNT));
}

/**
 * @brief 取驱动私有数据
 * @param dev device 指针
 * @return 驱动实例指针，无效时 ERR_PTR
 */
static struct ds18b20_device* ds18b20_get_drvdata(struct device* dev)
{
    return (struct ds18b20_device*)device_get_priv(dev);
}


/**
 * @brief 微秒延时（OSAL 转发）
 */
static void ds18b20_delay_us(uint32_t us)
{
    osal_delay_us(us);
}

/**
 * @brief 单总线复位脉冲：拉低 480us 后释放，检测存在脉冲
 * @return VFS_OK（检测到应答）或 VFS_ERR_IO（无应答）
 */
static int ds18b20_reset(struct ds18b20_device* d)
{
    int present;

    d->data_gpio.level = 0;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(480);
    d->data_gpio.level = 1;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(70);
    if (vfs_gpio_get_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    present = (d->data_gpio.level == 0);
    ds18b20_delay_us(410);
    return present ? VFS_OK : VFS_ERR_IO;
}

/**
 * @brief 写 1bit（写 1：短拉低；写 0：长拉低）
 */
static int ds18b20_write_bit(struct ds18b20_device* d, int bit)
{
    d->data_gpio.level = 0;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(bit ? 6U : 60U);
    d->data_gpio.level = 1;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(bit ? 64U : 10U);
    return VFS_OK;
}

/**
 * @brief 读 1bit（拉低 3us 后释放，采样电平）
 * @param bit 输出读到的位
 */
static int ds18b20_read_bit(struct ds18b20_device* d, int* bit)
{
    d->data_gpio.level = 0;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(3);
    d->data_gpio.level = 1;
    if (vfs_gpio_set_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    ds18b20_delay_us(10);
    if (vfs_gpio_get_level(&d->data_gpio) != VFS_OK)
        return VFS_ERR_IO;
    *bit = d->data_gpio.level ? 1 : 0;
    ds18b20_delay_us(50);
    return VFS_OK;
}

/**
 * @brief 写 1B（LSB 先行）
 */
static int ds18b20_write_byte(struct ds18b20_device* d, uint8_t v)
{
    int i;
    for (i = 0; i < 8; i++)
    {
        int r = ds18b20_write_bit(d, (v >> i) & 1);
        if (r != VFS_OK)
            return r;
    }
    return VFS_OK;
}

/**
 * @brief 读 1B（LSB 先行）
 * @param v 输出读到的字节
 */
static int ds18b20_read_byte(struct ds18b20_device* d, uint8_t* v)
{
    int i;
    int b;
    uint8_t out = 0;
    for (i = 0; i < 8; i++)
    {
        if (ds18b20_read_bit(d, &b) != VFS_OK)
            return VFS_ERR_IO;
        if (b)
            out |= (uint8_t)(1U << i);
    }
    *v = out;
    return VFS_OK;
}


/**
 * @brief 首次 open 时打开 GPIO 设备并查询默认电平（空实现，仅确保 hw_ready）
 * @return VFS_OK 或 VFS_ERR_*
 */
static int ds18b20_hw_create(struct ds18b20_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->data_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->data_dev, GPIO_CMD_GET_LEVEL, &d->data_gpio, sizeof(d->data_gpio), 0);
      if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

/**
 * @brief 释放硬件资源（关闭 GPIO 设备）
 */
static void ds18b20_hw_destroy(struct ds18b20_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->data_dev)
        COMPAT_IGNORE_RESULT(device_close(d->data_dev));
    d->hw_ready = 0;
}

/**
 * @brief fops.open：引用计数打开，首次调用初始化硬件
 */
static int ds18b20_open(struct device* dev, void* arg)
{
    struct ds18b20_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ds18b20_get_drvdata(dev);
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
        ret = ds18b20_hw_create(d);
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
static int ds18b20_close(struct device* dev)
{
    struct ds18b20_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ds18b20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ds18b20_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

/**
 * @brief ioctl 命令分发类型（命令处理函数由 map 绑定）
 */
typedef int (*ds18b20_ioctl_fn_t)(struct ds18b20_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ds18b20_ioctl_map { ds18b20_ioctl_fn_t handler; };


/**
 * @brief DS18B20_CMD_READ_TEMP 实现：复位 → 转换（750ms）→ 读暂存器换算温度
 */
static int ds18b20_cmd_temp(struct ds18b20_device* d, void* arg, size_t len, uint32_t ms)
{
    uint8_t lo = 0;
    uint8_t hi = 0;
    int16_t raw;
    int* t = (int*)arg;
    COMPAT_IGNORE_RESULT(ms);
    if (!d->hw_ready || !t || len != sizeof(int))
        return VFS_ERR_INVAL;
    if (ds18b20_reset(d) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_write_byte(d, DS18B20_OW_SKIP_ROM) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_write_byte(d, DS18B20_OW_CONVERT_T) != VFS_OK)
        return VFS_ERR_IO;
    osal_delay_ms(DS18B20_CONVERT_MS);
    if (ds18b20_reset(d) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_write_byte(d, DS18B20_OW_SKIP_ROM) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_write_byte(d, DS18B20_OW_READ_SCRATCHPAD) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_read_byte(d, &lo) != VFS_OK)
        return VFS_ERR_IO;
    if (ds18b20_read_byte(d, &hi) != VFS_OK)
        return VFS_ERR_IO;
    raw = (int16_t)(((uint16_t)hi << 8) | lo);
    *t = (int)(raw / DS18B20_TEMP_LSB_PER_C);
    return VFS_OK;
}
static const struct ds18b20_ioctl_map s_ds18b20_map[DS18B20_CMD_COUNT] = {
    [DS18B20_CMD_READ_TEMP - DS18B20_CMD_BASE - 1] = { ds18b20_cmd_temp },
};


/**
 * @brief fops.ioctl：查表分发命令，持 io 生命周期锁
 */
static int ds18b20_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ds18b20_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ds18b20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)DS18B20_CMD_BASE;
    if (off < 1 || off > DS18B20_CMD_COUNT || !s_ds18b20_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ds18b20_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ds18b20_fops =
{
    .open  = ds18b20_open,
    .close = ds18b20_close,
    .ioctl = ds18b20_ioctl,
};

/**
 * @brief probe：claim 池项、绑定 data-gpio 设备并挂 fops
 */
static int ds18b20_probe(struct device* dev)
{
    struct ds18b20_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ds18b20_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ds18b20_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->data_dev = device_get_phandle_dev(dev, "data-gpio");
    if (IS_ERR(d->data_dev)) { ret = PTR_ERR(d->data_dev); goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = ds18b20_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ds18b20_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief remove：排空在途 io、释放硬件并归还池项
 */
static int ds18b20_remove(struct device* dev)
{
    struct ds18b20_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = ds18b20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ds18b20_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ds18b20_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ds18b20_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ds18b20, "maxim,ds18b20", ds18b20_probe, ds18b20_remove)
