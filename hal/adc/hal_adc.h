/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file        hal_adc.h
 * @brief       通用 ADC 硬件直投层(HAL)接口定义和 ADC 用法扩展说明
 * @note        和 ADC 相关的配置和操作都放在 hal_adc.h 和 hal_adc.c 中
 * @details     本文件定义了 ADC 在热路径(Hot-Path)下的高效率配置接口。
 * @note        所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note        由于 ADC 是快速热路径外设所以 ADC 的初始化与配置应该尽量在硬件直投层完成
 * @note        vfs 层只负责拉取 ADC 的配置并传递给 HAL 层并且对 hal 层提供的 api 进行内联封装
 * @note        文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码
 * @note        获取参数不能直接返回，必须通过指针参数传递
 * @note
 * adc不走函数指针init或者close,因为adc和tim不一样，adc的模式没有tim那么多，所以不需要像tim那样走函数指针
 * @note        平台相关的不允许出现在 hal.h 中，必须出现在 hal.c 中
 */
#ifndef HAL_ADC_H
#define HAL_ADC_H

#include "buffer.h"
#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /*===========================================================================================================================================================*/
    /* 宏定义 */
    /*===========================================================================================================================================================*/

#ifndef HAL_ADC_MAX_CHANNELS
#define HAL_ADC_MAX_CHANNELS 16
#endif

#ifndef DTS_DMA_BUFFER_SIZE
#define DMA_BUFFER_SIZE 1024
#endif

    /*===========================================================================================================================================================*/
    /* 平台私有扩展配置 */
    /*===========================================================================================================================================================*/

    /**
     * @brief ADC HAL 内部私有运行时上下文
     * @note  供 DMA 中断路径使用，由 VFS 层分配内存并绑定到 host->private_cfg
     */
    struct hal_adc_private_cfg
    {
        bool dma_it_enable; /**< DMA 中断模式使能标志 */
        uint16_t dma_raw_data_buf[DMA_BUFFER_SIZE] COMPAT_ALIGNED(32); /**< DMA 原始采样缓冲区 */
        struct fifo_spsc dma_buffer_handle; /**< DMA 中断 FIFO 句柄 */
        fifo_data_type
            dma_data_buf[DMA_BUFFER_SIZE] COMPAT_ALIGNED(32); /**< DMA 中断 FIFO 数据缓冲区 */
    };

    /*===========================================================================================================================================================*/
    /* ADC 引脚与通道基础配置 */
    /*===========================================================================================================================================================*/

    /**
     * @brief ADC 引脚底层物理配置结构体
     */
    typedef struct hal_adc_gpio_cfg
    {
        uintptr_t port; /**< GPIO 端口基地址或端口号 (支持 32/64 位平台指针转换) */
        uint16_t pin; /**< 引脚编号 */
        uint32_t clk_bus; /**< 该引脚所属的外设时钟总线 */
        uint32_t af; /**< 引脚复用功能设置 (Alternate Function 选择) */
        uint32_t output_type; /**< 引脚输出类型 */
        uint32_t speed; /**< 引脚速度 */
        uint32_t mode; /**< 引脚工作模式 */
        uint32_t pull; /**< 引脚上拉/下拉 */
    } hal_adc_gpio_config;

    /**
     * @brief ADC 通道配置结构体
     */
    typedef struct hal_adc_channel_cfg
    {
        uint32_t channel_id; /**< 硬件通道号 */
        uint32_t rank; /**< 序列排队次序 */
        uint32_t sample_time; /**< 硬件采样周期时间 */
        uint32_t diff_mode; /**< 单端/差分输入选择 */
        uint32_t attenuation; /**< 衰减器配置 */
    } hal_adc_channel_config;

    /*===========================================================================================================================================================*/
    /* 多模式与 DMA 配置 */
    /*===========================================================================================================================================================*/

    /**
     * @brief ADC 多实例联动公共配置
     */
    typedef struct hal_adc_multi_cfg
    {
        uint32_t multimode; /**< 多模式选择 */
        uint32_t common_clock; /**< 全局公共时钟分频源 */
        uint32_t multi_dma; /**< 多模式下的多路全局 DMA 传输控制 */
        uint32_t sampling_delay; /**< 交替/交叉采样模式下的交错延迟周期数 */
    } hal_adc_multi_config;

    /**
     * @brief DMA 配置
     */
    typedef struct hal_adc_dma_cfg
    {
        uint32_t dma_it_enable; /**< DMA 中断模式使能标志 */
        uint32_t dma_enable; /**< DMA 模式使能标志 */
        uintptr_t dma_handle; /**< DMA 寄存器基地址或系统原生硬句柄 */
        uint32_t dma_stream; /**< DMA 流选择 */
        uint32_t dma_channel; /**< DMA 通道选择 */
        uint32_t dma_priority; /**< DMA 优先级选择 */
        uint32_t dma_memory_size; /**< DMA 内存大小选择 */
        uint32_t dma_direction; /**< DMA 传输方向 (外设→内存 / 内存→外设) */
        uint32_t dma_mode; /**< DMA 传输模式 (正常 / 循环) */
        uint32_t dma_periph_inc; /**< DMA 外设地址自增模式 */
        uint32_t dma_mem_inc; /**< DMA 内存地址自增模式 */
        uint32_t dma_periph_data_size; /**< DMA 外设端数据宽度 */
        uint32_t dma_fifo_mode; /**< DMA FIFO 模式控制 */
        uint32_t dma_fifo_threshold; /**< DMA FIFO 阈值 */
        uint32_t dma_mem_burst; /**< DMA 内存突发传输大小 */
        uint32_t dma_periph_burst; /**< DMA 外设突发传输大小 */
    } hal_adc_dma_config;

    /*===========================================================================================================================================================*/
    /* ADC 核心运行参数配置 */
    /*===========================================================================================================================================================*/

    /**
     * @brief ADC 寄存器级核心运行属性配置
     */
    typedef struct hal_adc_cfg
    {
        /**软件直射配置*/
        int32_t channel_num; /**< 动态维护的有效通道数量 */
        int32_t adc_clk_bus; /**< ADC 外设所属的总线时钟 */
        int32_t internal_ch_enable; /**< 内部特殊通道供电开关使能 */
        /*--------------------------------硬件直射配置--------------------------------*/
        int32_t resolution; /**< 分辨率设置 */
        int32_t align; /**< 数据对齐模式 */
        int32_t EOC_flag; /**< 转换结束标志触发逻辑 */
        int32_t sequencer_mode; /**< 扫描/序列器模式使能控制 */
        int32_t continuous_mode; /**< 连续转换模式控制 */
        int32_t TriggerSource; /**< 触发大类选择 */
        int32_t trigger_src; /**< 具体硬件触发源物理映射 ID */
        int32_t SequencerLength; /**< 序列器硬件长度寄存器掩码 */
        int32_t SequencerDiscont; /**< 序列器间断/间歇转换模式控制 */
        int32_t dma_mode; /**< DMA 数据搬运模式选择 */
        int32_t output_buf; /**< 输出缓冲使能开关 */
        uint32_t internal_ch_select; /**< 内部通道选择位掩码 (替代 LL_ADC_PATH_INTERNAL_*) */
        uint32_t dma_reg_mode; /**< DMA 规则组数据寄存器模式 (替代 LL_ADC_DMA_REG_REGULAR_DATA) */
        uint32_t sw_trigger; /**< 软件触发源选择 (替代 LL_ADC_REG_TRIG_SOFTWARE) */
    } hal_adc_config;

    /*===========================================================================================================================================================*/
    /* 类型重定义与对象聚合 */
    /*===========================================================================================================================================================*/

    /**
     * @brief 平台唯一配置结构体
     * @note adc_handle 存储 ADC 寄存器基址, 由 hal_adc.c 在 device_init 中填入
     */
    struct hal_adc_platform_unique_cfg
    {
        uintptr_t private_cfg; /**< 平台私有配置 */
    };
    typedef struct hal_adc_platform_unique_cfg hal_adc_platform_unique_config;

    /**
     * @brief ADC 主机配置结构体
     */
    typedef struct hal_adc_host_cfg
    {
        hal_adc_dma_config dma_cfg; /**< DMA 配置 */
        uintptr_t adc_handle; /**< ADC 寄存器基地址 */
        hal_adc_gpio_config gpio_cfg; /**< 物理 GPIO 配置 */
        hal_adc_config config; /**< ADC 寄存器直投配置属性 */
        hal_adc_channel_config* channels; /**< 通道结构体数组首地址 */
        uint32_t channel_count; /**< 当前有效通道总数 */
        hal_adc_multi_config* multi_cfg; /**< 多 ADC 同步联动公共配置 */
        struct hal_adc_private_cfg* private_cfg; /**< 平台特殊私有扩展配置 */
        uint32_t dev_index; /**< 设备索引 */
    } hal_adc_host_config;

    /**
     * @brief ADC 设备驱动核心上下文
     */
    typedef struct hal_adc_device
    {
        hal_adc_host_config* host; /**< 指向当前主机配置的指针 */
        hal_adc_platform_unique_config* unique; /**< 指向底层芯片平台特性的不透明指针 */
    } hal_adc_device;

    /*===========================================================================================================================================================*/
    /* 硬件直投层核心 API */
    /*===========================================================================================================================================================*/
    /**
     * @brief 初始化 ADC 设备
     * @param pdev ADC 设备指针
     * @param unique_cfg 平台唯一配置指针
     * @param host host 配置指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_device_init(hal_adc_device* pdev,
                                                      hal_adc_platform_unique_config* unique_cfg,
                                                      hal_adc_host_config* host);

    /**
     * @brief 释放 ADC 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_device_deinit(hal_adc_device* pdev);

    /**
     * @brief 初始化 ADC 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_init(hal_adc_device* pdev);
    /**
     * @brief 关闭 ADC 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_deinit_all_adcx(hal_adc_device* pdev);
    /**
     * @brief 关闭 ADC 设备通道
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_deinit_adcx_channel(hal_adc_device* pdev,
                                                              uint32_t channel_id);
    /**
     * @brief 启动 ADC 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_start(hal_adc_device* pdev);
    /**
     * @brief 停止 ADC 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_stop(hal_adc_device* pdev);
    /**
     * @brief 读取 ADC 设备值
     * @param pdev ADC 设备指针
     * @param channel_num 通道数量
     * @param out_val 输出值
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_read_value(hal_adc_device* pdev, uint32_t channel_num,
                                                     uint16_t* out_val);
    /**
     * @brief 轮询等待 ADC 设备转换完成
     * @param pdev ADC 设备指针
     * @param out_status 输出状态
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_poll_for_conversion(hal_adc_device* pdev,
                                                              uint32_t* out_status);
    /**
     * @brief 获取 ADC 设备通道数量
     * @param pdev ADC 设备指针
     * @param count 输出通道数量
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_get_channel_count(hal_adc_device* pdev, uint32_t* count);
    /**
     * @brief 获取 ADC 设备通道ID
     * @param pdev ADC 设备指针
     * @param index 通道索引
     * @param channel_id 输出通道ID
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_get_channel_id(hal_adc_device* pdev, int index,
                                                         uint32_t* channel_id);
    /**
     * @brief 获取 ADC 设备通道采样时间
     * @param pdev ADC 设备指针
     * @param index 通道索引
     * @param sample_time 输出采样时间
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_get_channel_sample_time(hal_adc_device* pdev, int index,
                                                                  uint32_t* sample_time);
    /**
     * @brief 启动 DMA 设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_dma_start(hal_adc_device* pdev);
    /**
     * @brief 启动 DMA 中断设备
     * @param pdev ADC 设备指针
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_dma_it_start(hal_adc_device* pdev);
    /**
     * @brief 读取 DMA 中断设备值
     * @param pdev ADC 设备指针
     * @param out_val 输出值
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_dma_it_read_value(hal_adc_device* pdev,
                                                            uint16_t* out_val);
    /**
     * @brief 读取 DMA 设备值
     * @param pdev ADC 设备指针
     * @param out_val 输出值
     * @return int 错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_adc_dma_read_value(hal_adc_device* pdev, uint16_t* out_val);

    /**
     * @brief ADC 虚拟中断上半部回调 (ISR 内执行)
     * @param arg 参数 (hal_adc_device*)
     * @param irq_num 虚拟中断号
     * @return 1 表示需要 schedule 下半部; 0 表示不需要
     */
    int hal_virtual_adc_irq_callback(void* arg, uint16_t irq_num);

    /**
     * @brief ADC DMA 下半部处理函数 (主循环上下文执行 fifo 拷贝)
     * @param arg 参数 (hal_adc_device*)
     */
    void hal_adc_dma_bottom_half_handler(void* arg);

#ifdef __cplusplus
}
#endif

#endif /* HAL_ADC_H */
