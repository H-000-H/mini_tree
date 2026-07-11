/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 该文件实现了 ADC 的 HAL 接口
 * 实现了 ADC 的初始化、读取、关闭等基本功能
*/
#include "hal_adc.h"
#include "buffer.h"
#include "system_log.h"
#include "interrupt.h"
#ifndef DTS_DMA_BUFFER_SIZE
#define DMA_BUFFER_SIZE 1024
#endif

/**< ADC 设备实例由 vfs_adc_probe 分配, ISR 通过 VIRQ 表 arg 获取 */
#ifndef DTS_HAL_ADC_INSTANCE_MAX
#define DTS_HAL_ADC_INSTANCE_MAX 3 /**< 兜底:最大 ADC 实例数 */
#endif

/*===========================================================================================================================================================*/
/*结构体和全局变量定义*/
/*===========================================================================================================================================================*/

/**
 * @brief 配置GPIO的模拟输入功能
 * @param gpio GPIO配置结构体
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数错误
 */
COMPAT_STATIC_INLINE int hal_adc_config_gpio_pin(hal_adc_gpio_config* gpio)
{
    if (!gpio)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief DMA 静态参数一次性配置 (hal_adc_init 时调用, start 路径只设长度+启停)
 * @note  采用 LL_DMA_InitTypeDef + LL_DMA_Init 批量初始化范式 (同 LL_ADC_Init)。
 *        channel/direction/priority/mode/inc/size/地址/长度 来自 DTS 且永不变。
 *        dma_enable=0 时由调用方跳过。
 */
static void hal_adc_dma_init(hal_adc_device* pdev)
{
}

/*===========================================================================================================================================================*/
/*设备初始化与销毁*/
/*===========================================================================================================================================================*/

/**
 * @brief 绑定 ADC host 配置与 unique 平台配置到设备句柄
 * @param pdev ADC 设备结构体指针
 * @param unique_cfg 平台唯一配置指针
 * @param host host 配置指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数错误
 */
int hal_adc_device_init(hal_adc_device* pdev, hal_adc_platform_unique_config* unique_cfg, hal_adc_host_config* host)
{
    if (!pdev || !unique_cfg || !host)
        return VFS_ERR_INVAL;

    pdev->host   = host;
    pdev->unique = unique_cfg;
    return VFS_OK;
}

int hal_adc_device_deinit(hal_adc_device* pdev)
{
    if (!pdev)
        return VFS_OK;

    pdev->unique = NULL;
    pdev->host   = NULL;
    return VFS_OK;
}
/**
 * @brief 初始化 ADC 外设
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_init(hal_adc_device* pdev)
{
    if (!pdev || !pdev->host->adc_handle || !pdev->host->multi_cfg || !pdev->host->channels)
        return VFS_ERR_INVAL;

    /**< 边界检查：防止 DTS 映射层传入的物理设备索引越界 */
    if (pdev->host->dev_index >= DTS_HAL_ADC_INSTANCE_MAX)
        return VFS_ERR_INVAL;

    /**
     * @brief pdev->host->config.channel_num 在 dts 解析层直接映射为自然数(1, 2, 3...)
     */
    uint32_t convert_count = (uint32_t)pdev->host->config.channel_num;
    if (convert_count == 0) convert_count = 1;

    if (hal_adc_config_gpio_pin(&pdev->host->gpio_cfg) != VFS_OK)
        return VFS_ERR_INVAL;

    /**< 初始化阶段完成物理基地址 */

    /**< 同步刷新上下文控制句柄的总数>*/
    pdev->host->channel_count = convert_count;

    /**< DMA 静态参数一次性配置: dma_enable=0 时跳过 */
    if (pdev->host->dma_cfg.dma_enable)
        hal_adc_dma_init(pdev);

    return VFS_OK;
}

/**
 * @brief 整个 ADCX 设备的整体销毁与去初始化
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_deinit_all_adcx(hal_adc_device *pdev)
{
    if (!pdev || !pdev->host->adc_handle)
        return VFS_ERR_INVAL;

    if (pdev->host->dev_index >= DTS_HAL_ADC_INSTANCE_MAX)
        return VFS_ERR_INVAL;

    /**< 恢复引脚状态为浮空输入>*/

    /**< 释放高速直投表，阻断野指针生存周期 */

    pdev->host->channel_count = 0;
    pdev->host->config.channel_num = 0;

    return VFS_OK;
}

/**
 * @brief 安全移除并关闭单个 ADCX 设备下的某个特定通道
 * @param pdev ADC 设备指针
 * @param channel_id 通道号
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_deinit_adcx_channel(hal_adc_device *pdev, uint32_t channel_id)
{
    if (!pdev || !pdev->host->adc_handle || !pdev->host->channels)
        return VFS_ERR_INVAL;

    uint32_t current_count = (uint32_t)pdev->host->config.channel_num;

    if (current_count <= 1)
    {
        pdev->host->config.channel_num = 0;
        pdev->host->channel_count = 0;
        return VFS_OK;
    }

    int target_index = -1;
    for (uint32_t i = 0; i < current_count; i++)
    {
        if (pdev->host->channels[i].channel_id == channel_id)
        {
            target_index = (int)i;
            break;
        }
    }

    if (target_index == -1)
        return VFS_ERR_INVAL;

    /**< 重组通道将后一位移到前一位，Rank(次序)要保持连续 >*/
    for (uint32_t i = (uint32_t)target_index; i < current_count - 1; i++)
    {
        pdev->host->channels[i].channel_id  = pdev->host->channels[i + 1].channel_id;
        pdev->host->channels[i].sample_time = pdev->host->channels[i + 1].sample_time;
    }

    /**< 先把被挤出来的、硬件上多余的最后一个 Rank 在寄存器中清除（抹除脏数据残留） >*/

    /**< 双账本计数器必须同步递减>*/
    pdev->host->config.channel_num--;
    pdev->host->channel_count--;

    /**<将已经计算并移位好的序列长度直接投喂给库函数 */

    /**< 重新刷新所有有效 Rank 的通道映射 >*/

    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*启动停止控制*/
/*===========================================================================================================================================================*/

/**
 * @brief 启动 ADC 转换任务
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_start(hal_adc_device *pdev)
{
    if (!pdev || !pdev->host->adc_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 停止 ADC 硬件转换任务
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_stop(hal_adc_device *pdev)
{
    if (!pdev || !pdev->host->adc_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 启动 ADC DMA 传输
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_dma_start(hal_adc_device *pdev)
{
    if (!pdev || !pdev->host->adc_handle || !pdev->host->dma_cfg.dma_handle || !pdev->host->private_cfg)
        return VFS_ERR_INVAL;

    /**< 热路径: 静态参数 (方向/地址/通道/优先级) 已在 hal_adc_init 经 LL_DMA_Init 配好;
     *   NORMAL 模式 stream 传完自动停, 重启需重设长度 + 启动 */

    /**< 联动激活 ADC 外设端的 DMA 传输通道请求触发 */

    return hal_adc_start(pdev);
}

/**
 * @brief 启动 ADC DMA 中断传输
 * @param pdev ADC 设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 int hal_adc_dma_it_start(hal_adc_device *pdev)
 {
     if (!pdev || !pdev->host->adc_handle || !pdev->host->dma_cfg.dma_handle || !pdev->host->private_cfg)
         return VFS_ERR_INVAL;

     pdev->host->private_cfg->dma_it_enable = true;

     /**< 热路径: 静态参数 (方向/地址/通道/优先级) 已在 hal_adc_init 经 LL_DMA_Init 配好;
      *   NORMAL 模式 stream 传完自动停, 重启需重设长度 + 启动 */

     fifo_init(&pdev->host->private_cfg->dma_buffer_handle, pdev->host->private_cfg->dma_data_buf, DMA_BUFFER_SIZE);

     /**< 一次性绑定全局下半部 work (fn/arg/原子位) — 全局变量由 interrupt.c 定义 */
     g_adc_dma_bottom_half_work.fn  = hal_adc_dma_bottom_half_handler;
     g_adc_dma_bottom_half_work.arg = pdev;
     COMPAT_ATOMIC_STORE(&g_adc_dma_bottom_half_work.pending,   false, COMPAT_MO_SEQ_CST);
     COMPAT_ATOMIC_STORE(&g_adc_dma_bottom_half_work.executing, false, COMPAT_MO_SEQ_CST);
     COMPAT_ATOMIC_STORE(&g_adc_dma_bottom_half_work.rerun,     false, COMPAT_MO_SEQ_CST);

     return hal_adc_start(pdev);
 }

/*===========================================================================================================================================================*/
/*数据读取接口*/
/*==========================================================================================================================================================*/

/**
 * @brief 读取单次转换的原始数据
 * @param pdev ADC 设备指针
 * @param channel_num 通道号
 * @param out_val 输出值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL/VFS_ERR_NOTSUPP/VFS_ERR_NODEV/VFS_ERR_AGAIN 失败
 */
 int hal_adc_read_value(hal_adc_device *pdev, uint32_t channel_num, uint16_t *out_val)
 {
    COMPAT_IGNORE_RESULT(channel_num);
     if (!pdev || !pdev->host->adc_handle || !out_val)
         return VFS_ERR_INVAL;

     /**< 边界拦截：若配置为多通道扫描或非软件独立触发，直接返回不受理错误码 */

     /**< 确保 ADC 已使能>*/

     /**< 刷新清除残留的旧规则组转换结束标志 >*/

     /**< 软件启动转换路径>*/

     /**< 轮询等待 EOCS 标志置位 */

     /**< 读出并输出数据（读取 DR 寄存器会自动清除 EOCS 标志）>*/

     return VFS_OK;
 }
 
 /**
 * @brief 检查当前转换是否已完成
 * @param pdev ADC 设备指针
 * @param out_status 状态输出指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
  */
int hal_adc_poll_for_conversion(hal_adc_device *pdev, uint32_t *out_status)
{
    if (!pdev || !pdev->host->adc_handle || !out_status)
        return VFS_ERR_INVAL;

    /**< 直接透出当前 EOCS 标志状态 (1 为完成，0 为未完成)>*/

    return VFS_OK;
}

/**
 * @brief 获取当前ADC的通道数量
 * @param pdev ADC 设备指针
 * @param count 存储通道数量的指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_get_channel_count(hal_adc_device *pdev, uint32_t*count)
{
    if (!pdev || !count)
        return VFS_ERR_INVAL;

    *count = (int)pdev->host->channel_count;
    return VFS_OK;
}

/**
 * @brief 获取指定索引的通道ID
 * @param pdev ADC 设备指针
 * @param index 通道索引
 * @param channel_id 存储通道ID的指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_get_channel_id(hal_adc_device *pdev, int index, uint32_t *channel_id)
{
    if (!pdev || !channel_id || index < 0 || (uint32_t)index >= pdev->host->channel_count)
        return VFS_ERR_INVAL;

    *channel_id = pdev->host->channels[index].channel_id;
    return VFS_OK;
}

/**
 * @brief 获取指定索引的通道采样时间
 * @param pdev ADC 设备指针
 * @param index 通道索引
 * @param sample_time 存储采样时间的指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_get_channel_sample_time(hal_adc_device *pdev, int index, uint32_t *sample_time)
{
    if (!pdev || !sample_time || index < 0 || (uint32_t)index >= pdev->host->channel_count)
        return VFS_ERR_INVAL;

    *sample_time = pdev->host->channels[index].sample_time;
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*DMA 快速路径接口*/
/*===========================================================================================================================================================*/

/**
 * @brief 极致热路径数据读取（普通DMA同步数据包提取接口）
 * @param pdev ADC 设备指针
 * @param out_val 输出值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_adc_dma_read_value(hal_adc_device *pdev, uint16_t *out_val)
{
    /**<热路径优化:直接读取 DR 寄存器 */
#ifdef HARD_PATH_STRICT_CHECK
    if (!pdev || !out_val || pdev->host->dev_index >= DTS_HAL_ADC_INSTANCE_MAX) return VFS_ERR_INVAL;
#endif

    return VFS_OK;
}

/**
 * @brief 读取DMA中断的ADC值
 * @param pdev ADC 设备指针
 * @param out_val 输出值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL/VFS_ERR_AGAIN 失败
 */
int hal_adc_dma_it_read_value(hal_adc_device *pdev, uint16_t *out_val)
{
    if (!pdev || !pdev->host->adc_handle || !pdev->host->private_cfg || !out_val)
        return VFS_ERR_INVAL;

    Fifo_Data_type tmp;
    if (fifo_read_data(&pdev->host->private_cfg->dma_buffer_handle, &tmp))
    {
        *out_val = (uint16_t)tmp;
        return VFS_OK;
    }

    /**<读空代表当前数据未就绪，必须返回 VFS_ERR_AGAIN 提示上层稍后再试 >*/
    return VFS_ERR_AGAIN;
}

/**
 * @brief ADC 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_adc_device*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 表示需要 submit 下半部; VFS_IRQ_ENTRY_NOBOTTOM 表示不需要
 * @note  上半部仅做轻量状态检查, 重活 (fifo 拷贝) 由 dispatch 自动 submit 下半部
 */
int hal_virtual_adc_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);

    return VFS_IRQ_ENTRY_NOBOTTOM;
}

/**
 * @brief ADC DMA 下半部处理函数 (主循环上下文执行 fifo 拷贝)
 * @param arg 参数 (hal_adc_device*)
 * @note  由 interrupt_virtual_dispatch 在 ISR 退出后通过 bottom_half_run_pending 调用
 */
void hal_adc_dma_bottom_half_handler(void* arg)
{
}