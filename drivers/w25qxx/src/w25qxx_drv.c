/* SPDX-License-Identifier: Apache-2.0 */
#include "w25qxx_drv.h"
#include "vfs-spi.h"
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

#ifndef DTC_GEN_COUNT_WINBOND_W25QXX
#define DTC_GEN_COUNT_WINBOND_W25QXX  1
#endif
#define W25QXX_POOL_COUNT  DTC_GEN_COUNT_WINBOND_W25QXX

struct w25qxx_device
{
    struct file_operations ops;
    struct device* spi_dev;

    int                    hw_ready;
};

static struct w25qxx_device s_w25qxx_pool[W25QXX_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_w25qxx_used[W25QXX_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_w25qxx_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "w25qxx";

pre_execution(160)
static void w25qxx_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_w25qxx_pool_ctrl, s_w25qxx_used, W25QXX_POOL_COUNT));
}

static struct w25qxx_device* w25qxx_get_drvdata(struct device* dev)
{
    return (struct w25qxx_device*)device_get_priv(dev);
}


static int w25qxx_spi_xfer(struct w25qxx_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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


static int w25qxx_hw_create(struct w25qxx_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->spi_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void w25qxx_hw_destroy(struct w25qxx_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->spi_dev) COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    d->hw_ready = 0;

}

static int w25qxx_open(struct device* dev, void* arg)
{
    struct w25qxx_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = w25qxx_get_drvdata(dev);
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
        ret = w25qxx_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int w25qxx_close(struct device* dev)
{
    struct w25qxx_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = w25qxx_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        w25qxx_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*w25qxx_ioctl_fn_t)(struct w25qxx_device* d, void* arg, size_t arg_len, uint32_t ms);
struct w25qxx_ioctl_map { w25qxx_ioctl_fn_t handler; };


static int w25qxx_cmd_jedec(struct w25qxx_device* d, void* arg, size_t len, uint32_t to)
{
    uint8_t tx[4]={0x9F, 0, 0, 0}, rx[4]={0}; struct w25qxx_jedec* j=(struct w25qxx_jedec*)arg;
    if(!d->hw_ready||!j||len!=sizeof(*j)) return VFS_ERR_INVAL;
    if(w25qxx_spi_xfer(d, tx, rx, 4, to)!=VFS_OK) return VFS_ERR_IO;
    j->id[0]=rx[1]; j->id[1]=rx[2]; j->id[2]=rx[3]; return VFS_OK;
}
static const struct w25qxx_ioctl_map s_w25qxx_map[W25QXX_CMD_COUNT] = {
    [W25QXX_CMD_READ_JEDEC_ID - W25QXX_CMD_BASE - 1] = { w25qxx_cmd_jedec },
};


static int w25qxx_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct w25qxx_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = w25qxx_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)W25QXX_CMD_BASE;
    if (off < 1 || off > W25QXX_CMD_COUNT || !s_w25qxx_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_w25qxx_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations w25qxx_fops =
{
    .open  = w25qxx_open,
    .close = w25qxx_close,
    .ioctl = w25qxx_ioctl,
};

static int w25qxx_probe(struct device* dev)
{
    struct w25qxx_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_w25qxx_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_w25qxx_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = w25qxx_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_w25qxx_pool_ctrl, pool_idx));
    return ret;
}

static int w25qxx_remove(struct device* dev)
{
    struct w25qxx_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = w25qxx_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_w25qxx_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    w25qxx_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_w25qxx_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(w25qxx, "winbond,w25qxx", w25qxx_probe, w25qxx_remove)
