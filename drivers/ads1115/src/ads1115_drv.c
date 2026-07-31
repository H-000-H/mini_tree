/* SPDX-License-Identifier: Apache-2.0 */
#include "ads1115_drv.h"
#include "vfs-i2c.h"

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

#ifndef DTC_GEN_COUNT_TI_ADS1115
#define DTC_GEN_COUNT_TI_ADS1115  1
#endif
#define ADS1115_POOL_COUNT  DTC_GEN_COUNT_TI_ADS1115

struct ads1115_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct ads1115_device s_ads1115_pool[ADS1115_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_ads1115_used[ADS1115_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_ads1115_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "ads1115";

pre_execution(160)
static void ads1115_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ads1115_pool_ctrl, s_ads1115_used, ADS1115_POOL_COUNT));
}

static struct ads1115_device* ads1115_get_drvdata(struct device* dev)
{
    return (struct ads1115_device*)device_get_priv(dev);
}


static int ads1115_i2c_wr(struct ads1115_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}
static int ads1115_i2c_rd(struct ads1115_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int ads1115_hw_create(struct ads1115_device* d)
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

static void ads1115_hw_destroy(struct ads1115_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

static int ads1115_open(struct device* dev, void* arg)
{
    struct ads1115_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ads1115_get_drvdata(dev);
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
        ret = ads1115_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int ads1115_close(struct device* dev)
{
    struct ads1115_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ads1115_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ads1115_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*ads1115_ioctl_fn_t)(struct ads1115_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ads1115_ioctl_map { ads1115_ioctl_fn_t handler; };


static int ads1115_cmd_read(struct ads1115_device* d, void* arg, size_t len, uint32_t to)
{
    struct ads1115_sample* o = (struct ads1115_sample*)arg;
    uint8_t cfg[3];
    uint8_t ptr = 0x00;
    uint8_t raw[2];
    uint16_t mux;
    int r;
    if (!d->hw_ready || !o || len != sizeof(*o) || o->channel < 0 || o->channel > 3)
        return VFS_ERR_INVAL;
    mux = (uint16_t)(0x8000U | ((uint16_t)(o->channel + 4) << 12) | 0x0200U | 0x0100U);
    cfg[0] = 0x01;
    cfg[1] = (uint8_t)(mux >> 8);
    cfg[2] = (uint8_t)mux;
    r = ads1115_i2c_wr(d, cfg, 3, to);
    if (r != VFS_OK)
        return r;
    osal_delay_ms(10);
    r = ads1115_i2c_wr(d, &ptr, 1, to);
    if (r != VFS_OK)
        return r;
    r = ads1115_i2c_rd(d, raw, 2, to);
    if (r != VFS_OK)
        return r;
    o->raw = (int16_t)((raw[0] << 8) | raw[1]);
    return VFS_OK;
}


static const struct ads1115_ioctl_map s_ads1115_map[ADS1115_CMD_COUNT] = {
    [ADS1115_CMD_READ_CHANNEL - ADS1115_CMD_BASE - 1] = { ads1115_cmd_read },
};

static int ads1115_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ads1115_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ads1115_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)ADS1115_CMD_BASE;
    if (off < 1 || off > ADS1115_CMD_COUNT || !s_ads1115_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ads1115_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ads1115_fops = {
    .open  = ads1115_open,
    .close = ads1115_close,
    .ioctl = ads1115_ioctl,
};

static int ads1115_probe(struct device* dev)
{
    struct ads1115_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ads1115_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ads1115_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = ads1115_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ads1115_pool_ctrl, pool_idx));
    return ret;
}

static int ads1115_remove(struct device* dev)
{
    struct ads1115_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = ads1115_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ads1115_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ads1115_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ads1115_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ads1115, "ti,ads1115", ads1115_probe, ads1115_remove)
