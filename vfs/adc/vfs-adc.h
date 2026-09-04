/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-adc.h
 *@brief ADC VFS 实现 — ADC 总线子系统 VFS 层头文件
 *@author H-000-H

 */

#ifndef __VFS_ADC_H__
#define __VFS_ADC_H__

#include "compiler_compat.h"
#include "hal_adc.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct vfs_adc_arg_t
{
    uint32_t channel_id;    /**< 通道 ID */
    uint32_t done_status;   /**< 转换完成状态 */
    uint32_t channel_count; /**< 通道总数 */
    uint32_t sample_time;   /**< 采样时间 (LL_ADC_SAMPLINGTIME_*) */
    uint32_t channel_index; /**< 通道索引 */
    uint16_t value;         /**< ADC 采样值 */
};
typedef struct vfs_adc_arg_t vfs_adc_arg;

/**
 * @brief Fast Path: 中断 DMA 异步无锁 FIFO 极致直读接口
 * @param[in] pdev ADC 设备指针
 * @param[out] out_val 回传转换值
 * @return 成功返回 MINI_OK, 参数为空返回 MINI_ERR_INVAL
 */
MINI_STATIC_INLINE int vfs_read_dma_it_adc_value(hal_adc_device* pdev, uint16_t* out_val)
{
    if (!pdev || !out_val)
        return MINI_ERR_INVAL;
    return hal_adc_dma_it_read_value(pdev, (uint16_t*)out_val);
}

/**
 * @brief Fast Path: 普通 DMA 同步寄存器极致直读接口
 * @param[in] pdev ADC 设备指针
 * @param[out] out_val 回传转换值
 * @return 成功返回 MINI_OK, 参数为空返回 MINI_ERR_INVAL
 */
MINI_STATIC_INLINE int vfs_read_dma_adc_value(hal_adc_device* pdev, uint16_t* out_val)
{
    if (!pdev || !out_val)
        return MINI_ERR_INVAL;
    return hal_adc_dma_read_value(pdev, out_val);
}

/* -------------------------------------------------------------------------- */
/* IOCTL 命令控制字定义 */
/* -------------------------------------------------------------------------- */
#define ADC_CMD_BASE MINI_MAGIC(ADC)
#define ADC_CMD_GET_CHANNEL_SAMPLE_TIME (ADC_CMD_BASE + 1)
#define ADC_CMD_GET_CHANNEL_ID (ADC_CMD_BASE + 2)
#define ADC_CMD_GET_CHANNEL_COUNT (ADC_CMD_BASE + 3)
#define ADC_CMD_POLL_FOR_CONVERSION (ADC_CMD_BASE + 4)
#define ADC_CMD_CLOSE_CHANNEL (ADC_CMD_BASE + 5)
#define ADC_CMD_READ_VALUE (ADC_CMD_BASE + 6)
#define ADC_CMD_COUNT 6

/* -------------------------------------------------------------------------- */
/* 隔离毒杀机制：防止非内核/非VFS层文件越权绕过总线控制直接调用底层 HAL 慢路径 */
/* -------------------------------------------------------------------------- */
#ifndef ADC_VFS_IMPL
#pragma GCC poison hal_adc_init hal_adc_deinit_all_adcx hal_adc_deinit_adcx_channel
#pragma GCC poison hal_adc_start hal_adc_stop hal_adc_dma_start hal_adc_dma_it_start
#pragma GCC poison hal_adc_read_value hal_adc_poll_for_conversion
#pragma GCC poison hal_adc_get_channel_count hal_adc_get_channel_id hal_adc_get_channel_sample_time
#pragma GCC poison hal_virtual_adc_irq_callback
#endif

#ifdef __cplusplus
}
#endif
#endif /* __VFS_ADC_H__ */