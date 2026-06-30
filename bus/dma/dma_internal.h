/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DMA Capability 内部头
 */
#ifndef BUS_DMA_INTERNAL_H
#define BUS_DMA_INTERNAL_H

#include "dma.h"

#ifdef __cplusplus
extern "C" {
#endif

struct bus_dma_chan {
    uint32_t           dts_id;
    bus_dma_callback_t cb;
    void*              cb_arg;
    uint8_t            in_use;
};

struct bus_dma_soc_ops {
    int  (*init)(void);
    void (*force_stop)(void);
    int  (*request)(struct bus_dma_chan* chan);
    void (*release)(struct bus_dma_chan* chan);
    int  (*submit)(struct bus_dma_chan* chan, const bus_dma_xfer_t* xfer);
    int  (*wait)(struct bus_dma_chan* chan, uint32_t timeout_ms);
    int  (*abort)(struct bus_dma_chan* chan);
    int  (*busy)(struct bus_dma_chan* chan);
};

extern const struct bus_dma_soc_ops g_bus_dma_soc_ops;

#ifdef __cplusplus
}
#endif

#endif /* BUS_DMA_INTERNAL_H */
