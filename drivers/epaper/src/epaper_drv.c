/* SPDX-License-Identifier: Apache-2.0 */
#include "epaper_drv.h"
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

#ifndef DTC_GEN_COUNT_GOODDISPLAY_EPAPER
#define DTC_GEN_COUNT_GOODDISPLAY_EPAPER  1
#endif
#define EPAPER_POOL_COUNT  DTC_GEN_COUNT_GOODDISPLAY_EPAPER

struct epaper_device
{
    struct file_operations ops;
    struct device* spi_dev;
    struct device* dc_dev;
    struct device* rst_dev;
    struct device* busy_dev;
    struct vfs_gpio_arg dc_gpio;
    struct vfs_gpio_arg rst_gpio;
    struct vfs_gpio_arg busy_gpio;

    int                    hw_ready;
};

static struct epaper_device s_epaper_pool[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_epaper_used[EPAPER_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_epaper_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "epaper";

pre_execution(160)
static void epaper_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_epaper_pool_ctrl, s_epaper_used, EPAPER_POOL_COUNT));
}

static struct epaper_device* epaper_get_drvdata(struct device* dev)
{
    return (struct epaper_device*)device_get_priv(dev);
}


static int epaper_spi_xfer(struct epaper_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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

static int epaper_dc(struct epaper_device* d, int data)
{
    d->dc_gpio.level = data ? 1 : 0;
    return vfs_gpio_set_level(&d->dc_gpio);
}
static int epaper_wait_busy(struct epaper_device* d, uint32_t to)
{
    uint32_t e=0; int r;
    while (e <= to) {
        r = vfs_gpio_get_level(&d->busy_gpio);
        if (r != VFS_OK) return r;
        if (d->busy_gpio.level == 0) return VFS_OK;
        osal_delay_ms(1); e++;
    }
    return VFS_ERR_BUSY;
}


static int epaper_hw_create(struct epaper_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r;
      r = device_open(d->spi_dev, NULL); if (r != VFS_OK) return r;
      r = device_open(d->dc_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->dc_dev, GPIO_CMD_GET_LEVEL, &d->dc_gpio, sizeof(d->dc_gpio), 0);
      if (r != VFS_OK) return r;
      r = device_open(d->rst_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->rst_dev, GPIO_CMD_GET_LEVEL, &d->rst_gpio, sizeof(d->rst_gpio), 0);
      if (r != VFS_OK) return r;
      r = device_open(d->busy_dev, NULL); if (r != VFS_OK) return r;
      r = device_ioctl(d->busy_dev, GPIO_CMD_GET_LEVEL, &d->busy_gpio, sizeof(d->busy_gpio), 0);
      if (r != VFS_OK) return r;
    }
    d->hw_ready = 1; return VFS_OK;

}

static void epaper_hw_destroy(struct epaper_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->spi_dev) COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    if (d->dc_dev) COMPAT_IGNORE_RESULT(device_close(d->dc_dev));
    if (d->rst_dev) COMPAT_IGNORE_RESULT(device_close(d->rst_dev));
    if (d->busy_dev) COMPAT_IGNORE_RESULT(device_close(d->busy_dev));
    d->hw_ready = 0;

}

static int epaper_open(struct device* dev, void* arg)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(dev);
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
        ret = epaper_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int epaper_close(struct device* dev)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        epaper_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*epaper_ioctl_fn_t)(struct epaper_device* d, void* arg, size_t arg_len, uint32_t ms);
struct epaper_ioctl_map { epaper_ioctl_fn_t handler; };


static int epaper_cmd_init(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(len);
    d->rst_gpio.level = 0; vfs_gpio_set_level(&d->rst_gpio);
    osal_delay_ms(EPAPER_RESET_HOLD_MS);
    d->rst_gpio.level = 1; vfs_gpio_set_level(&d->rst_gpio);
    osal_delay_ms(EPAPER_RESET_HOLD_MS);
    return epaper_wait_busy(d, ms ? ms : 500U);
}
static int epaper_cmd_clear(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    uint8_t z=0x00;
    COMPAT_IGNORE_RESULT(arg); COMPAT_IGNORE_RESULT(len);
    epaper_dc(d, 1); epaper_spi_xfer(d, &z, NULL, 1, ms);
    return epaper_wait_busy(d, ms ? ms : EPAPER_BUSY_TIMEOUT_MS);
}
static int epaper_cmd_draw(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    struct epaper_bitmap* bm = (struct epaper_bitmap*)arg;
    if (!bm || len != sizeof(*bm) || !bm->data || !bm->len) return VFS_ERR_INVAL;
    epaper_dc(d, 1);
    if (epaper_spi_xfer(d, bm->data, NULL, bm->len, ms)!=VFS_OK) return VFS_ERR_IO;
    return epaper_wait_busy(d, ms ? ms : EPAPER_BUSY_TIMEOUT_MS);
}
static int epaper_cmd_get_info(struct epaper_device* d, void* arg, size_t len, uint32_t ms)
{
    struct epaper_info* info = (struct epaper_info*)arg;
    COMPAT_IGNORE_RESULT(d);
    COMPAT_IGNORE_RESULT(ms);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width  = EPAPER_DEFAULT_WIDTH;
    info->height = EPAPER_DEFAULT_HEIGHT;
    info->bpp    = EPAPER_DEFAULT_BPP;
    return VFS_OK;
}
static const struct epaper_ioctl_map s_epaper_map[EPAPER_CMD_COUNT] = {
    [EPAPER_CMD_INIT - EPAPER_CMD_BASE - 1] = { epaper_cmd_init },
    [EPAPER_CMD_CLEAR - EPAPER_CMD_BASE - 1] = { epaper_cmd_clear },
    [EPAPER_CMD_DRAW_BITMAP - EPAPER_CMD_BASE - 1] = { epaper_cmd_draw },
    [EPAPER_CMD_GET_INFO - EPAPER_CMD_BASE - 1] = { epaper_cmd_get_info },
};


static int epaper_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)EPAPER_CMD_BASE;
    if (off < 1 || off > EPAPER_CMD_COUNT || !s_epaper_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_epaper_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations epaper_fops =
{
    .open  = epaper_open,
    .close = epaper_close,
    .ioctl = epaper_ioctl,
};

static int epaper_probe(struct device* dev)
{
    struct epaper_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_epaper_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_epaper_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev) { ret = VFS_ERR_NODEV; goto err; }
    d->dc_dev = device_get_phandle_dev(dev, "dc-gpio");
    d->rst_dev = device_get_phandle_dev(dev, "reset-gpio");
    d->busy_dev = device_get_phandle_dev(dev, "busy-gpio");
    if (IS_ERR(d->dc_dev) || IS_ERR(d->rst_dev) || IS_ERR(d->busy_dev))
    { ret = VFS_ERR_INVAL; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = epaper_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, pool_idx));
    return ret;
}

static int epaper_remove(struct device* dev)
{
    struct epaper_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = epaper_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_epaper_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    epaper_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_epaper_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(epaper, "gooddisplay,epaper", epaper_probe, epaper_remove)
