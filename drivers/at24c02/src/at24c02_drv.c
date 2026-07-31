/* SPDX-License-Identifier: Apache-2.0 */
#include "at24c02_drv.h"
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

#ifndef DTC_GEN_COUNT_ATMEL_AT24C02
#define DTC_GEN_COUNT_ATMEL_AT24C02  1
#endif
#define AT24C02_POOL_COUNT  DTC_GEN_COUNT_ATMEL_AT24C02

struct at24c02_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct at24c02_device s_at24c02_pool[AT24C02_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_at24c02_used[AT24C02_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_at24c02_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "at24c02";

pre_execution(160)
static void at24c02_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_at24c02_pool_ctrl, s_at24c02_used, AT24C02_POOL_COUNT));
}

static struct at24c02_device* at24c02_get_drvdata(struct device* dev)
{
    return (struct at24c02_device*)device_get_priv(dev);
}


static int at24c02_i2c_wr(struct at24c02_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

static int at24c02_i2c_rd(struct at24c02_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int at24c02_hw_create(struct at24c02_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->i2c_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void at24c02_hw_destroy(struct at24c02_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev) COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;

}

static int at24c02_open(struct device* dev, void* arg)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(dev);
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

static int at24c02_close(struct device* dev)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
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

typedef int (*at24c02_ioctl_fn_t)(struct at24c02_device* d, void* arg, size_t arg_len, uint32_t ms);
struct at24c02_ioctl_map { at24c02_ioctl_fn_t handler; };


static int at24c02_cmd_read(struct at24c02_device* d, void* arg, size_t len, uint32_t to)
{
    struct at24c02_io_arg* io=(struct at24c02_io_arg*)arg; uint8_t a;
    if(!d->hw_ready||!io||len!=sizeof(*io)||!io->buf||!io->len) return VFS_ERR_INVAL;
    if((uint32_t)io->offset+io->len>AT24C02_SIZE) return VFS_ERR_INVAL;
    a=io->offset; if(at24c02_i2c_wr(d, &a, 1, to)!=VFS_OK) return VFS_ERR_IO;
    return at24c02_i2c_rd(d, io->buf, io->len, to);
}
static int at24c02_cmd_write(struct at24c02_device* d, void* arg, size_t len, uint32_t to)
{
    struct at24c02_io_arg* io=(struct at24c02_io_arg*)arg; uint8_t f[17]; size_t c, o=0;
    if(!d->hw_ready||!io||len!=sizeof(*io)||!io->buf||!io->len) return VFS_ERR_INVAL;
    if((uint32_t)io->offset+io->len>AT24C02_SIZE) return VFS_ERR_INVAL;
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
    [AT24C02_CMD_READ - AT24C02_CMD_BASE - 1] = { at24c02_cmd_read },
    [AT24C02_CMD_WRITE - AT24C02_CMD_BASE - 1] = { at24c02_cmd_write },
};


static int at24c02_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
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

static const struct file_operations at24c02_fops =
{
    .open  = at24c02_open,
    .close = at24c02_close,
    .ioctl = at24c02_ioctl,
};

static int at24c02_probe(struct device* dev)
{
    struct at24c02_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_at24c02_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_at24c02_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = at24c02_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_at24c02_pool_ctrl, pool_idx));
    return ret;
}

static int at24c02_remove(struct device* dev)
{
    struct at24c02_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = at24c02_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_at24c02_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
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
