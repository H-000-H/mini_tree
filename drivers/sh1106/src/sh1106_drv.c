/* SPDX-License-Identifier: Apache-2.0 */
#include "sh1106_drv.h"
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

#ifndef DTC_GEN_COUNT_SINOWEALTH_SH1106
#define DTC_GEN_COUNT_SINOWEALTH_SH1106  1
#endif
#define SH1106_POOL_COUNT  DTC_GEN_COUNT_SINOWEALTH_SH1106

struct sh1106_device
{
    struct file_operations ops;
    struct device*         i2c_dev;

    int                    hw_ready;
};

static struct sh1106_device s_sh1106_pool[SH1106_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_sh1106_used[SH1106_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_sh1106_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "sh1106";

pre_execution(160)
static void sh1106_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_sh1106_pool_ctrl, s_sh1106_used, SH1106_POOL_COUNT));
}

static struct sh1106_device* sh1106_get_drvdata(struct device* dev)
{
    return (struct sh1106_device*)device_get_priv(dev);
}


static int sh1106_i2c_wr(struct sh1106_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}


static int sh1106_hw_create(struct sh1106_device* d)
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

static void sh1106_hw_destroy(struct sh1106_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->i2c_dev)
        COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;
}

static int sh1106_open(struct device* dev, void* arg)
{
    struct sh1106_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sh1106_get_drvdata(dev);
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
        ret = sh1106_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int sh1106_close(struct device* dev)
{
    struct sh1106_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sh1106_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        sh1106_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*sh1106_ioctl_fn_t)(struct sh1106_device* d, void* arg, size_t arg_len, uint32_t ms);
struct sh1106_ioctl_map { sh1106_ioctl_fn_t handler; };


static int sh1106_cmd_byte(struct sh1106_device* d, uint8_t ctrl, uint8_t v, uint32_t to)
{
    uint8_t buf[2] = {ctrl, v};
    return sh1106_i2c_wr(d, buf, 2, to);
}

static int sh1106_set_page_col(struct sh1106_device* d, uint8_t page, uint32_t to)
{
    uint8_t col = SH1106_COL_OFFSET;
int r = sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD, (uint8_t)(SH1106_REG_SET_PAGE | (page & 0x07U)), to);
    if (r != VFS_OK)
        return r;
    r = sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD,
                        (uint8_t)(SH1106_REG_SET_COL_LO | (col & 0x0FU)), to);
    if (r != VFS_OK)
        return r;
    return sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD,
                           (uint8_t)(SH1106_REG_SET_COL_HI | ((col >> 4) & 0x0FU)), to);
}

static int sh1106_cmd_init(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    static const uint8_t seq[] = {
        SH1106_REG_DISPLAY_OFF,
        SH1106_REG_CLK_DIV, SH1106_VAL_CLK_DIV,
        SH1106_REG_MUX_RATIO, SH1106_VAL_MUX_63,
        SH1106_REG_DISP_OFFSET, SH1106_VAL_OFFSET_0,
        SH1106_REG_START_LINE,
        SH1106_REG_CHARGE_PUMP, SH1106_VAL_CHARGE_ON,
        SH1106_REG_MEM_MODE, SH1106_VAL_HORIZ_ADDR,
        SH1106_REG_SEG_REMAP, SH1106_REG_COM_SCAN_DEC,
        SH1106_REG_COM_PINS, SH1106_VAL_COM_PINS,
        SH1106_REG_SET_CONTRAST, SH1106_VAL_CONTRAST,
        SH1106_REG_PRECHARGE, SH1106_VAL_PRECHARGE,
        SH1106_REG_VCOM_DETECT, SH1106_VAL_VCOM,
        SH1106_REG_ENTIRE_ON, SH1106_REG_NORMAL_DISP,
        SH1106_REG_DISPLAY_ON
    };
    size_t i;
    COMPAT_IGNORE_RESULT(arg);
    COMPAT_IGNORE_RESULT(len);
    if (!d->hw_ready)
        return VFS_ERR_INVAL;
    for (i = 0; i < sizeof(seq); i++)
    {
        int r = sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD, seq[i], to ? to : 100U);
        if (r != VFS_OK)
            return r;
    }
    return VFS_OK;
}

static int sh1106_cmd_fill(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    uint8_t v;
    int p;
    uint32_t t = to ? to : 100U;
    uint8_t page_buf[1 + SH1106_WIDTH];
    size_t i;
    if (!d->hw_ready || !arg || len != sizeof(uint8_t))
        return VFS_ERR_INVAL;
    v = *(uint8_t*)arg;
    page_buf[0] = SH1106_I2C_CTRL_DATA;
    for (i = 1; i < sizeof(page_buf); i++)
        page_buf[i] = v;
    for (p = 0; p < SH1106_PAGES; p++)
    {
        int r = sh1106_set_page_col(d, (uint8_t)p, t);
        if (r != VFS_OK)
            return r;
        r = sh1106_i2c_wr(d, page_buf, sizeof(page_buf), t);
        if (r != VFS_OK)
            return r;
    }
    return VFS_OK;
}

static int sh1106_cmd_get_info(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    struct sh1106_info* info = (struct sh1106_info*)arg;
    COMPAT_IGNORE_RESULT(d);
    COMPAT_IGNORE_RESULT(to);
    if (!info || len != sizeof(*info))
        return VFS_ERR_INVAL;
    info->width      = SH1106_WIDTH;
    info->height     = SH1106_HEIGHT;
    info->pages      = SH1106_PAGES;
    info->fb_size    = SH1106_FB_SIZE;
    info->col_offset = SH1106_COL_OFFSET;
    return VFS_OK;
}

static int sh1106_cmd_write_cmd(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    struct sh1106_byte* a = (struct sh1106_byte*)arg;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    return sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD, a->value, to ? to : 100U);
}

static int sh1106_cmd_write_data(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    struct sh1106_data* a = (struct sh1106_data*)arg;
    uint8_t chunk[1 + 64];
    size_t off = 0;
    uint32_t t = to ? to : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a) || !a->buf || a->len == 0U)
        return VFS_ERR_INVAL;
    chunk[0] = SH1106_I2C_CTRL_DATA;
    while (off < a->len)
    {
        size_t n = a->len - off;
        if (n > 64U)
            n = 64U;
        COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&chunk[1], &a->buf[off], n));
        if (sh1106_i2c_wr(d, chunk, n + 1U, t) != VFS_OK)
            return VFS_ERR_IO;
        off += n;
    }
    return VFS_OK;
}

static int sh1106_cmd_flush_fb(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    struct sh1106_fb* a = (struct sh1106_fb*)arg;
    int page;
    uint32_t t = to ? to : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a) || !a->buf || a->len != SH1106_FB_SIZE)
        return VFS_ERR_INVAL;
    for (page = 0; page < SH1106_PAGES; page++)
    {
        struct sh1106_data slice;
        if (sh1106_set_page_col(d, (uint8_t)page, t) != VFS_OK)
            return VFS_ERR_IO;
        slice.buf = &a->buf[page * SH1106_WIDTH];
        slice.len = SH1106_WIDTH;
        if (sh1106_cmd_write_data(d, &slice, sizeof(slice), t) != VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

static int sh1106_cmd_set_contrast(struct sh1106_device* d, void* arg, size_t len, uint32_t to)
{
    struct sh1106_contrast* a = (struct sh1106_contrast*)arg;
    uint32_t t = to ? to : 100U;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    if (sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD, SH1106_REG_SET_CONTRAST, t) != VFS_OK)
        return VFS_ERR_IO;
    return sh1106_cmd_byte(d, SH1106_I2C_CTRL_CMD, a->value, t);
}

static const struct sh1106_ioctl_map s_sh1106_map[SH1106_CMD_COUNT] = {
    [SH1106_CMD_INIT - SH1106_CMD_BASE - 1]         = { sh1106_cmd_init },
    [SH1106_CMD_FILL - SH1106_CMD_BASE - 1]         = { sh1106_cmd_fill },
    [SH1106_CMD_GET_INFO - SH1106_CMD_BASE - 1]     = { sh1106_cmd_get_info },
    [SH1106_CMD_WRITE_CMD - SH1106_CMD_BASE - 1]    = { sh1106_cmd_write_cmd },
    [SH1106_CMD_WRITE_DATA - SH1106_CMD_BASE - 1]   = { sh1106_cmd_write_data },
    [SH1106_CMD_FLUSH_FB - SH1106_CMD_BASE - 1]     = { sh1106_cmd_flush_fb },
    [SH1106_CMD_SET_CONTRAST - SH1106_CMD_BASE - 1] = { sh1106_cmd_set_contrast },
};

static int sh1106_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct sh1106_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = sh1106_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)SH1106_CMD_BASE;
    if (off < 1 || off > SH1106_CMD_COUNT || !s_sh1106_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_sh1106_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations sh1106_fops = {
    .open  = sh1106_open,
    .close = sh1106_close,
    .ioctl = sh1106_ioctl,
};

static int sh1106_probe(struct device* dev)
{
    struct sh1106_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_sh1106_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_sh1106_pool[pool_idx];
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
    d->ops = sh1106_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sh1106_pool_ctrl, pool_idx));
    return ret;
}

static int sh1106_remove(struct device* dev)
{
    struct sh1106_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = sh1106_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_sh1106_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    sh1106_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_sh1106_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(sh1106, "sinowealth,sh1106", sh1106_probe, sh1106_remove)
