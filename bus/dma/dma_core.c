/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DMA Capability — 核心
 *
 * 为 Bus 层提供 DMA 通道管理。静态池，禁止运行时动态分配。
 */
#include "dma.h"
#include "dma_internal.h"
#include "device.h"
#include "VFS.h"
#include "osal.h"
#include "compiler_compat.h"

#include <string.h>

#ifndef BUS_DMA_MAX_CHANNELS
#define BUS_DMA_MAX_CHANNELS 8U
#endif

static struct bus_dma_chan s_dma_chans[BUS_DMA_MAX_CHANNELS];
static uint8_t             s_dma_chan_used[BUS_DMA_MAX_CHANNELS];
static osal_pool_t         s_dma_chan_pool;
static uint8_t             s_dma_inited;

pre_execution(160)
static void bus_dma_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_dma_chan_pool, s_dma_chan_used, BUS_DMA_MAX_CHANNELS));
}

static int bus_dma_soc_init(void)
{
    if (s_dma_inited)
        return VFS_OK;
    if (g_bus_dma_soc_ops.request == NULL)
        return VFS_ERR_NODEV;
    if (g_bus_dma_soc_ops.init && g_bus_dma_soc_ops.init() != VFS_OK)
        return VFS_ERR_IO;
    s_dma_inited = 1;
    return VFS_OK;
}

int bus_dma_request_chan(struct device* dev, const char* name,
                          struct bus_dma_chan** out)
{
    struct device* dma_dev;
    int            dts_id = -1;
    int            pool_idx;

    if (!dev || !name || !out)
        return VFS_ERR_INVAL;
    *out = NULL;

    if (!s_dma_inited && bus_dma_soc_init() != VFS_OK)
        return VFS_ERR_NODEV;

    dma_dev = device_get_phandle_dev(dev, name);
    if (!dma_dev)
        return VFS_ERR_NODEV;

    if (device_get_prop_int(dma_dev, "reg", &dts_id) != VFS_OK)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_dma_chan_pool);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    struct bus_dma_chan* chan = &s_dma_chans[pool_idx];
    __builtin_memset(chan, 0, sizeof(*chan));
    chan->dts_id = (uint32_t)dts_id;
    chan->in_use = 1;

    if (g_bus_dma_soc_ops.request(chan) != VFS_OK)
    {
        __builtin_memset(chan, 0, sizeof(*chan));
        osal_pool_release(&s_dma_chan_pool, pool_idx);
        return VFS_ERR_IO;
    }

    *out = chan;
    return VFS_OK;
}

void bus_dma_release_chan(struct bus_dma_chan* chan)
{
    int pool_idx;

    if (!chan)
        return;

    pool_idx = (int)(chan - s_dma_chans);
    if (pool_idx < 0 || pool_idx >= BUS_DMA_MAX_CHANNELS || !chan->in_use)
        return;

    g_bus_dma_soc_ops.release(chan);
    __builtin_memset(chan, 0, sizeof(*chan));
    osal_pool_release(&s_dma_chan_pool, pool_idx);
}

int bus_dma_submit(struct bus_dma_chan* chan, const bus_dma_xfer_t* xfer)
{
    if (!chan || !chan->in_use || !xfer)
        return VFS_ERR_INVAL;
    return g_bus_dma_soc_ops.submit(chan, xfer);
}

int bus_dma_wait(struct bus_dma_chan* chan, uint32_t timeout_ms)
{
    if (!chan || !chan->in_use)
        return VFS_ERR_INVAL;
    return g_bus_dma_soc_ops.wait(chan, timeout_ms);
}

int bus_dma_abort(struct bus_dma_chan* chan)
{
    if (!chan || !chan->in_use)
        return VFS_ERR_INVAL;
    return g_bus_dma_soc_ops.abort(chan);
}

int bus_dma_set_callback(struct bus_dma_chan* chan,
                          bus_dma_callback_t cb, void* user_data)
{
    if (!chan || !chan->in_use)
        return VFS_ERR_INVAL;
    chan->cb     = cb;
    chan->cb_arg = user_data;
    return VFS_OK;
}

int bus_dma_busy(struct bus_dma_chan* chan)
{
    if (!chan || !chan->in_use)
        return 0;
    return g_bus_dma_soc_ops.busy(chan);
}

void bus_dma_force_stop(void)
{
    if (g_bus_dma_soc_ops.force_stop)
        g_bus_dma_soc_ops.force_stop();
}
