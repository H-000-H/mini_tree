/* SPDX-License-Identifier: Apache-2.0 */
#include "ssd1306_drv.h"
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

#ifndef DTC_GEN_COUNT_SOLOMON_SSD1306
#define DTC_GEN_COUNT_SOLOMON_SSD1306  1
#endif
#define SSD1306_POOL_COUNT  DTC_GEN_COUNT_SOLOMON_SSD1306

struct ssd1306_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct ssd1306_device s_ssd1306_pool[SSD1306_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_ssd1306_used[SSD1306_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_ssd1306_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "ssd1306";

pre_execution(160)
static void ssd1306_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_ssd1306_pool_ctrl, s_ssd1306_used, SSD1306_POOL_COUNT));
}

static struct ssd1306_device* ssd1306_get_drvdata(struct device* dev)
{
    return (struct ssd1306_device*)device_get_priv(dev);
}


static int ssd1306_i2c_wr(struct ssd1306_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}


static int ssd1306_hw_create(struct ssd1306_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->i2c_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void ssd1306_hw_destroy(struct ssd1306_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev) COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;

}

static int ssd1306_open(struct device* dev, void* arg)
{
    struct ssd1306_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ssd1306_get_drvdata(dev);
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
        ret = ssd1306_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int ssd1306_close(struct device* dev)
{
    struct ssd1306_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ssd1306_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        ssd1306_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*ssd1306_ioctl_fn_t)(struct ssd1306_device* d, void* arg, size_t arg_len, uint32_t ms);
struct ssd1306_ioctl_map { ssd1306_ioctl_fn_t handler; };


static int ssd1306_wr_ctrl(struct ssd1306_device* d, uint8_t ctrl, uint8_t v, uint32_t to)
{
    uint8_t tx[2] = {ctrl, v};
    return ssd1306_i2c_wr(d, tx, 2, to);
}

static int ssd1306_cmd_init(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    static const uint8_t seq[] = {
        SSD1306_REG_DISPLAY_OFF,
        SSD1306_REG_CLK_DIV, SSD1306_VAL_CLK_DIV,
        SSD1306_REG_MUX_RATIO, SSD1306_VAL_MUX_63,
        SSD1306_REG_DISP_OFFSET, SSD1306_VAL_OFFSET_0,
        SSD1306_REG_START_LINE,
        SSD1306_REG_CHARGE_PUMP, SSD1306_VAL_CHARGE_ON,
        SSD1306_REG_MEM_MODE, SSD1306_VAL_HORIZ_ADDR,
        SSD1306_REG_SEG_REMAP, SSD1306_REG_COM_SCAN_DEC,
        SSD1306_REG_COM_PINS, SSD1306_VAL_COM_PINS,
        SSD1306_REG_SET_CONTRAST, SSD1306_VAL_CONTRAST,
        SSD1306_REG_PRECHARGE, SSD1306_VAL_PRECHARGE,
        SSD1306_REG_VCOM_DETECT, SSD1306_VAL_VCOM,
        SSD1306_REG_ENTIRE_ON, SSD1306_REG_NORMAL_DISP,
        SSD1306_REG_DISPLAY_ON
    };
    size_t i;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!d->hw_ready)
        return VFS_ERR_IO;
    for (i = 0; i < sizeof(seq); i++)
    {
        int r = ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, seq[i], ms ? ms : 100U);
        if (r != VFS_OK)
            return r;
    }
    return VFS_OK;
}

static int ssd1306_cmd_fill(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    uint8_t page_buf[1 + SSD1306_WIDTH];
    size_t i;
    int page;
    int v;
    uint32_t to = ms ? ms : 100U;
    if (!d->hw_ready || !arg || len != sizeof(int))
        return VFS_ERR_INVAL;
    v = *(int*)arg;
    page_buf[0] = SSD1306_I2C_CTRL_DATA;
    for (i = 1; i < sizeof(page_buf); i++)
        page_buf[i] = (uint8_t)v;
    for (page = 0; page < SSD1306_PAGES; page++)
    {
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD,
                            (uint8_t)(SSD1306_REG_SET_PAGE | page), to) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_LO, to) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_HI, to) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_i2c_wr(d, page_buf, sizeof(page_buf), to) != VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

static int ssd1306_cmd_draw(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_draw* dr = (struct ssd1306_draw*)arg;
    if (!d->hw_ready || !dr || len != sizeof(*dr) || !dr->buf || dr->len == 0U)
        return VFS_ERR_INVAL;
    return ssd1306_i2c_wr(d, dr->buf, dr->len, ms ? ms : 100U);
}

static int ssd1306_cmd_get_info(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_info* info = (struct ssd1306_info*)arg;
    COMPAT_IGNORE_RESULT(d);
    COMPAT_IGNORE_RESULT(ms);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width   = SSD1306_WIDTH;
    info->height  = SSD1306_HEIGHT;
    info->pages   = SSD1306_PAGES;
    info->fb_size = SSD1306_FB_SIZE;
    return VFS_OK;
}

static int ssd1306_cmd_write_cmd(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_byte* a = (struct ssd1306_byte*)arg;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    return ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, a->value, ms ? ms : 100U);
}

static int ssd1306_cmd_write_data(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_data* a = (struct ssd1306_data*)arg;
    uint8_t chunk[1 + 64];
    size_t off = 0;
    uint32_t to = ms ? ms : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a) || !a->buf || a->len == 0U)
        return VFS_ERR_INVAL;
    chunk[0] = SSD1306_I2C_CTRL_DATA;
    while (off < a->len)
    {
        size_t n = a->len - off;
        if (n > 64U)
            n = 64U;
        COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&chunk[1], &a->buf[off], n));
        if (ssd1306_i2c_wr(d, chunk, n + 1U, to) != VFS_OK)
            return VFS_ERR_IO;
        off += n;
    }
    return VFS_OK;
}

static int ssd1306_cmd_flush_fb(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_fb* a = (struct ssd1306_fb*)arg;
    int page;
    uint32_t to = ms ? ms : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a) || !a->buf || a->len != SSD1306_FB_SIZE)
        return VFS_ERR_INVAL;
    for (page = 0; page < SSD1306_PAGES; page++)
    {
        struct ssd1306_data slice;
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD,
                            (uint8_t)(SSD1306_REG_SET_PAGE | page), to) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_LO, to) != VFS_OK)
            return VFS_ERR_IO;
        if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_COL_HI, to) != VFS_OK)
            return VFS_ERR_IO;
        slice.buf = &a->buf[page * SSD1306_WIDTH];
        slice.len = SSD1306_WIDTH;
        if (ssd1306_cmd_write_data(d, &slice, sizeof(slice), to) != VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

static int ssd1306_cmd_set_contrast(struct ssd1306_device* d, void* arg, size_t len, uint32_t ms)
{
    struct ssd1306_contrast* a = (struct ssd1306_contrast*)arg;
    uint32_t to = ms ? ms : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    if (ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, SSD1306_REG_SET_CONTRAST, to) != VFS_OK)
        return VFS_ERR_IO;
    return ssd1306_wr_ctrl(d, SSD1306_I2C_CTRL_CMD, a->value, to);
}

static const struct ssd1306_ioctl_map s_ssd1306_map[SSD1306_CMD_COUNT] = {
    [SSD1306_CMD_INIT - SSD1306_CMD_BASE - 1]         = { ssd1306_cmd_init },
    [SSD1306_CMD_FILL - SSD1306_CMD_BASE - 1]         = { ssd1306_cmd_fill },
    [SSD1306_CMD_DRAW - SSD1306_CMD_BASE - 1]         = { ssd1306_cmd_draw },
    [SSD1306_CMD_GET_INFO - SSD1306_CMD_BASE - 1]     = { ssd1306_cmd_get_info },
    [SSD1306_CMD_WRITE_CMD - SSD1306_CMD_BASE - 1]    = { ssd1306_cmd_write_cmd },
    [SSD1306_CMD_WRITE_DATA - SSD1306_CMD_BASE - 1]   = { ssd1306_cmd_write_data },
    [SSD1306_CMD_FLUSH_FB - SSD1306_CMD_BASE - 1]     = { ssd1306_cmd_flush_fb },
    [SSD1306_CMD_SET_CONTRAST - SSD1306_CMD_BASE - 1] = { ssd1306_cmd_set_contrast },
};


static int ssd1306_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct ssd1306_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = ssd1306_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SSD1306_CMD_BASE;
    if (off < 1 || off > SSD1306_CMD_COUNT || !s_ssd1306_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_ssd1306_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations ssd1306_fops =
{
    .open  = ssd1306_open,
    .close = ssd1306_close,
    .ioctl = ssd1306_ioctl,
};

static int ssd1306_probe(struct device* dev)
{
    struct ssd1306_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_ssd1306_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_ssd1306_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = ssd1306_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ssd1306_pool_ctrl, pool_idx));
    return ret;
}

static int ssd1306_remove(struct device* dev)
{
    struct ssd1306_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = ssd1306_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_ssd1306_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    ssd1306_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_ssd1306_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(ssd1306, "solomon,ssd1306", ssd1306_probe, ssd1306_remove)
