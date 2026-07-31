/* SPDX-License-Identifier: Apache-2.0 */
#include "bme280_drv.h"
#include "bme280_regs.h"
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

#ifndef DTC_GEN_COUNT_BOSCH_BME280
#define DTC_GEN_COUNT_BOSCH_BME280  1
#endif
#define BME280_POOL_COUNT  DTC_GEN_COUNT_BOSCH_BME280

struct bme280_device
{
    struct file_operations ops;
    struct device*         i2c_dev;
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;
    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;
    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
    int32_t  t_fine;
    int      calib_ok;
    int      hw_ready;
};

static struct bme280_device s_bme280_pool[BME280_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_bme280_used[BME280_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_bme280_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "bme280";

pre_execution(160)
static void bme280_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_bme280_pool_ctrl, s_bme280_used, BME280_POOL_COUNT));
}

static struct bme280_device* bme280_get_drvdata(struct device* dev)
{
    return (struct bme280_device*)device_get_priv(dev);
}


static int bme280_i2c_wr(struct bme280_device* d, const uint8_t* tx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !tx || len == 0U)
        return VFS_ERR_INVAL;
    return device_write(d->i2c_dev, tx, len, to);
}

static int bme280_i2c_rd(struct bme280_device* d, uint8_t* rx, size_t len, uint32_t to)
{
    if (!d || !d->i2c_dev || !rx || len == 0U)
        return VFS_ERR_INVAL;
    return device_read(d->i2c_dev, rx, len, to);
}


static int bme280_read_regs(struct bme280_device* d, uint8_t start, uint8_t* buf, size_t len, uint32_t to)
{
    int r = bme280_i2c_wr(d, &start, 1, to);
    if (r != VFS_OK)
        return r;
    return bme280_i2c_rd(d, buf, len, to);
}

static int bme280_load_calib(struct bme280_device* d, uint32_t to)
{
    uint8_t c[24];
    uint8_t h[7];
    int r = bme280_read_regs(d, BME280_REG_DIG_T1, c, 24, to);
    if (r != VFS_OK)
        return r;
    r = bme280_read_regs(d, BME280_REG_DIG_H1, &d->dig_H1, 1, to);
    if (r != VFS_OK)
        return r;
    r = bme280_read_regs(d, BME280_REG_DIG_H2, h, 7, to);
    if (r != VFS_OK)
        return r;
    d->dig_T1 = (uint16_t)(c[0] | ((uint16_t)c[1] << 8));
    d->dig_T2 = (int16_t)(c[2] | ((uint16_t)c[3] << 8));
    d->dig_T3 = (int16_t)(c[4] | ((uint16_t)c[5] << 8));
    d->dig_P1 = (uint16_t)(c[6] | ((uint16_t)c[7] << 8));
    d->dig_P2 = (int16_t)(c[8] | ((uint16_t)c[9] << 8));
    d->dig_P3 = (int16_t)(c[10] | ((uint16_t)c[11] << 8));
    d->dig_P4 = (int16_t)(c[12] | ((uint16_t)c[13] << 8));
    d->dig_P5 = (int16_t)(c[14] | ((uint16_t)c[15] << 8));
    d->dig_P6 = (int16_t)(c[16] | ((uint16_t)c[17] << 8));
    d->dig_P7 = (int16_t)(c[18] | ((uint16_t)c[19] << 8));
    d->dig_P8 = (int16_t)(c[20] | ((uint16_t)c[21] << 8));
    d->dig_P9 = (int16_t)(c[22] | ((uint16_t)c[23] << 8));
    d->dig_H2 = (int16_t)(h[0] | ((uint16_t)h[1] << 8));
    d->dig_H3 = h[2];
    d->dig_H4 = (int16_t)(((int16_t)h[3] << 4) | (h[4] & 0x0F));
    d->dig_H5 = (int16_t)(((int16_t)h[5] << 4) | (h[4] >> 4));
    d->dig_H6 = (int8_t)h[6];
    d->calib_ok = 1;
    return VFS_OK;
}

static int32_t bme280_compensate_t(struct bme280_device* d, int32_t adc_t)
{
    int32_t var1 = ((((adc_t >> 3) - ((int32_t)d->dig_T1 << 1))) * ((int32_t)d->dig_T2)) >> 11;
    int32_t var2 = (((((adc_t >> 4) - ((int32_t)d->dig_T1)) *
                      ((adc_t >> 4) - ((int32_t)d->dig_T1))) >> 12) *
                    ((int32_t)d->dig_T3)) >> 14;
    d->t_fine = var1 + var2;
    return (d->t_fine * 5 + 128) >> 8;
}

static uint32_t bme280_compensate_p(struct bme280_device* d, int32_t adc_p)
{
    int64_t var1 = ((int64_t)d->t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)d->dig_P6;
    int64_t p;
    var2 = var2 + ((var1 * (int64_t)d->dig_P5) << 17);
    var2 = var2 + (((int64_t)d->dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)d->dig_P3) >> 8) + ((var1 * (int64_t)d->dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1) * (int64_t)d->dig_P1) >> 33;
    if (var1 == 0)
        return 0;
    p = 1048576 - adc_p;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)d->dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)d->dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)d->dig_P7) << 4);
    return (uint32_t)(p >> 8);
}

static uint32_t bme280_compensate_h(struct bme280_device* d, int32_t adc_h)
{
    int32_t v_x1;
    v_x1 = d->t_fine - 76800;
    v_x1 = (((((adc_h << 14) - (((int32_t)d->dig_H4) << 20) - (((int32_t)d->dig_H5) * v_x1)) + 16384) >> 15) *
            (((((((v_x1 * ((int32_t)d->dig_H6)) >> 10) *
                 (((v_x1 * ((int32_t)d->dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
              ((int32_t)d->dig_H2) + 8192) >> 14));
    v_x1 = v_x1 - (((((v_x1 >> 15) * (v_x1 >> 15)) >> 7) * ((int32_t)d->dig_H1)) >> 4);
    v_x1 = (v_x1 < 0) ? 0 : v_x1;
    v_x1 = (v_x1 > 419430400) ? 419430400 : v_x1;
    return (uint32_t)(v_x1 >> 12); /* Q22.10 → %RH * 1024 */
}

static int bme280_hw_create(struct bme280_device* d)
{
    const uint8_t soft_rst[2] = {BME280_REG_SOFT_RESET, BME280_SOFT_RESET_VAL};
    const uint8_t ctrl_hum[2] = {BME280_REG_CTRL_HUM, BME280_CTRL_HUM_OSRS1};
    const uint8_t ctrl_meas[2] = {BME280_REG_CTRL_MEAS, BME280_CTRL_FORCED_X1};
    int r;
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    r = device_open(d->i2c_dev, NULL);
    if (r != VFS_OK)
        return r;
    r = bme280_i2c_wr(d, soft_rst, 2, 100);
    if (r != VFS_OK)
        goto fail;
    osal_delay_ms(10);
    r = bme280_load_calib(d, 100);
    if (r != VFS_OK)
        goto fail;
    r = bme280_i2c_wr(d, ctrl_hum, 2, 100);
    if (r != VFS_OK)
        goto fail;
    r = bme280_i2c_wr(d, ctrl_meas, 2, 100);
    if (r != VFS_OK)
        goto fail;
    d->hw_ready = 1;
    return VFS_OK;
fail:
    COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    return r;
}

static void bme280_hw_destroy(struct bme280_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->i2c_dev) COMPAT_IGNORE_RESULT(device_close(d->i2c_dev));
    d->hw_ready = 0;

}

static int bme280_open(struct device* dev, void* arg)
{
    struct bme280_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bme280_get_drvdata(dev);
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
        ret = bme280_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int bme280_close(struct device* dev)
{
    struct bme280_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bme280_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        bme280_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*bme280_ioctl_fn_t)(struct bme280_device* d, void* arg, size_t arg_len, uint32_t ms);
struct bme280_ioctl_map { bme280_ioctl_fn_t handler; };


static int bme280_cmd_env(struct bme280_device* d, void* arg, size_t len, uint32_t to)
{
    const uint8_t ctrl_hum[2] = {BME280_REG_CTRL_HUM, BME280_CTRL_HUM_OSRS1};
    const uint8_t ctrl_meas[2] = {BME280_REG_CTRL_MEAS, BME280_CTRL_FORCED_X1};
    uint8_t raw[8];
    struct bme280_env* e = (struct bme280_env*)arg;
    int32_t adc_p;
    int32_t adc_t;
    int32_t adc_h;
    uint32_t hum_q22;
    int r;
    if (!d->hw_ready || !d->calib_ok || !e || len != sizeof(*e))
        return VFS_ERR_INVAL;
    r = bme280_i2c_wr(d, ctrl_hum, 2, to);
    if (r != VFS_OK)
        return r;
    r = bme280_i2c_wr(d, ctrl_meas, 2, to);
    if (r != VFS_OK)
        return r;
    osal_delay_ms(15);
    r = bme280_read_regs(d, BME280_REG_PRESS_MSB, raw, sizeof(raw), to);
    if (r != VFS_OK)
        return r;
    adc_p = (int32_t)(((uint32_t)raw[0] << 12) | ((uint32_t)raw[1] << 4) | (raw[2] >> 4));
    adc_t = (int32_t)(((uint32_t)raw[3] << 12) | ((uint32_t)raw[4] << 4) | (raw[5] >> 4));
    adc_h = (int32_t)(((uint32_t)raw[6] << 8) | raw[7]);
    e->temp_c_x100 = (int16_t)bme280_compensate_t(d, adc_t);
    e->pressure = bme280_compensate_p(d, adc_p);
    hum_q22 = bme280_compensate_h(d, adc_h);
    e->humidity_x100 = (uint16_t)((hum_q22 * 100U) / 1024U);
    return VFS_OK;
}
static const struct bme280_ioctl_map s_bme280_map[BME280_CMD_COUNT] = {
    [BME280_CMD_READ_ENV - BME280_CMD_BASE - 1] = { bme280_cmd_env },
};


static int bme280_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct bme280_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = bme280_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)BME280_CMD_BASE;
    if (off < 1 || off > BME280_CMD_COUNT || !s_bme280_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_bme280_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations bme280_fops =
{
    .open  = bme280_open,
    .close = bme280_close,
    .ioctl = bme280_ioctl,
};

static int bme280_probe(struct device* dev)
{
    struct bme280_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_bme280_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_bme280_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->i2c_dev = device_get_parent(dev);
    if (!d->i2c_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = bme280_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bme280_pool_ctrl, pool_idx));
    return ret;
}

static int bme280_remove(struct device* dev)
{
    struct bme280_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = bme280_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_bme280_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    bme280_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_bme280_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(bme280, "bosch,bme280", bme280_probe, bme280_remove)
