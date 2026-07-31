/* SPDX-License-Identifier: Apache-2.0 */
#include "sx1278_drv.h"
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

#ifndef DTC_GEN_COUNT_SEMTECH_SX1278
#define DTC_GEN_COUNT_SEMTECH_SX1278  1
#endif
#define SX1278_POOL_COUNT  DTC_GEN_COUNT_SEMTECH_SX1278

struct sx1278_device
{
    struct file_operations ops;
    struct device* spi_dev;
    uint8_t opmode;

    int                    hw_ready;
};

static struct sx1278_device s_sx1278_pool[SX1278_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_sx1278_used[SX1278_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_sx1278_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "sx1278";

pre_execution(160)
static void sx1278_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sx1278_pool_ctrl, s_sx1278_used, SX1278_POOL_COUNT));
}

static struct sx1278_device* sx1278_get_drvdata(struct device* dev)
{
    return (struct sx1278_device*)device_get_priv(dev);
}


static int sx1278_spi_xfer(struct sx1278_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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


static int sx1278_hw_create(struct sx1278_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->spi_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void sx1278_hw_destroy(struct sx1278_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->spi_dev) COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    d->hw_ready = 0;

}

static int sx1278_open(struct device* dev, void* arg)
{
    struct sx1278_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sx1278_get_drvdata(dev);
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
        ret = sx1278_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int sx1278_close(struct device* dev)
{
    struct sx1278_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sx1278_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sx1278_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*sx1278_ioctl_fn_t)(struct sx1278_device* d, void* arg, size_t arg_len, uint32_t ms);
struct sx1278_ioctl_map { sx1278_ioctl_fn_t handler; };


static int sx1278_wr_reg(struct sx1278_device* d, uint8_t reg, uint8_t val, uint32_t to)
{
    uint8_t tx[2]={(uint8_t)(reg|0x80U), val}; return sx1278_spi_xfer(d, tx, NULL, 2, to);
}
static int sx1278_cmd_reset(struct sx1278_device* d, void* arg, size_t len, uint32_t to)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(len);
    if (sx1278_wr_reg(d, 0x01, 0x00, to) != VFS_OK)
        return VFS_ERR_IO;
    d->opmode = 0;
    return VFS_OK;
}
static int sx1278_cmd_freq(struct sx1278_device* d, void* arg, size_t len, uint32_t to)
{
    uint32_t hz; uint8_t frf[3]; uint64_t f;
    if(!arg||len!=sizeof(uint32_t)) return VFS_ERR_INVAL;
    hz=*(uint32_t*)arg; f=((uint64_t)hz<<19)/32000000ULL;
    frf[0]=(uint8_t)(f>>16); frf[1]=(uint8_t)(f>>8); frf[2]=(uint8_t)f;
    { uint8_t tx[4]={0x06|0x80U, frf[0], frf[1], frf[2]}; if(sx1278_spi_xfer(d, tx, NULL, 4, to)!=VFS_OK) return VFS_ERR_IO; }
    return VFS_OK;
}
static int sx1278_cmd_send(struct sx1278_device* d, void* arg, size_t len, uint32_t to)
{
    struct sx1278_payload* pl=(struct sx1278_payload*)arg;
    if(!pl||len!=sizeof(*pl)||!pl->data||!pl->len) return VFS_ERR_INVAL;
    if(sx1278_wr_reg(d, 0x01, 0x83, to)!=VFS_OK) return VFS_ERR_IO;
    { uint8_t tx[257]; size_t n=pl->len; if(n>255U)n=255U; tx[0]=0x80; COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&tx[1], pl->data, n));
      if(sx1278_spi_xfer(d, tx, NULL, n+1U, to)!=VFS_OK) return VFS_ERR_IO; }
    return VFS_OK;
}
static int sx1278_cmd_recv(struct sx1278_device* d, void* arg, size_t len, uint32_t to)
{
    struct sx1278_payload* pl = (struct sx1278_payload*)arg;
    uint8_t                tx[2] = {0, 0};
    uint8_t                rx[2] = {0, 0};
    uint8_t*               out;

    COMPAT_IGNORE_RESULT(to);
    if (!pl || len != sizeof(*pl) || !pl->data || pl->len == 0U)
        return VFS_ERR_INVAL;
    if (sx1278_spi_xfer(d, tx, rx, 2, 50) != VFS_OK)
        return VFS_ERR_IO;
    out = (uint8_t*)(uintptr_t)pl->data;
    out[0] = rx[1];
    pl->len = 1U;
    return VFS_OK;
}
static const struct sx1278_ioctl_map s_sx1278_map[SX1278_CMD_COUNT] = {
    [SX1278_CMD_RESET - SX1278_CMD_BASE - 1] = { sx1278_cmd_reset },
    [SX1278_CMD_SET_FREQ - SX1278_CMD_BASE - 1] = { sx1278_cmd_freq },
    [SX1278_CMD_SEND - SX1278_CMD_BASE - 1] = { sx1278_cmd_send },
    [SX1278_CMD_RECV - SX1278_CMD_BASE - 1] = { sx1278_cmd_recv },
};


static int sx1278_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sx1278_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sx1278_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SX1278_CMD_BASE;
    if (off < 1 || off > SX1278_CMD_COUNT || !s_sx1278_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_sx1278_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sx1278_fops =
{
    .open  = sx1278_open,
    .close = sx1278_close,
    .ioctl = sx1278_ioctl,
};

static int sx1278_probe(struct device* dev)
{
    struct sx1278_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sx1278_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_sx1278_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = sx1278_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sx1278_pool_ctrl, pool_idx));
    return ret;
}

static int sx1278_remove(struct device* dev)
{
    struct sx1278_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = sx1278_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_sx1278_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    sx1278_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sx1278_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(sx1278, "semtech,sx1278", sx1278_probe, sx1278_remove)
