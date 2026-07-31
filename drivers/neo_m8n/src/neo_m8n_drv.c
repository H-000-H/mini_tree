/* SPDX-License-Identifier: Apache-2.0 */
#include "neo_m8n_drv.h"
#include "vfs-uart.h"
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

#ifndef DTC_GEN_COUNT_UBLOX_NEO_M8N
#define DTC_GEN_COUNT_UBLOX_NEO_M8N  1
#endif
#define NEO_M8N_POOL_COUNT  DTC_GEN_COUNT_UBLOX_NEO_M8N

struct neo_m8n_device
{
    struct file_operations ops;
    struct device* uart_dev;
    uint8_t rxbuf[128];

    int                    hw_ready;
};

static struct neo_m8n_device s_neo_m8n_pool[NEO_M8N_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_neo_m8n_used[NEO_M8N_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_neo_m8n_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "neo_m8n";

pre_execution(160)
static void neo_m8n_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_neo_m8n_pool_ctrl, s_neo_m8n_used, NEO_M8N_POOL_COUNT));
}

static struct neo_m8n_device* neo_m8n_get_drvdata(struct device* dev)
{
    return (struct neo_m8n_device*)device_get_priv(dev);
}


static int neo_m8n_uart_xchg(struct neo_m8n_device* d, const uint8_t* tx, size_t tx_len, uint8_t* rx, size_t rx_len, uint32_t to)
{
    struct uart_transfer_arg arg;
    if (!d || !d->uart_dev)
        return VFS_ERR_INVAL;
    arg.tx = tx;
    arg.rx = rx;
    arg.tx_len = tx_len;
    arg.rx_len = rx_len;
    return device_ioctl(d->uart_dev, UART_CMD_TRANSFER, &arg, sizeof(arg), to);
}


static int neo_m8n_hw_create(struct neo_m8n_device* d)
{
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    { int r = device_open(d->uart_dev, NULL); if (r != VFS_OK) return r; }
    d->hw_ready = 1; return VFS_OK;

}

static void neo_m8n_hw_destroy(struct neo_m8n_device* d)
{
    if (!d || !d->hw_ready)
        return;
    if (d->uart_dev) COMPAT_IGNORE_RESULT(device_close(d->uart_dev));
    d->hw_ready = 0;

}

static int neo_m8n_open(struct device* dev, void* arg)
{
    struct neo_m8n_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = neo_m8n_get_drvdata(dev);
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
        ret = neo_m8n_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int neo_m8n_close(struct device* dev)
{
    struct neo_m8n_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = neo_m8n_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        neo_m8n_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*neo_m8n_ioctl_fn_t)(struct neo_m8n_device* d, void* arg, size_t arg_len, uint32_t ms);
struct neo_m8n_ioctl_map { neo_m8n_ioctl_fn_t handler; };


static int neo_m8n_cmd_nmea(struct neo_m8n_device* d, void* arg, size_t len, uint32_t to)
{
    struct neo_m8n_buf* b = (struct neo_m8n_buf*)arg;
    size_t got = 0;
    int r;

    if (!d->hw_ready || !b || len != sizeof(*b) || !b->data || b->cap == 0U)
        return VFS_ERR_INVAL;

    r = device_read(d->uart_dev, (uint8_t*)b->data, b->cap, to);
    if (r < 0)
        return r;
    got = (size_t)r;
    while (got < b->cap)
    {
        r = device_read(d->uart_dev, (uint8_t*)&b->data[got], 1, 10);
        if (r <= 0)
            break;
        got += (size_t)r;
        if (b->data[got - 1U] == '\n')
            break;
    }
    b->len = got;
    return VFS_OK;
}
static const struct neo_m8n_ioctl_map s_neo_m8n_map[NEO_M8N_CMD_COUNT] = {
    [NEO_M8N_CMD_READ_NMEA - NEO_M8N_CMD_BASE - 1] = { neo_m8n_cmd_nmea },
};


static int neo_m8n_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct neo_m8n_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = neo_m8n_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)NEO_M8N_CMD_BASE;
    if (off < 1 || off > NEO_M8N_CMD_COUNT || !s_neo_m8n_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_neo_m8n_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations neo_m8n_fops =
{
    .open  = neo_m8n_open,
    .close = neo_m8n_close,
    .ioctl = neo_m8n_ioctl,
};

static int neo_m8n_probe(struct device* dev)
{
    struct neo_m8n_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_neo_m8n_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_neo_m8n_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->uart_dev = device_get_parent(dev);
    if (!d->uart_dev) { ret = VFS_ERR_NODEV; goto err; }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = neo_m8n_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_neo_m8n_pool_ctrl, pool_idx));
    return ret;
}

static int neo_m8n_remove(struct device* dev)
{
    struct neo_m8n_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = neo_m8n_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_neo_m8n_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    neo_m8n_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_neo_m8n_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(neo_m8n, "u-blox,neo-m8n", neo_m8n_probe, neo_m8n_remove)
