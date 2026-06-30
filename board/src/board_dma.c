/* SPDX-License-Identifier: Apache-2.0 */
/*
 * board_dma.c — 板级 DMA 通道注册实现 (STM32)
 *
 * board_dma_id_from_phandle: 解析 phandle 引用的 DMA 设备并读取其 reg ID.
 * board_dma_register_channels: 遍历 stm32,dma-channel 节点, 读取
 *   controller/stream/channel 属性调 hal_dma_stm32_register_from_props,
 *   最后调 hal_dma_stm32_init 完成 DMA 控制器初始化.
 */
#include "board_dma.h"
#include "device.h"
#include "board_devtable.h"
#include "hal_dma_stm32.h"
#include "VFS.h"

#include <string.h>

int board_dma_id_from_phandle(const struct device* dev, const char* prop, int* out_id)
{
    struct device* dma_dev;

    if (!dev || !prop || !out_id)
        return VFS_ERR_INVAL;

    *out_id = -1;
    dma_dev = device_get_phandle_dev(dev, prop);
    if (!dma_dev)
        return VFS_ERR_NODEV;

    return device_get_prop_int(dma_dev, "reg", out_id);
}

int board_dma_register_channels(void)
{
    int registered = 0;

    for (int i = 0; i < DEV_ID_COUNT; i++)
    {
        const struct device_node* node = board_node_get((device_id_t)i);
        struct device             dev_stub;
        int                       reg_val;
        int                       ctrl;
        int                       stream;
        int                       channel;

        if (!node || !node->compatible)
            continue;
        if (strcmp(node->compatible, "stm32,dma-channel") != 0)
            continue;
        if (node->status == DEVICE_STATUS_DISABLED)
            continue;

        dev_stub.node = node;

        if (device_get_prop_int(&dev_stub, "reg", &reg_val) != VFS_OK)
            continue;
        if (device_get_prop_int(&dev_stub, "controller", &ctrl) != VFS_OK)
            continue;
        if (device_get_prop_int(&dev_stub, "stream", &stream) != VFS_OK)
            continue;
        if (device_get_prop_int(&dev_stub, "channel", &channel) != VFS_OK)
            continue;

        if (hal_dma_stm32_register_from_props((uint32_t)reg_val, ctrl, stream, channel) != VFS_OK)
            return VFS_ERR_IO;

        registered++;
    }

    (void)registered;
    return hal_dma_stm32_init();
}
