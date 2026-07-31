/* SPDX-License-Identifier: Apache-2.0 */
#include "xpt2046_drv.h"
#include "vfs-spi.h"
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

#ifndef DTC_GEN_COUNT_TI_XPT2046
#define DTC_GEN_COUNT_TI_XPT2046  1
#endif
#define XPT2046_POOL_COUNT  DTC_GEN_COUNT_TI_XPT2046

struct xpt2046_device
{
    struct file_operations ops;
    struct device* spi_dev;
    struct device* irq_dev;
    struct vfs_gpio_arg irq_gpio;
    int has_irq;

    int                    hw_ready;
};

static struct xpt2046_device s_xpt2046_pool[XPT2046_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_xpt2046_used[XPT2046_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_xpt2046_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "xpt2046";

pre_execution(160)
static void xpt2046_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_xpt2046_pool_ctrl, s_xpt2046_used, XPT2046_POOL_COUNT));
}

static struct xpt2046_device* xpt2046_get_drvdata(struct device* dev)
{
    return (struct xpt2046_device*)device_get_priv(dev);
}


static int xpt2046_spi_xfer(struct xpt2046_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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


static int xpt2046_hw_create(struct xpt2046_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->spi_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void xpt2046_hw_destroy(struct xpt2046_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->spi_dev) COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    d->hw_ready = 0;

}

static int xpt2046_open(struct device* dev, void* arg)
{
    struct xpt2046_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = xpt2046_get_drvdata(dev);
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
        ret = xpt2046_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int xpt2046_close(struct device* dev)
{
    struct xpt2046_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = xpt2046_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        xpt2046_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*xpt2046_ioctl_fn_t)(struct xpt2046_device* d, void* arg, size_t arg_len, uint32_t ms);
struct xpt2046_ioctl_map { xpt2046_ioctl_fn_t handler; };


static int xpt2046_cmd_xy(struct xpt2046_device* d, void* arg, size_t len, uint32_t to)
{
    uint8_t tx[3]={0x90, 0, 0}, rx[3]={0}; struct xpt2046_xy* p=(struct xpt2046_xy*)arg;
    if(!d->hw_ready||!p||len!=sizeof(*p)) return VFS_ERR_INVAL;
    if(xpt2046_spi_xfer(d, tx, rx, 3, to)!=VFS_OK) return VFS_ERR_IO;
    p->x=(uint16_t)(((rx[1]<<8)|rx[2])>>3); tx[0]=0xD0;
    if(xpt2046_spi_xfer(d, tx, rx, 3, to)!=VFS_OK) return VFS_ERR_IO;
    p->y=(uint16_t)(((rx[1]<<8)|rx[2])>>3); p->pressed=1; return VFS_OK;
}
static const struct xpt2046_ioctl_map s_xpt2046_map[XPT2046_CMD_COUNT] = {
    [XPT2046_CMD_READ_XY - XPT2046_CMD_BASE - 1] = { xpt2046_cmd_xy },
};


static int xpt2046_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct xpt2046_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = xpt2046_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)XPT2046_CMD_BASE;
    if (off < 1 || off > XPT2046_CMD_COUNT || !s_xpt2046_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_xpt2046_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations xpt2046_fops =
{
    .open  = xpt2046_open,
    .close = xpt2046_close,
    .ioctl = xpt2046_ioctl,
};

static int xpt2046_probe(struct device* dev)
{
    struct xpt2046_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_xpt2046_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_xpt2046_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev) { ret = VFS_ERR_NODEV; goto err; }
    d->irq_dev = device_get_phandle_dev(dev, "irq-gpio");
    if (IS_ERR(d->irq_dev)) { d->irq_dev = NULL; d->has_irq = 0; }
    else d->has_irq = 1;

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = xpt2046_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_xpt2046_pool_ctrl, pool_idx));
    return ret;
}

static int xpt2046_remove(struct device* dev)
{
    struct xpt2046_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = xpt2046_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_xpt2046_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    xpt2046_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_xpt2046_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(xpt2046, "ti,xpt2046", xpt2046_probe, xpt2046_remove)
