/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ST7789 公共核心 — 有 CS / 无 CS 共用
 *
 * SPI: device_get_parent → SPI_CMD_TRANSFER / device_write。
 * DC/RST: vfs-gpio；背光: vfs-tim（LEDC 后端）。禁止厂商 SDK。
 */
#include "st7789_drv.h"
#include "st7789_core.h"
#include "st7789_regs.h"
#include "vfs-spi.h"
#include "vfs-gpio.h"
#include "vfs-tim.h"
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

#ifndef DTC_GEN_COUNT_SITRONIX_ST7789
#define DTC_GEN_COUNT_SITRONIX_ST7789  1
#endif
#ifndef DTC_GEN_COUNT_SITRONIX_ST7789_NOCS
#define DTC_GEN_COUNT_SITRONIX_ST7789_NOCS  1
#endif

#define ST7789_COUNT \
    (DTC_GEN_COUNT_SITRONIX_ST7789 + DTC_GEN_COUNT_SITRONIX_ST7789_NOCS)

struct st7789_device
{
    struct file_operations ops;
    struct device*         spi_dev;
    struct device*         dc_dev;
    struct device*         rst_dev;
    struct device*         bl_tim_dev;
    struct vfs_gpio_arg    dc_gpio;
    struct vfs_gpio_arg    rst_gpio;
    struct vfs_tim_arg     bl_tim;
    uint32_t               bl_arr;
    uint32_t               bl_channel;
    int                    bl_active_high;
    int                    width;
    int                    height;
    uint8_t                madctl;
    uint8_t                invert;
    uint8_t                bl_brightness;
    int                    pins_ready;
    size_t                 spi_chunk;
    uint8_t*               block_buf;
};

static struct st7789_device s_st7789_pool[ST7789_COUNT] COMPAT_ALIGNED(4);
static uint8_t              s_st7789_used[ST7789_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t          s_st7789_pool_ctrl COMPAT_ALIGNED(4);
static uint8_t s_st7789_block_buf[ST7789_COUNT][ST7789_BLOCK_BUF_SIZE] COMPAT_ALIGNED(4);

static const char* const kTag = "st7789";

pre_execution(160)
static void st7789_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_st7789_pool_ctrl, s_st7789_used, ST7789_COUNT));
}

static struct st7789_device* st7789_get_drvdata(struct device* dev)
{
    return (struct st7789_device*)device_get_priv(dev);
}

static int st7789_gpio_out(struct vfs_gpio_arg* ga, int level)
{
    if (!ga || !ga->obj)
        return VFS_OK;
    ga->level = level ? 1 : 0;
    return vfs_gpio_set_level(ga);
}

static int st7789_bind_gpio(struct device* gdev, struct vfs_gpio_arg* ga)
{
    int ret;

    if (!gdev || !ga)
        return VFS_ERR_INVAL;
    ret = device_open(gdev, NULL);
    if (ret != VFS_OK)
        return ret;
    ret = device_ioctl(gdev, GPIO_CMD_GET_LEVEL, ga, sizeof(*ga), 0);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(device_close(gdev));
        ga->obj = NULL;
    }
    return ret;
}

static int st7789_spi_write(struct st7789_device* lcd, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    size_t offset = 0;

    if (!lcd || !lcd->spi_dev || (!data && len > 0U))
        return VFS_ERR_INVAL;

    while (offset < len)
    {
        size_t chunk = len - offset;
        int    ret;

        if (chunk > lcd->spi_chunk)
            chunk = lcd->spi_chunk;

        ret = device_write(lcd->spi_dev, data + offset, chunk, timeout_ms);
        if (ret != VFS_OK)
            return ret;
        offset += chunk;
    }
    return VFS_OK;
}

static int st7789_write_cmd(struct st7789_device* lcd, uint8_t cmd, uint32_t timeout_ms)
{
    int ret = st7789_gpio_out(&lcd->dc_gpio, 0);
    if (ret != VFS_OK)
        return ret;
    return st7789_spi_write(lcd, &cmd, 1U, timeout_ms);
}

static int st7789_write_data(struct st7789_device* lcd, const uint8_t* data, size_t len, uint32_t timeout_ms)
{
    int ret = st7789_gpio_out(&lcd->dc_gpio, 1);
    if (ret != VFS_OK)
        return ret;
    return st7789_spi_write(lcd, data, len, timeout_ms);
}

static int st7789_clip_rect(const struct st7789_device* lcd, int* x, int* y, int* w, int* h)
{
    if (!lcd || !x || !y || !w || !h || *w <= 0 || *h <= 0)
        return VFS_ERR_INVAL;

    if (*x < 0)
    {
        *w += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *h += *y;
        *y = 0;
    }
    if (*x >= lcd->width || *y >= lcd->height || *w <= 0 || *h <= 0)
        return VFS_ERR_INVAL;
    if (*w > lcd->width - *x)
        *w = lcd->width - *x;
    if (*h > lcd->height - *y)
        *h = lcd->height - *y;
    return (*w > 0 && *h > 0) ? VFS_OK : VFS_ERR_INVAL;
}

static int st7789_set_window(struct st7789_device* lcd, int x, int y, int w, int h, uint32_t timeout_ms)
{
    uint16_t x0 = (uint16_t)x;
    uint16_t y0 = (uint16_t)y;
    uint16_t x1 = (uint16_t)(x + w - 1);
    uint16_t y1 = (uint16_t)(y + h - 1);
    uint8_t  col[] = {
        (uint8_t)(x0 >> 8), (uint8_t)(x0 & 0xFFU),
        (uint8_t)(x1 >> 8), (uint8_t)(x1 & 0xFFU),
    };
    uint8_t row[] = {
        (uint8_t)(y0 >> 8), (uint8_t)(y0 & 0xFFU),
        (uint8_t)(y1 >> 8), (uint8_t)(y1 & 0xFFU),
    };
    int ret;

    ret = st7789_write_cmd(lcd, ST7789_REG_CASET, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_data(lcd, col, sizeof(col), timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_cmd(lcd, ST7789_REG_RASET, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    return st7789_write_data(lcd, row, sizeof(row), timeout_ms);
}

/* 对齐 esp_lcd_panel_st7789: SLPOUT / MADCTL / COLMOD / RAMCTRL */
static int st7789_hw_init(struct st7789_device* lcd, uint32_t timeout_ms)
{
    uint8_t madctl  = lcd->madctl;
    uint8_t colmod  = ST7789_COLMOD_16BIT; /* RGB565 */
    uint8_t ramctrl[] = { ST7789_RAMCTRL_BE0, ST7789_RAMCTRL_BE1 }; /* big-endian */
    int     ret;

    if (lcd->rst_gpio.obj)
    {
        COMPAT_IGNORE_RESULT(st7789_gpio_out(&lcd->rst_gpio, 0));
        osal_delay_ms(10U);
        COMPAT_IGNORE_RESULT(st7789_gpio_out(&lcd->rst_gpio, 1));
        osal_delay_ms(10U);
    }
    else
    {
        ret = st7789_write_cmd(lcd, ST7789_REG_SWRESET, timeout_ms);
        if (ret != VFS_OK)
            return ret;
        osal_delay_ms(20U);
    }

    ret = st7789_write_cmd(lcd, ST7789_REG_SLPOUT, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    osal_delay_ms(100U);

    ret = st7789_write_cmd(lcd, ST7789_REG_MADCTL, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_data(lcd, &madctl, 1U, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    ret = st7789_write_cmd(lcd, ST7789_REG_COLMOD, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_data(lcd, &colmod, 1U, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    ret = st7789_write_cmd(lcd, ST7789_REG_RAMCTRL, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_data(lcd, ramctrl, sizeof(ramctrl), timeout_ms);
    if (ret != VFS_OK)
        return ret;

    if (lcd->invert)
    {
        ret = st7789_write_cmd(lcd, ST7789_REG_INVON, timeout_ms);
        if (ret != VFS_OK)
            return ret;
    }

    ret = st7789_write_cmd(lcd, ST7789_REG_NORON, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_cmd(lcd, ST7789_REG_DISPON, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    COMPAT_IGNORE_RESULT(st7789_gpio_out(&lcd->dc_gpio, 1));
    return VFS_OK;
}

static int st7789_apply_backlight(struct st7789_device* lcd, uint8_t brightness)
{
    uint32_t ccr;

    if (!lcd->bl_tim.obj)
        return VFS_ERR_NOTSUPP;

    ccr = ((uint32_t)brightness * lcd->bl_arr) / 255U;
    if (!lcd->bl_active_high)
        ccr = lcd->bl_arr - ccr;

    lcd->bl_tim.channel = lcd->bl_channel;
    lcd->bl_tim.arr     = lcd->bl_arr;
    lcd->bl_tim.ccr     = ccr;
    if (vfs_tim_fast_pwm_update(&lcd->bl_tim) != VFS_OK)
        return VFS_ERR_IO;
    lcd->bl_brightness = brightness;
    return VFS_OK;
}

static int st7789_do_fill_rect(struct st7789_device* lcd, int x, int y, int w, int h, uint16_t color, uint32_t timeout_ms)
{
    uint8_t hi;
    uint8_t lo;
    int     i;
    int     ret;
    size_t  fill_bytes;
    size_t  total;
    size_t  left;

    if (st7789_clip_rect(lcd, &x, &y, &w, &h) != VFS_OK)
        return VFS_ERR_INVAL;

    ret = st7789_set_window(lcd, x, y, w, h, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_cmd(lcd, ST7789_REG_RAMWR, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_gpio_out(&lcd->dc_gpio, 1);
    if (ret != VFS_OK)
        return ret;

    /* 窗已开好：按 GRAM 顺序流式写即可，不必逐行 device_write */
    fill_bytes = ST7789_BLOCK_BUF_SIZE;
    if (fill_bytes > lcd->spi_chunk)
        fill_bytes = lcd->spi_chunk;
    fill_bytes &= ~1U; /* RGB565：保持偶数字节 */
    if (fill_bytes < 2U)
        return VFS_ERR_NOSPC;

    hi = (uint8_t)(color >> 8);
    lo = (uint8_t)(color & 0xFFU);
    for (i = 0; i < (int)(fill_bytes / 2U); i++)
    {
        lcd->block_buf[i * 2]     = hi;
        lcd->block_buf[i * 2 + 1] = lo;
    }

    total = (size_t)w * (size_t)h * 2U;
    left  = total;
    while (left > 0U)
    {
        size_t n = (left < fill_bytes) ? left : fill_bytes;

        ret = st7789_spi_write(lcd, lcd->block_buf, n, timeout_ms);
        if (ret != VFS_OK)
            return ret;
        left -= n;
    }
    return VFS_OK;
}

static int st7789_do_draw_bitmap(struct st7789_device* lcd, int x, int y, int w, int h, const uint8_t* data, uint32_t timeout_ms)
{
    size_t pixels;
    int    ret;

    if (!data || w <= 0 || h <= 0)
        return VFS_ERR_INVAL;
    if (x < 0 || y < 0 || x > lcd->width - w || y > lcd->height - h)
        return VFS_ERR_INVAL;

    pixels = (size_t)w * (size_t)h;
    if (h != 0 && pixels / (size_t)h != (size_t)w)
        return VFS_ERR_INVAL;

    ret = st7789_set_window(lcd, x, y, w, h, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_write_cmd(lcd, ST7789_REG_RAMWR, timeout_ms);
    if (ret != VFS_OK)
        return ret;
    ret = st7789_gpio_out(&lcd->dc_gpio, 1);
    if (ret != VFS_OK)
        return ret;
    return st7789_spi_write(lcd, data, pixels * 2U, timeout_ms);
}

static int st7789_open(struct device* dev, void* arg)
{
    struct st7789_device* lcd;
    struct dev_lifecycle* lc;
    int                   first;
    int                   ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lcd = st7789_get_drvdata(dev);
    if (IS_ERR(lcd))
        return PTR_ERR(lcd);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = st7789_bind_gpio(lcd->dc_dev, &lcd->dc_gpio);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
        if (lcd->rst_dev)
        {
            ret = st7789_bind_gpio(lcd->rst_dev, &lcd->rst_gpio);
            if (ret != VFS_OK)
            {
                COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
                lcd->dc_gpio.obj = NULL;
                dev_lc_open_abort(lc);
                return ret;
            }
        }
        if (lcd->bl_tim_dev)
        {
            ret = device_open(lcd->bl_tim_dev, NULL);
            if (ret != VFS_OK)
            {
                if (lcd->rst_dev)
                    COMPAT_IGNORE_RESULT(device_close(lcd->rst_dev));
                COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
                lcd->dc_gpio.obj = NULL;
                lcd->rst_gpio.obj = NULL;
                dev_lc_open_abort(lc);
                return ret;
            }
            lcd->bl_tim.obj = vfs_tim_get_hal_dev(lcd->bl_tim_dev);
            if (!lcd->bl_tim.obj)
            {
                COMPAT_IGNORE_RESULT(device_close(lcd->bl_tim_dev));
                if (lcd->rst_dev)
                    COMPAT_IGNORE_RESULT(device_close(lcd->rst_dev));
                COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
                lcd->dc_gpio.obj = NULL;
                lcd->rst_gpio.obj = NULL;
                dev_lc_open_abort(lc);
                return VFS_ERR_IO;
            }
            /* 经 VFS 快路径取 ARR，禁止 product 直调 hal_tim_* */
            if (vfs_tim_fast_get_autoreload(&lcd->bl_tim) == VFS_OK &&
                lcd->bl_tim.value != 0U)
                lcd->bl_arr = lcd->bl_tim.value;
            else
                lcd->bl_arr = ST7789_BL_ARR_FALLBACK;
        }

        ret = device_open(lcd->spi_dev, NULL);
        if (ret != VFS_OK)
        {
            if (lcd->bl_tim_dev)
                COMPAT_IGNORE_RESULT(device_close(lcd->bl_tim_dev));
            if (lcd->rst_dev)
                COMPAT_IGNORE_RESULT(device_close(lcd->rst_dev));
            COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
            lcd->dc_gpio.obj = NULL;
            lcd->rst_gpio.obj = NULL;
            lcd->bl_tim.obj = NULL;
            dev_lc_open_abort(lc);
            return ret;
        }

        lcd->pins_ready = 1;
        ret = st7789_hw_init(lcd, ST7789_TIMEOUT_CMD_MS);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(device_close(lcd->spi_dev));
            if (lcd->bl_tim_dev)
                COMPAT_IGNORE_RESULT(device_close(lcd->bl_tim_dev));
            if (lcd->rst_dev)
                COMPAT_IGNORE_RESULT(device_close(lcd->rst_dev));
            COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
            lcd->dc_gpio.obj = NULL;
            lcd->rst_gpio.obj = NULL;
            lcd->bl_tim.obj = NULL;
            lcd->pins_ready = 0;
            dev_lc_open_abort(lc);
            return ret;
        }

        if (lcd->bl_tim.obj)
            COMPAT_IGNORE_RESULT(st7789_apply_backlight(lcd, lcd->bl_brightness));
    }

    dev_lc_open_end(lc);
    return VFS_OK;
}

static int st7789_close(struct device* dev)
{
    struct st7789_device* lcd;
    struct dev_lifecycle* lc;
    int                   last;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lcd = st7789_get_drvdata(dev);
    if (IS_ERR(lcd))
        return PTR_ERR(lcd);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
    {
        COMPAT_IGNORE_RESULT(st7789_write_cmd(lcd, ST7789_REG_DISPOFF, ST7789_TIMEOUT_CMD_MS));
        if (lcd->bl_tim.obj)
            COMPAT_IGNORE_RESULT(st7789_apply_backlight(lcd, 0U));
        COMPAT_IGNORE_RESULT(device_close(lcd->spi_dev));
        if (lcd->bl_tim_dev)
            COMPAT_IGNORE_RESULT(device_close(lcd->bl_tim_dev));
        if (lcd->rst_dev)
            COMPAT_IGNORE_RESULT(device_close(lcd->rst_dev));
        if (lcd->dc_dev)
            COMPAT_IGNORE_RESULT(device_close(lcd->dc_dev));
        lcd->dc_gpio.obj = NULL;
        lcd->rst_gpio.obj = NULL;
        lcd->bl_tim.obj = NULL;
        lcd->pins_ready = 0;
    }

    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*st7789_ioctl_fn_t)(struct st7789_device* lcd, void* arg,
                                 size_t arg_len, uint32_t timeout_ms);

struct st7789_ioctl_map
{
    st7789_ioctl_fn_t handler;
};

static int st7789_cmd_fill_rect(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct st7789_fill_rect_arg* a = (const struct st7789_fill_rect_arg*)arg;

    if (!lcd || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return st7789_do_fill_rect(lcd, a->x, a->y, a->w, a->h, a->color, timeout_ms);
}

static int st7789_cmd_fill_screen(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct st7789_fill_screen_arg* a =
        (const struct st7789_fill_screen_arg*)arg;

    if (!lcd || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return st7789_do_fill_rect(lcd, 0, 0, lcd->width, lcd->height, a->color,
                               timeout_ms);
}

static int st7789_cmd_draw_bitmap(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct st7789_draw_bitmap_arg* a =
        (const struct st7789_draw_bitmap_arg*)arg;

    if (!lcd || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return st7789_do_draw_bitmap(lcd, a->x, a->y, a->w, a->h, a->data, timeout_ms);
}

static int st7789_cmd_set_backlight(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct st7789_backlight_arg* a =
        (const struct st7789_backlight_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!lcd || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    return st7789_apply_backlight(lcd, a->brightness);
}

static int st7789_cmd_get_info(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct st7789_info_arg* a = (struct st7789_info_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!lcd || !a || arg_len != sizeof(*a))
        return VFS_ERR_INVAL;
    a->width        = (int16_t)lcd->width;
    a->height       = (int16_t)lcd->height;
    a->color_format = ST7789_COLOR_FORMAT_RGB565;
    return VFS_OK;
}

static int st7789_cmd_flush_area(struct st7789_device* lcd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    const struct st7789_flush_area_arg* a =
        (const struct st7789_flush_area_arg*)arg;
    int16_t w;
    int16_t h;

    if (!lcd || !a || arg_len != sizeof(*a) || !a->color_map)
        return VFS_ERR_INVAL;
    if (a->x2 < a->x1 || a->y2 < a->y1)
        return VFS_ERR_INVAL;
    w = (int16_t)(a->x2 - a->x1 + 1);
    h = (int16_t)(a->y2 - a->y1 + 1);
    return st7789_do_draw_bitmap(lcd, a->x1, a->y1, w, h, a->color_map,
                                 timeout_ms);
}

static const struct st7789_ioctl_map s_st7789_ioctl_map[ST7789_CMD_COUNT] =
{
    [ST7789_CMD_FILL_RECT - ST7789_CMD_BASE - 1]     = { st7789_cmd_fill_rect },
    [ST7789_CMD_FILL_SCREEN - ST7789_CMD_BASE - 1]   = { st7789_cmd_fill_screen },
    [ST7789_CMD_DRAW_BITMAP - ST7789_CMD_BASE - 1]   = { st7789_cmd_draw_bitmap },
    [ST7789_CMD_SET_BACKLIGHT - ST7789_CMD_BASE - 1] = { st7789_cmd_set_backlight },
    [ST7789_CMD_GET_INFO - ST7789_CMD_BASE - 1]      = { st7789_cmd_get_info },
    [ST7789_CMD_FLUSH_AREA - ST7789_CMD_BASE - 1]    = { st7789_cmd_flush_area },
};

static int st7789_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct st7789_device* lcd;
    struct dev_lifecycle* lc;
    int32_t               offset;
    int                   ret;

    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;

    lcd = st7789_get_drvdata(dev);
    if (IS_ERR(lcd))
        return PTR_ERR(lcd);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)ST7789_CMD_BASE;
    if (offset < 1 || offset > ST7789_CMD_COUNT ||
        !s_st7789_ioctl_map[offset - 1].handler)
    {
        ret = VFS_ERR_INVAL;
    }
    else
    {
        ret = s_st7789_ioctl_map[offset - 1].handler(lcd, arg, arg_len, timeout_ms);
    }

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations st7789_fops =
{
    .open  = st7789_open,
    .close = st7789_close,
    .ioctl = st7789_ioctl,
};

int st7789_probe_common(struct device* dev, int require_nocs)
{
    struct st7789_device* lcd;
    struct device*        dc_dev;
    struct device*        rst_dev;
    struct device*        bl_tim_dev;
    int                   width = 0;
    int                   height = 0;
    int                   bl_active = 1;
    int                   madctl = 0;
    int                   invert = 0;
    int                   parent_cs = 0;
    int                   max_trans = (int)ST7789_DEFAULT_CHUNK;
    int                   pool_idx;
    int                   ret;

    if (!dev)
        return VFS_ERR_INVAL;

    dc_dev = device_get_phandle_dev(dev, "dc-gpio");
    if (IS_ERR(dc_dev))
        return PTR_ERR(dc_dev);

    if (device_get_prop_int(dev, "width", &width) != VFS_OK ||
        device_get_prop_int(dev, "height", &height) != VFS_OK ||
        width <= 0 || height <= 0 || width > ST7789_MAX_WIDTH)
        return VFS_ERR_INVAL;

    rst_dev = device_get_phandle_dev(dev, "reset-gpio");
    if (IS_ERR(rst_dev))
        rst_dev = NULL;

    bl_tim_dev = device_get_phandle_dev(dev, "backlight");
    if (IS_ERR(bl_tim_dev))
        bl_tim_dev = NULL;

    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "bl-active-high", &bl_active));
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "madctl", &madctl));
    COMPAT_IGNORE_RESULT(device_get_prop_int(dev, "invert", &invert));

    pool_idx = osal_pool_claim(&s_st7789_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    lcd = &s_st7789_pool[pool_idx];
    COMPAT_MEM_SET(lcd, 0, sizeof(*lcd));

    lcd->spi_dev = device_get_parent(dev);
    if (!lcd->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err_pool;
    }

    if (require_nocs)
    {
        if (device_get_prop_int(lcd->spi_dev, "cs-pin", &parent_cs) != VFS_OK ||
            parent_cs >= 0)
        {
            SYS_LOGE(kTag, "nocs requires parent cs-pin < 0");
            ret = VFS_ERR_INVAL;
            goto err_pool;
        }
    }

    COMPAT_IGNORE_RESULT(device_get_prop_int(lcd->spi_dev, "max-trans-buffer",
                                             &max_trans));
    if (max_trans <= 0)
    {
        struct device* host = device_get_parent(lcd->spi_dev);
        if (host)
        {
            COMPAT_IGNORE_RESULT(device_get_prop_int(host, "max-trans-buffer",
                                                     &max_trans));
        }
    }
    if (max_trans <= 0)
        max_trans = (int)ST7789_DEFAULT_CHUNK;

    lcd->dc_dev         = dc_dev;
    lcd->rst_dev        = rst_dev;
    lcd->bl_tim_dev     = bl_tim_dev;
    lcd->bl_arr         = ST7789_BL_ARR_FALLBACK;
    lcd->bl_channel     = 1U;
    lcd->bl_active_high = bl_active;
    lcd->width          = width;
    lcd->height         = height;
    lcd->madctl         = (uint8_t)madctl;
    lcd->invert         = invert ? 1U : 0U;
    lcd->bl_brightness  = 255U;
    lcd->spi_chunk      = (size_t)max_trans;
    lcd->block_buf      = s_st7789_block_buf[pool_idx];

    if (device_set_priv(dev, lcd) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    lcd->ops = st7789_fops;
    dev->ops = &lcd->ops;

    SYS_LOGI(kTag, "probe OK: pool=%d %dx%d dc=%s bl=%s nocs=%d",
             pool_idx, width, height, device_get_name(dc_dev),
             bl_tim_dev ? device_get_name(bl_tim_dev) : "none", require_nocs);
    return VFS_OK;

err_pool:
    dev->ops = NULL;
    COMPAT_MEM_SET(lcd, 0, sizeof(*lcd));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_st7789_pool_ctrl, pool_idx));
    return ret;
}

int st7789_remove_common(struct device* dev)
{
    struct st7789_device* lcd;
    struct dev_lifecycle* lc;
    int                   pool_idx;

    if (!dev)
        return VFS_ERR_INVAL;

    lcd = st7789_get_drvdata(dev);
    if (IS_ERR(lcd))
        return PTR_ERR(lcd);

    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = (int)(lcd - s_st7789_pool);

    dev_lc_remove_start(lc);
    device_ops_unregister(dev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    if (lcd->bl_tim.obj)
        COMPAT_IGNORE_RESULT(st7789_apply_backlight(lcd, 0U));

    COMPAT_MEM_SET(lcd, 0, sizeof(*lcd));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_st7789_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}
