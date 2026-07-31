/* SPDX-License-Identifier: Apache-2.0 */
#include "aht20_drv.h"
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

#ifndef DTC_GEN_COUNT_AOSONG_AHT20
#define DTC_GEN_COUNT_AOSONG_AHT20  1
#endif
#define AHT20_POOL_COUNT  DTC_GEN_COUNT_AOSONG_AHT20

struct aht20_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct aht20_device s_aht20_pool[AHT20_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_aht20_used[AHT20_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_aht20_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "aht20";

pre_execution(160)
static void aht20_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_aht20_pool_ctrl, s_aht20_used, AHT20_POOL_COUNT));
}

static struct aht20_device* aht20_get_drvdata(struct device* dev)
{
    return (struct aht20_device*)device_get_priv(dev);
}


static int aht20_i2c_wr(struct aht20_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}
static int aht20_i2c_rd(struct aht20_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int aht20_hw_create(struct aht20_device* d)
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

static void aht20_hw_destroy(struct aht20_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

static int aht20_open(struct device* dev, void* arg)
{
    struct aht20_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = aht20_get_drvdata(dev);
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
        ret = aht20_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int aht20_close(struct device* dev)
{
    struct aht20_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = aht20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        aht20_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*aht20_ioctl_fn_t)(struct aht20_device* d, void* arg, size_t arg_len, uint32_t ms);
struct aht20_ioctl_map { aht20_ioctl_fn_t handler; };


static int aht20_cmd_read(struct aht20_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t trig[3] = {0xAC, 0x33, 0x00};
    uint8_t raw[6];
    struct aht20_sample* o = (struct aht20_sample*)arg;
    int r;
    uint32_t rh, t;
    if (!d->hw_ready || !o || len != sizeof(*o))
        return VFS_ERR_INVAL;
    r = aht20_i2c_wr(d, trig, 3, to);
    if (r != VFS_OK)
        return r;
    osal_delay_ms(80);
    r = aht20_i2c_rd(d, raw, 6, to);
    if (r != VFS_OK)
        return r;
    rh = ((uint32_t)raw[1] << 12) | ((uint32_t)raw[2] << 4) | (raw[3] >> 4);
    t = (((uint32_t)raw[3] & 0x0FU) << 16) | ((uint32_t)raw[4] << 8) | raw[5];
    o->rh_x100 = (uint16_t)((rh * 10000U) / 1048576U);
    o->temp_c_x100 = (int16_t)(((int32_t)t * 20000) / 1048576 - 5000);
    return VFS_OK;
}


static const struct aht20_ioctl_map s_aht20_map[AHT20_CMD_COUNT] = {
    [AHT20_CMD_READ_TEMP_RH - AHT20_CMD_BASE - 1] = { aht20_cmd_read },
};

static int aht20_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct aht20_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = aht20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)AHT20_CMD_BASE;
    if (off < 1 || off > AHT20_CMD_COUNT || !s_aht20_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_aht20_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations aht20_fops = {
    .open  = aht20_open,
    .close = aht20_close,
    .ioctl = aht20_ioctl,
};

static int aht20_probe(struct device* dev)
{
    struct aht20_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_aht20_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_aht20_pool[pool_idx];
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
    d->ops = aht20_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_aht20_pool_ctrl, pool_idx));
    return ret;
}

static int aht20_remove(struct device* dev)
{
    struct aht20_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = aht20_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_aht20_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    aht20_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_aht20_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(aht20, "aosong,aht20", aht20_probe, aht20_remove)
