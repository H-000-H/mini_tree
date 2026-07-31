/* SPDX-License-Identifier: Apache-2.0 */
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

struct ds18b20_device
{
    struct file_operations ops;
    struct device* data_dev;
    struct vfs_gpio_arg data_gpio;

    int                    hw_ready;
};

static struct ds18b20_device s_ds18b20_pool[DS18B20_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_ds18b20_used[DS18B20_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_ds18b20_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "ds18b20";

pre_execution(160)
static void ds18b20_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ds18b20_pool_ctrl, s_ds18b20_used, DS18B20_POOL_COUNT));
}

static struct ds18b20_device* ds18b20_get_drvdata(struct device* dev)
{
    return (struct ds18b20_device*)device_get_priv(dev);
}


static void ds18b20_delay_us(uint32_t us)
{
    osal_delay_us(us);
}

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

static void ds18b20_hw_destroy(struct ds18b20_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->data_dev)
        COMPAT_IGNORE_RESULT(device_close(d->data_dev));
    d->hw_ready = 0;
}

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

typedef int (*ds18b20_ioctl_fn_t)(struct ds18b20_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ds18b20_ioctl_map { ds18b20_ioctl_fn_t handler; };


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
