/* SPDX-License-Identifier: Apache-2.0 */
#include "nrf24l01_drv.h"
#include "nrf24l01_regs.h"
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

#ifndef DTC_GEN_COUNT_NORDIC_NRF24L01
#define DTC_GEN_COUNT_NORDIC_NRF24L01  1
#endif
#define NRF24L01_POOL_COUNT  DTC_GEN_COUNT_NORDIC_NRF24L01

struct nrf24l01_device
{
    struct file_operations ops;
    struct device*         spi_dev;

    int                    hw_ready;
};

static struct nrf24l01_device s_nrf24l01_pool[NRF24L01_POOL_COUNT] COMPAT_ALIGNED(4);
static uint8_t             s_nrf24l01_used[NRF24L01_POOL_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t         s_nrf24l01_pool_ctrl COMPAT_ALIGNED(4);
static const char* const kTag = "nrf24l01";

pre_execution(160)
static void nrf24l01_pool_boot_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_nrf24l01_pool_ctrl, s_nrf24l01_used, NRF24L01_POOL_COUNT));
}

static struct nrf24l01_device* nrf24l01_get_drvdata(struct device* dev)
{
    return (struct nrf24l01_device*)device_get_priv(dev);
}


static int nrf24l01_spi_xfer(struct nrf24l01_device* d, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t to)
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


static int nrf24l01_hw_create(struct nrf24l01_device* d)
{
    int r;
    if (!d)
        return VFS_ERR_INVAL;
    if (d->hw_ready)
        return VFS_OK;
    r = device_open(d->spi_dev, NULL);
    if (r != VFS_OK)
        return r;

    d->hw_ready = 1;
    return VFS_OK;
}

static void nrf24l01_hw_destroy(struct nrf24l01_device* d)
{
    if (!d || !d->hw_ready)
        return;

    if (d->spi_dev)
        COMPAT_IGNORE_RESULT(device_close(d->spi_dev));
    d->hw_ready = 0;
}

static int nrf24l01_open(struct device* dev, void* arg)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int first, ret;
    COMPAT_IGNORE_RESULT(arg);
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(dev);
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
        ret = nrf24l01_hw_create(d);
        if (ret != VFS_OK)
        {
            dev_lc_open_abort(lc);
            return ret;
        }
    }
    dev_lc_open_end(lc);
    return VFS_OK;
}

static int nrf24l01_close(struct device* dev)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int last;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;
    if (last)
        nrf24l01_hw_destroy(d);
    dev_lc_close_end(lc);
    return VFS_OK;
}

typedef int (*nrf24l01_ioctl_fn_t)(struct nrf24l01_device* d, void* arg, size_t arg_len, uint32_t ms);
struct nrf24l01_ioctl_map { nrf24l01_ioctl_fn_t handler; };


static int nrf24l01_cmd_wreg(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_reg* a = (struct nrf24l01_reg*)arg;
    uint8_t tx[2];
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    tx[0] = (uint8_t)(NRF24L01_OP_W_REGISTER | (a->reg & NRF24L01_REG_ADDR_MASK));
    tx[1] = a->val;
    return nrf24l01_spi_xfer(d, tx, NULL, 2, to);
}

static int nrf24l01_cmd_rreg(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_reg* a = (struct nrf24l01_reg*)arg;
    uint8_t tx[2] = {0};
    uint8_t rx[2] = {0};
    int r;
    if (!d->hw_ready || !a || len != sizeof(*a))
        return VFS_ERR_INVAL;
    tx[0] = (uint8_t)(a->reg & NRF24L01_REG_ADDR_MASK);
    r = nrf24l01_spi_xfer(d, tx, rx, 2, to);
    if (r != VFS_OK)
        return r;
    a->val = rx[1];
    return VFS_OK;
}

static int nrf24l01_cmd_send(struct nrf24l01_device* d, void* arg, size_t len, uint32_t to)
{
    struct nrf24l01_payload* p = (struct nrf24l01_payload*)arg;
    uint8_t tx[NRF24L01_MAX_PAYLOAD + 1U];
    size_t n;
    if (!d->hw_ready || !p || len != sizeof(*p) || !p->data || p->len == 0U)
        return VFS_ERR_INVAL;
    n = p->len > NRF24L01_MAX_PAYLOAD ? NRF24L01_MAX_PAYLOAD : p->len;
    tx[0] = NRF24L01_OP_W_TX_PAYLOAD;
    COMPAT_IGNORE_RESULT(COMPAT_MEM_COPY(&tx[1], p->data, n));
    return nrf24l01_spi_xfer(d, tx, NULL, n + 1U, to);
}


static const struct nrf24l01_ioctl_map s_nrf24l01_map[NRF24L01_CMD_COUNT] = {
    [NRF24L01_CMD_WRITE_REG - NRF24L01_CMD_BASE - 1] = { nrf24l01_cmd_wreg },
    [NRF24L01_CMD_READ_REG - NRF24L01_CMD_BASE - 1] = { nrf24l01_cmd_rreg },
    [NRF24L01_CMD_SEND - NRF24L01_CMD_BASE - 1] = { nrf24l01_cmd_send },
};

static int nrf24l01_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t ms)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int32_t off;
    int ret;
    if (!dev || !dev->ops)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;
    off = (int32_t)cmd - (int32_t)NRF24L01_CMD_BASE;
    if (off < 1 || off > NRF24L01_CMD_COUNT || !s_nrf24l01_map[off - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_nrf24l01_map[off - 1].handler(d, arg, arg_len, ms);
    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations nrf24l01_fops = {
    .open  = nrf24l01_open,
    .close = nrf24l01_close,
    .ioctl = nrf24l01_ioctl,
};

static int nrf24l01_probe(struct device* dev)
{
    struct nrf24l01_device* d;
    int pool_idx, ret;
    if (!dev)
        return VFS_ERR_INVAL;
    pool_idx = osal_pool_claim(&s_nrf24l01_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;
    d = &s_nrf24l01_pool[pool_idx];
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    d->spi_dev = device_get_parent(dev);
    if (!d->spi_dev)
    {
        ret = VFS_ERR_NODEV;
        goto err;
    }

    if (device_set_priv(dev, d) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err;
    }
    d->ops = nrf24l01_fops;
    dev->ops = &d->ops;
    SYS_LOGI(kTag, "probe OK pool=%d", pool_idx);
    return VFS_OK;
err:
    dev->ops = NULL;
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, pool_idx));
    return ret;
}

static int nrf24l01_remove(struct device* dev)
{
    struct nrf24l01_device* d;
    struct dev_lifecycle* lc;
    int idx;
    if (!dev)
        return VFS_ERR_INVAL;
    d = nrf24l01_get_drvdata(dev);
    if (IS_ERR(d))
        return PTR_ERR(d);
    lc = device_lc(dev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);
    idx = (int)(d - s_nrf24l01_pool);
    dev_lc_remove_start(lc);
    device_ops_unregister(dev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }
    nrf24l01_hw_destroy(d);
    COMPAT_MEM_SET(d, 0, sizeof(*d));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_nrf24l01_pool_ctrl, idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(nrf24l01, "nordic,nrf24l01", nrf24l01_probe, nrf24l01_remove)
