/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_dac.h
 *@brief DAC HAL 层 — 硬件抽象接口, 硬件直投层
 *@author H-000-H
 *@details
 *   @note        所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 *   @note        由于 DAC 是热路径外设所以 DAC 的初始化与配置应该尽量在硬件直投层完成
 *   @note        文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码
 *   @note        获取参数不能直接返回，必须通过指针参数传递
 *   @note 禁止使用enum，enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */

#ifndef HAL_DAC_H
#define HAL_DAC_H

#include "buffer.h"
#include "compiler_compat.h"
#include "status.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief GPIO 配置
     */
    typedef struct hal_dac_gpio_cfg
    {
        uintptr_t port; /**< GPIO 端口基地址或端口号 (支持 32/64 位平台指针转换) */
        uint16_t pin; /**< 引脚编号 (如 GPIO_PIN_0) */
        uint32_t clk_bus; /**< 该引脚所属的外设时钟总线 */
        uint32_t af; /**< 引脚复用功能设置 (Alternate Function 选择) */
        uint32_t output_type; /**< 引脚输出类型 */
        uint32_t speed; /**< 引脚速度 */
        uint32_t mode; /**< 引脚模式 */
        uint32_t pull; /**< 引脚上拉/下拉 */
    } hal_dac_gpio_config;

    /**
     * @brief DAC 配置
     */
    typedef struct hal_dac_cfg
    {
        uint32_t it_enable; /**< DAC 中断模式使能标志 */
        int32_t irqn; /**< NVIC 中断号 (DTS irqn, -1 = 无中断) */
        uint32_t irq_priority; /**< NVIC 中断优先级 (DTS irq-priority, 0=最高) */
        uint32_t channel; /**< 通道选择 (LL_DAC_CHANNEL_1 / LL_DAC_CHANNEL_2) */
        uint32_t trigger_source; /**< 触发源 */
        uint32_t data_align; /**< 数据对齐模式 */
        uint32_t output_buf; /**< 输出缓冲: 0 = 不缓冲, 1 = 缓冲 */
        uint32_t dac_clk_periph; /**< DAC 外设所属的总线时钟 (与 LL_xxx_GRP1_PERIPH_DACx 一致) */
        uint32_t dma_enable; /**< DMA 模式: 0 = 软件写入, 1 = DMA 自动输出 */
        uint32_t wave_auto_generation_mode; /**< 波形自动生成模式 */
        uint32_t wave_auto_generation_config; /**< 波形自动生成配置 */
        uint32_t dac_sw_trigger; /**< 软件触发源标识 (替代 LL_DAC_TRIG_SOFTWARE) */
        uint32_t dma_data_align; /**< DMA 数据寄存器对齐模式 (替代
                                    LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED) */
    } hal_dac_config;

    /**
     * @brief DAC DMA 配置结构体
     */
    typedef struct hal_dac_dma_cfg
    {
        uint32_t dma_mode; /**< DMA 模式 */
        uintptr_t dma_handle; /**< DMA 寄存器基地址或系统原生硬句柄 */
        uint32_t dma_stream; /**< DMA 流选择 */
        uint32_t dma_channel; /**< DMA 通道选择 */
        uint32_t dma_priority; /**< DMA 优先级选择 */
        uint32_t dma_buffer_size; /**< DMA 缓冲区大小选择 */
        uint32_t dma_data_size; /**< DMA 数据大小选择(1字 字节 半字) */
        uint32_t dma_fifo_is_enable; /**< DMA 缓冲区 FIFO 使能标志 */
        uint32_t dma_fifo_mode; /**< DMA 缓冲区 FIFO 模式 */
        uint32_t dma_mem_burst; /**< DMA 缓冲区内存突发模式 */
        uint32_t dma_periph_burst; /**< DMA 缓冲区外设突发模式 */
        struct fifo_spsc* dma_fifo; /**< 默认DMA 缓冲区 FIFO */
        uint32_t dma_direction; /**< DMA 传输方向 (替代 LL_DMA_DIRECTION_MEMORY_TO_PERIPH) */
        uint32_t dma_periph_inc; /**< DMA 外设地址增量模式 (替代 LL_DMA_PERIPH_NOINCREMENT) */
        uint32_t dma_mem_inc; /**< DMA 内存地址增量模式 (替代 LL_DMA_MEMORY_INCREMENT) */
        uint32_t dma_periph_data_size; /**< DMA 外设数据宽度 */
        uint32_t dma_fifo_threshold; /**< DMA FIFO 阈值 */
    } hal_dac_dma_config;

    /**
     * @brief DAC 主机配置结构体
     */
    typedef struct hal_dac_host_cfg
    {
        hal_dac_dma_config dma_cfg; /**< DMA 配置 */
        uintptr_t dac_handle; /**< DAC 寄存器基地址 */
        hal_dac_gpio_config gpio_cfg; /**< 物理 GPIO 配置 */
        hal_dac_config config; /**< DAC 寄存器直投配置属性 */
    } hal_dac_host_config;

    /**
     * @brief 平台唯一配置结构体
     */
    typedef struct hal_dac_platform_unique_cfg
    {
        uintptr_t private_cfg; /**< 平台私有配置 */
    } hal_dac_platform_unique_config;

    /**
     * @brief DAC 设备
     */
    typedef struct hal_dac_dev
    {
        hal_dac_host_config* host; /**< 指向当前主机配置的指针 */
        hal_dac_platform_unique_config* unique; /**< 指向底层芯片平台特性的不透明指针 */
    } hal_dac_device;

    /*===========================================================================================================================================================*/
    /* 硬件直投层核心 API */
    /*===========================================================================================================================================================*/

    /**
     * @brief 绑定 DAC 设备与主机/平台配置
     * @param[in] pdev DAC 设备指针
     * @param[in] host_cfg 主机配置指针
     * @param[in] unique_cfg 平台唯一配置指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_device_init(hal_dac_device* pdev, hal_dac_host_config* host_cfg, hal_dac_platform_unique_config* unique_cfg);

    /**
     * @brief 关闭 DAC 设备
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_close(hal_dac_device* pdev);

    /**
     * @brief 初始化 DAC 外设寄存器
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_init(hal_dac_device* pdev);

    /**
     * @brief 启动 DAC 输出（含 DMA / 软件触发路径）
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_start(hal_dac_device* pdev);

    /**
     * @brief 暂停 DMA 模式 DAC 输出（保留 DMA 配置与当前位置）
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_dma_pause(hal_dac_device* pdev);

    /**
     * @brief 暂停非 DMA 模式 DAC 输出（保留触发与寄存器配置）
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_base_pause(hal_dac_device* pdev);

    /**
     * @brief 暂停 DAC 输出（按 DMA / 非 DMA 模式分发）
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_pause(hal_dac_device* pdev);

    /**
     * @brief 恢复 DAC 输出
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_resume(hal_dac_device* pdev);

    /**
     * @brief 设置 DAC 输出值
     * @param[in] pdev DAC 设备指针
     * @param[in] value 输出值
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_set_value(hal_dac_device* pdev, uint32_t value);

    /**
     * @brief 获取 DAC 输出值
     * @param[in] pdev DAC 设备指针
     * @param[in] value 输出值指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_get_value(hal_dac_device* pdev, uint32_t* value);

    /**
     * @brief 获取 DMA 当前发送进度
     * @param[in] pdev DAC 设备指针
     * @param[in] remaining 返回剩余未发送的数据个数
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_get_dma_progress(hal_dac_device* pdev, uint32_t* remaining);

    /**
     * @brief 向 DMA 缓冲区写入一段波形数据
     * @param[in] pdev DAC 设备指针
     * @param[in] data 待写入的数据源指针
     * @param[in] len 待写入的数据长度
     * @return 成功返回写入长度, 失败返回负数错误码
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_write_dma_buffer(hal_dac_device* pdev, const uint16_t* data, uint32_t len);

    /**
     * @brief 停止 DMA 并彻底复位硬件状态
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 非 DMA 模式返回 MINI_ERR_AGAIN, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_stop_dma(hal_dac_device* pdev);

    /**
     * @brief 停止非 DMA 模式 DAC 输出
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_base_stop(hal_dac_device* pdev);

    /**
     * @brief 强制停止 DAC 输出（按 DMA / 非 DMA 模式分发）
     * @param[in] pdev DAC 设备指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_dac_force_stop(hal_dac_device* pdev);

#ifdef __cplusplus
}
#endif

#endif /* HAL_DAC_H */
