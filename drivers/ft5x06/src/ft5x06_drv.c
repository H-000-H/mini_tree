/* SPDX-License-Identifier: Apache-2.0 */
#include "ft5x06_drv.h"
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

#ifndef DTC_GEN_COUNT_FOCALTECH_FT5X06
#define DTC_GEN_COUNT_FOCALTECH_FT5X06  1
#endif
#define FT5X06_POOL_COUNT  DTC_GEN_COUNT_FOCALTECH_FT5X06

struct ft5x06_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct ft5x06_device s_ft5x06_pool[FT5X06_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_ft5x06_used[FT5X06_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_ft5x06_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "ft5x06";

pre_execution(160)
static void ft5x06_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ft5x06_pool_ctrl, s_ft5x06_used, FT5X06_POOL_COUNT));
}

static struct ft5x06_device* ft5x06_get_drvdata(struct device* dev)
{
    return (struct ft5x06_device*)device_get_priv(dev);
}


static int ft5x06_i2c_wr(struct ft5x06_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

static int ft5x06_i2c_rd(struct ft5x06_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int ft5x06_hw_create(struct ft5x06_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->i2c_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void ft5x06_hw_destroy(struct ft5x06_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev) COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;

}

static int ft5x06_open(struct device* dev, void* arg)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(dev);
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
        ret = ft5x06_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int ft5x06_close(struct device* dev)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ft5x06_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*ft5x06_ioctl_fn_t)(struct ft5x06_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ft5x06_ioctl_map { ft5x06_ioctl_fn_t handler; };


static int ft5x06_cmd_touch(struct ft5x06_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t reg=0x02; uint8_t raw[6]; struct ft5x06_touch* t=(struct ft5x06_touch*)arg;
    if(!d->hw_ready||!t||len!=sizeof(*t)) return VFS_ERR_INVAL;
    if(ft5x06_i2c_wr(d, &reg, 1, to)!=VFS_OK) return VFS_ERR_IO;
    if(ft5x06_i2c_rd(d, raw, 6, to)!=VFS_OK) return VFS_ERR_IO;
    t->points=(raw[0]&0x0FU); t->x=(uint16_t)(((raw[1]&0x0FU)<<8)|raw[2]);
    t->y=(uint16_t)(((raw[3]&0x0FU)<<8)|raw[4]); return VFS_OK;
}
static const struct ft5x06_ioctl_map s_ft5x06_map[FT5X06_CMD_COUNT] = {
    [FT5X06_CMD_READ_TOUCH - FT5X06_CMD_BASE - 1] = { ft5x06_cmd_touch },
};


static int ft5x06_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)FT5X06_CMD_BASE;
    if (off < 1 || off > FT5X06_CMD_COUNT || !s_ft5x06_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ft5x06_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ft5x06_fops =
{
    .open  = ft5x06_open,
    .close = ft5x06_close,
    .ioctl = ft5x06_ioctl,
};

static int ft5x06_probe(struct device* dev)
{
    struct ft5x06_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ft5x06_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ft5x06_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = ft5x06_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, pool_idx));
    return ret;
}

static int ft5x06_remove(struct device* dev)
{
    struct ft5x06_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = ft5x06_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ft5x06_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ft5x06_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ft5x06_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ft5x06, "focaltech,ft5x06", ft5x06_probe, ft5x06_remove)
