/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * I2S VFS — I2S 总线子系统 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → Bus Layer → HAL Layer
 * 职责: file_operations + dev_lifecycle + DTS; I/O 走 i2s_bus。
 * 隔离: 定义 I2S_VFS_IMPL 可调 i2s_bus_*; 其他文件包含本头时相关符号被 #pragma GCC poison。
 *
 * sync: POLL / DMA NORMAL / AUTO; circular+fifo_spsc 经 CIRC_* ioctl。
 * HT/TC irq_mode 与 async 经 ioctl; 虚拟中断在 i2s_bus_open 注册 (对齐 ADC, 不进 ioctl)。
 *
 * @see bus/i2s/i2s_bus.h
 * @see hal/i2s/hal_i2s.h
 *@=========================================================================================================================*/
#ifndef VFS_I2S_H
#define VFS_I2S_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

#define I2S_CMD_BASE            COMPAT_MAGIC(I2S)
#define I2S_CMD_TRANSFER        (I2S_CMD_BASE + 0x01)
#define I2S_CMD_SET_XFER_MODE   (I2S_CMD_BASE + 0x02)
#define I2S_CMD_GET_XFER_MODE   (I2S_CMD_BASE + 0x03)
#define I2S_CMD_TRANSFER_ASYNC  (I2S_CMD_BASE + 0x04)  /**< async 提交 (参数存档占位) */
#define I2S_CMD_ASYNC_WAIT      (I2S_CMD_BASE + 0x05)  /**< 等 async 完成 (占位) */
#define I2S_CMD_CIRC_START      (I2S_CMD_BASE + 0x06)  /**< arg: i2s_circ_arg */
#define I2S_CMD_CIRC_STOP       (I2S_CMD_BASE + 0x07)
#define I2S_CMD_CIRC_WRITE      (I2S_CMD_BASE + 0x08)  /**< arg: i2s_circ_buf_arg */
#define I2S_CMD_CIRC_READ       (I2S_CMD_BASE + 0x09)  /**< arg: i2s_circ_buf_arg */
#define I2S_CMD_SET_DMA_IRQ_MODE (I2S_CMD_BASE + 0x0A) /**< arg: i2s_dma_irq_mode_arg */
#define I2S_CMD_GET_DMA_IRQ_MODE (I2S_CMD_BASE + 0x0B) /**< arg: i2s_dma_irq_mode_arg */
#define I2S_CMD_COUNT           11

#define I2S_XFER_AUTO 0U
#define I2S_XFER_POLL 1U
#define I2S_XFER_DMA  2U

#define I2S_IRQ_NONE  0U
#define I2S_IRQ_TC    1U
#define I2S_IRQ_HT    2U
#define I2S_IRQ_HT_TC 3U

struct i2s_transfer_arg
{
    const uint16_t* tx;    /**< 发送缓冲区 (可为 NULL) */
    uint16_t*       rx;    /**< 接收缓冲区 (可为 NULL) */
    size_t          samples; /**< 采样点数 */
    uint32_t        xfer_mode; /**< I2S_XFER_AUTO / POLL / DMA */
};

struct i2s_xfer_mode_arg
{
    uint32_t xfer_mode;     /**< I2S_XFER_AUTO / POLL / DMA */
};

/** @brief 异步传输参数 (提交即返回; 完成经 cb — 当前 HAL 存档占位) */
typedef void (*i2s_async_cb_t)(struct device* dev, const void* trans, void* userdata);

struct i2s_transfer_async_arg
{
    const uint16_t* tx;    /**< 发送缓冲区 (可为 NULL) */
    uint16_t*       rx;    /**< 接收缓冲区 (可为 NULL) */
    size_t          samples; /**< 采样点数 */
    i2s_async_cb_t  cb;      /**< 完成回调 */
    void*           userdata; /**< 用户私有数据 */
};

struct i2s_dma_irq_mode_arg
{
    uint32_t irq_mode; /**< I2S_IRQ_NONE / TC / HT / HT_TC */
};

struct i2s_circ_arg
{
    uint32_t tx_enable; /**< 1=启动 TX circular */
    uint32_t rx_enable; /**< 1=启动 RX circular */
};

struct i2s_circ_buf_arg
{
    uint16_t* data;       /**< 数据缓冲区 */
    uint32_t  samples;    /**< 采样点数 */
};

#ifdef __cplusplus
}
#endif

#ifndef I2S_VFS_IMPL
#pragma GCC poison i2s_bus_host_init i2s_bus_host_deinit i2s_bus_client_register
#pragma GCC poison i2s_bus_open i2s_bus_close i2s_bus_transfer
#pragma GCC poison i2s_bus_transfer_async i2s_bus_transfer_poll
#pragma GCC poison i2s_bus_set_dma_irq_mode i2s_bus_get_dma_irq_mode
#pragma GCC poison i2s_bus_dma_circ_start i2s_bus_dma_circ_stop
#pragma GCC poison i2s_bus_dma_circ_write i2s_bus_dma_circ_read
#endif

#endif
