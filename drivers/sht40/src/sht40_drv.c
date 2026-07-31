/* SPDX-License-Identifier: Apache-2.0 */
#include "sht40_drv.h"
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

#ifndef DTC_GEN_COUNT_SENSIRION_SHT40
#define DTC_GEN_COUNT_SENSIRION_SHT40  1
#endif
#define SHT40_POOL_COUNT  DTC_GEN_COUNT_SENSIRION_SHT40

struct sht40_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct sht40_device s_sht40_pool[SHT40_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_sht40_used[SHT40_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_sht40_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "sht40";

pre_execution(160)
static void sht40_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sht40_pool_ctrl, s_sht40_used, SHT40_POOL_COUNT));
}

static struct sht40_device* sht40_get_drvdata(struct device* dev)
{
    return (struct sht40_device*)device_get_priv(dev);
}


static int sht40_i2c_wr(struct sht40_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}
static int sht40_i2c_rd(struct sht40_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int sht40_hw_create(struct sht40_device* d)
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

static void sht40_hw_destroy(struct sht40_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

static int sht40_open(struct device* dev, void* arg)
{
    struct sht40_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sht40_get_drvdata(dev);
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
        ret = sht40_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int sht40_close(struct device* dev)
{
    struct sht40_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sht40_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sht40_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*sht40_ioctl_fn_t)(struct sht40_device* d, void* arg, size_t arg_len, uint32_t ms);
struct sht40_ioctl_map { sht40_ioctl_fn_t handler; };


static int sht40_cmd_read(struct sht40_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t cmd = 0xFD;
    uint8_t raw[6];
    struct sht40_sample* o = (struct sht40_sample*)arg;
    int r;
    uint16_t t, h;
    if (!d->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    r = sht40_i2c_wr(d, &cmd, 1, to);
    if (r != VFS_OK)
        return r;
    osal_delay_ms(10);
    r = sht40_i2c_rd(d, raw, 6, to);
    if (r != VFS_OK)
        return r;
    t = (uint16_t)((raw[0] << 8) | raw[1]);
    h = (uint16_t)((raw[3] << 8) | raw[4]);
    o->temp_c_x100 = (int16_t)((((int32_t)t * 17500) / 65535) - 4500);
    o->rh_x100 = (uint16_t)(((uint32_t)h * 12500U) / 65535U);
    if (o->rh_x100 > 10000U)
        o->rh_x100 = 10000U;
    return VFS_OK;
}


static const struct sht40_ioctl_map s_sht40_map[SHT40_CMD_COUNT] = {
    [SHT40_CMD_READ_TEMP_RH - SHT40_CMD_BASE - 1] = { sht40_cmd_read },
};

static int sht40_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sht40_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sht40_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SHT40_CMD_BASE;
    if (off < 1 || off > SHT40_CMD_COUNT || !s_sht40_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_sht40_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sht40_fops = {
    .open  = sht40_open,
    .close = sht40_close,
    .ioctl = sht40_ioctl,
};

static int sht40_probe(struct device* dev)
{
    struct sht40_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sht40_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_sht40_pool[pool_idx];
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
    d->ops = sht40_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sht40_pool_ctrl, pool_idx));
    return ret;
}

static int sht40_remove(struct device* dev)
{
    struct sht40_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = sht40_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_sht40_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    sht40_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sht40_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(sht40, "sensirion,sht40", sht40_probe, sht40_remove)
