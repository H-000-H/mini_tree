/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_tim.h
 *@brief 通用定时器硬件直投层(HAL)接口定义和tim用法扩展说明
 *@author H-000-H
 *@details
 *   @note
 *   和定时器相关的配置和操作都放在hal_tim.h和hal_tim.c中,不区分PWM,输入捕获,输出比较,编码器,hall传感器
 *   @note        不同模式是需要你去dtsi中自己去选择不同的模式,hal只是提供一个统一的接口让你去使用
 *   @details     本文件定义了定时器在热路径(Hot-Path)下的高效率配置接口。
 *   @note        所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 *   @note        由于TIM是快速热路径外设所以TIM的初始化与配置应该尽量在硬件直投层完成
 *   @note        vfs  层只负责拉取TIM的配置并传递给HAL层并且对hal层提供的api进行内联封装
 *   @note        文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码
 *   @note        获取参数不能直接返回，必须通过指针参数传递
 *   @note        平台相关的不允许出现在hal.h中，必须出现在hal.c中
 */

#ifndef HAL_TIM_H
#define HAL_TIM_H

#include "compiler_compat.h"
#include "status.h"
#include <dt-bindings/tim/tim-parameter.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
/*===========================================================================================================================================================*/
/* 宏定义 */
/*===========================================================================================================================================================*/
#ifndef HAL_BASE_TIM_MAX_CHANNELS
#define HAL_BASE_TIM_MAX_CHANNELS 4 /**< 基础定时器最大通道数 DTS没有定义时使用默认值 */
#endif
#ifndef HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS
#define HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS 4 /**< 输出比较定时器最大通道数 DTS没有定义时使用默认值 */
#endif
#ifndef HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS
#define HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS 4 /**< 输入捕获定时器最大通道数 DTS没有定义时使用默认值 */
#endif
#ifndef HAL_ENCODER_TIM_MAX_CHANNELS
#define HAL_ENCODER_TIM_MAX_CHANNELS 2 /**< 编码器定时器最大通道数 DTS没有定义时使用默认值 */
#endif
#ifndef HAL_HALL_TIM_MAX_CHANNELS
#define HAL_HALL_TIM_MAX_CHANNELS 3 /**< 霍尔传感器定时器最大通道数 DTS没有定义时使用默认值 */
#endif

/** 用于通道实体数组分配的绝对上限（取上述各通道最大值的上限值） */
#define HAL_TIM_CHANNELS_INSTANCE_MAX 4
    /**
     * @brief TIM 引脚底层物理配置结构体
     */
    struct hal_tim_pin_cfg
    {
        uintptr_t port; /**< GPIO 端口基地址或端口号 (支持 32/64 位平台指针转换) */
        uint16_t pin; /**< 引脚编号 (如 GPIO_PIN_0) */
        uint32_t clk_bus; /**< 该引脚所属的外设时钟总线 */
        uint32_t af; /**< 引脚复用功能设置 (Alternate Function 选择) */
        uint32_t output_type; /**< 引脚输出类型 */
        uint32_t speed; /**< 引脚速度 */
        uint32_t mode; /**< 引脚模式 */
        uint32_t pull; /**< 引脚上拉/下拉 */
    };

    /**
     * @brief TIM 通道业务配置结构体
     */
    struct hal_tim_chn_config_t
    {
        uint32_t channel_id; /**< 通道号：1..HAL_TIM_CHANNELS_INSTANCE_MAX */
        uint32_t mode; /**< 当前通道的工作模式（如 PWM1, PWM2, 输入捕获） */
        uint32_t polarity; /**< 极性 / 触发边沿 */
        uint32_t filter; /**< 滤波器配置（0表示禁用） */
        uint32_t prescaler; /**< 通道分频（输入捕获时用） */
        uint32_t enable_complementary; /**< 是否开启互补输出（高级定时器 CHxN） */
    };

    /**
     * @brief TIM 基础时基配置结构体
     */
    struct hal_tim_base_cfg
    {
        uint32_t prescaler; /**< 预分频器值 (PSC) */
        uint32_t counter_mode; /**< 计数模式 (LL_TIM_COUNTERMODE_* 直投) */
        uint32_t autoreload; /**< 自动重装载值 (ARR) */
        uint32_t clock_division; /**< 时钟分频因子 (用于死区/滤波器采样) */
        uint32_t repetition_counter; /**< 重复计数器值 (RCR，仅高级定时器有效) */
    };

    /*===========================================================================================================================================================*/
    /* 各工作模式特定配置（用于Union） */
    /*===========================================================================================================================================================*/

    /**
     * @brief 输出比较配置结构体
     */
    struct hal_output_compare_cfg
    {
        uint32_t compare_value; /**< 初始比较值/占空比计数值 */
        uint32_t oc_mode; /**< 输出比较模式 */
        uint32_t oc_state; /**< 输出比较状态 */
        uint32_t oc_polarity; /**< 输出比较极性 */
        uint32_t oc_idle_state; /**< 输出比较空闲状态 */
        uint32_t oc_n_state; /**< 输出比较互补状态 */
        uint32_t oc_n_polarity; /**< 输出比较互补极性 */
        uint32_t oc_n_idle_state; /**< 输出比较互补空闲状态 */
    };

    /**
     * @brief 输入捕获配置结构体
     */
    struct hal_input_capture_cfg
    {
        uint32_t polarity; /**< 捕获触发边沿 (LL_TIM_IC_POLARITY_* 直投) */
        uint32_t filter; /**< 输入数字滤波器配置 */
        uint32_t prescaler; /**< 输入分频器 */
        uint32_t active_input; /**< 输入源映射 (LL_TIM_ACTIVEINPUT_* 直投) */
    };

    /**
     * @brief 编码器硬件配置结构体
     */
    typedef struct hal_encoder_cfg
    {
        uint32_t mode; /**< 编码器计数模式：看A相、看B相，还是双沿计数（1/2/4倍频） */
        uint32_t period; /**< 计数自动重装载值（ARR） */
        /**< A相配置 */
        uint32_t ic1_active_input; /**< A相输入源 */
        uint32_t ic1_polarity; /**< A相极性 */
        uint32_t ic1_filter; /**< A相滤波 */
        uint32_t ic1_prescaler; /**< A相分频器 */
        /**< B相配置 */
        uint32_t ic2_active_input; /**< B相输入源 */
        uint32_t ic2_polarity; /**< B相极性 */
        uint32_t ic2_filter; /**< B相滤波 */
        uint32_t ic2_prescaler; /**< B相分频器 */
    } hal_encoder_cfg;

    /**
     * @brief 编码器配置结构体
     */
    typedef struct hal_encoder_config
    {
        hal_encoder_cfg hw_cfg; /**< 编码器硬件配置 */
        uint32_t pulse_per_rev; /**< 编码器线数（PPR） */
        float reduction_ratio; /**< 减速比 */
        int32_t total_count; /**< 软件累加的总脉冲数 */
        int16_t overflow_num; /**< 溢出圈数 */
        float current_velocity; /**< 当前转速 */
    } hal_encoder_config;

    /**
     * @brief 霍尔传感器配置结构体
     */
    struct hal_hall_cfg
    {
        uint32_t hall_polarity; /**< 霍尔传感器极性 */
        uint32_t hall_filter_time; /**< 霍尔信号去抖时间 */
        uint32_t hall_prescaler; /**< 霍尔传感器分频器 */
        uint32_t hall_commutation_delay_time; /**< 换向延时时间 */
    };

    /**
     * @brief 断路器配置结构体
     */
    struct hal_bdtr_cfg
    {
        uint32_t automatic_output; /**< 自动输出使能 (LL_TIM_AUTOMATICOUTPUT_ENABLE/DISABLE) */
        uint32_t break_state; /**< 刹车使能 (LL_TIM_BREAK_ENABLE/DISABLE) */
        uint32_t break_polarity; /**< 刹车极性 (LL_TIM_BREAK_POLARITY_HIGH/LOW) */
        uint32_t break_filter; /**< 刹车输入滤波器 */
        uint32_t ossi_state; /**< 空闲状态 OSSI (LL_TIM_OSSI_ENABLE/DISABLE) */
        uint32_t ossr_state; /**< 运行状态 OSSR (LL_TIM_OSSR_ENABLE/DISABLE) */
        uint32_t dead_time; /**< 死区 DTG 编码 (直灌 BDTR, 非纯时钟周期数) */
        uint32_t lock_level; /**< 锁级别 (LL_TIM_LOCKLEVEL_OFF/1/2/3) */
    };

    /*===========================================================================================================================================================*/
    /* 类型重定义与对象聚合 */
    /*===========================================================================================================================================================*/

    typedef struct hal_tim_base_cfg hal_tim_base_config;
    typedef struct hal_input_capture_cfg hal_input_capture_config;
    typedef struct hal_output_compare_cfg hal_output_compare_config;
    typedef struct hal_tim_chn_config_t hal_tim_channel_config;
    typedef struct hal_tim_pin_cfg hal_tim_pin_config;
    typedef struct hal_bdtr_cfg hal_bdtr_config;
    typedef struct hal_hall_cfg hal_hall_config;

    /**
     * @brief TIM 主机配置结构体
     * @note  寄存器基址 / 时钟使能 / 中断号 / 中断掩码等字段跨平台语义一致,
     *        仅底层 API 调用不同 (ST: LL_TIM, CH32: 标准库, ESP32: gptimer),
     *        故统一放在 host_config 中, 不再单列 platform_unique_cfg。
     */
    typedef struct hal_tim_host_config
    {
        uintptr_t tim_handle; /**< 寄存器基址 / 外设句柄 (DTS hw-instance) */
        uint32_t clk_periph; /**< 时钟使能位 (DTS clk-periph) */
        int irqn; /**< 中断号 (DTS irqn, -1 = 无中断) */
        uint32_t irq_priority; /**< NVIC 中断优先级 (DTS irq-priority, 0=最高) */
        uint32_t int_mask; /**< 中断使能掩码 (DTS interrupt-mask, 0 = 不使能) */
        hal_tim_base_config base; /**< 定时器基础时基配置 */
        uint32_t mode; /**< 定时器全局工作模式选择 */
        uint32_t active_chn_mask; /**< 激活的通道掩码（如按位表示：1<<1 | 1<<2 激活通道1和2） */

        union
        {
            /* 1. 多通道输出比较/PWM 模式 */
            struct
            {
                hal_output_compare_config config[HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS]; /**< 输出比较配置 */
                hal_tim_channel_config channel[HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS]; /**< 输出比较模式通道配置 */
                hal_tim_pin_config pin[HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS]; /**< 输出比较模式引脚配置 */
            } oc_mode;

            /* 2. 多通道输入捕获模式 */
            struct
            {
                uintptr_t ic_handle; /**< 指向底层芯片特有的输入捕获寄存器块或句柄 */
                hal_input_capture_config config[HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS]; /**< 输入捕获配置 */
                hal_tim_channel_config channel[HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS]; /**< 输入捕获模式通道配置 */
                hal_tim_pin_config pin[HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS]; /**< 输入捕获模式引脚配置 */
            } ic_mode;

            /* 3. 正交编码器接口模式（固定双通道、双引脚） */
            struct
            {
                uintptr_t encoder_handle; /**< 指向底层芯片特有的编码器寄存器块或句柄 */
                hal_encoder_config config; /**< 编码器配置 */
                hal_tim_channel_config channel[HAL_ENCODER_TIM_MAX_CHANNELS]; /**< 编码器模式通道配置 */
                hal_tim_pin_config pin[HAL_ENCODER_TIM_MAX_CHANNELS]; /**< 编码器模式引脚配置 */
            } encoder_mode;

            /* 4. 霍尔传感器接口模式（硬件级异或：3引脚，借道1个捕获通道） */
            struct
            {
                uintptr_t hall_handle; /**< 指向底层芯片特有的霍尔传感器寄存器块或句柄 */
                hal_hall_config config; /**< 霍尔传感器配置 */
                hal_tim_channel_config capture_channel; /**< 硬件映射的目标捕获通道（STM32通常固定为CH1） */
                hal_tim_pin_config phase_pins[HAL_HALL_TIM_MAX_CHANNELS]; /**< 接入的 U, V, W 3相物理引脚 */
            } hall_mode;
        };
        hal_bdtr_config bdtr; /**< 断路器与死区配置，属于高级定时器全局联动属性 */
    } hal_tim_host_config;

    /**
     * @brief TIM 通道实体（热路径下用于快速响应中断或执行回调）
     */
    struct hal_tim_chn_t
    {
        hal_tim_channel_config config; /**< 当前通道的配置状态快照 */
        void* priv_data; /**< 指向通道底层平台特有数据的指针（如STM32通道寄存器块） */
        void (*callback)(void* arg); /**< 通道专属中断回调函数 */
        void* callback_arg; /**< 通道中断回调私有参数 */
    };

    /**
     * @brief TIM 平台唯一配置结构体 (对齐 ADC/DAC)
     */
    typedef struct hal_tim_platform_unique_cfg
    {
        uintptr_t private_cfg; /**< 平台私有配置 */
    } hal_tim_platform_unique_config;

    /**
     * @brief TIM 设备驱动核心上下文 (对齐 ADC/DAC: host + unique)
     */
    typedef struct hal_tim_device
    {
        hal_tim_host_config* host; /**< 指向当前主机配置的指针 */
        hal_tim_platform_unique_config* unique; /**< 指向平台唯一配置的指针 */
    } hal_tim_device;

    /*===========================================================================================================================================================*/
    /* 硬件直投层核心 API */
    /*===========================================================================================================================================================*/
    /**
     * @brief 绑定 TIM 设备与平台/主机配置
     * @param[in] pdev TIM 设备对象指针
     * @param[in] unique 平台唯一配置指针
     * @param[in] host 主机配置指针 (DTSI 直投)
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_device_init(hal_tim_device* pdev, hal_tim_platform_unique_config* unique, hal_tim_host_config* host);
    /**
     * @brief 释放 TIM 设备运行时资源
     * @param[in] pdev TIM 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_device_deinit(hal_tim_device* pdev);

    /**
     * @brief 打开 TIM 设备 (引用计数 +1)
     * @param[in] pdev TIM 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_open(hal_tim_device* pdev);
    /**
     * @brief 关闭 TIM 设备 (引用计数 -1)
     * @param[in] pdev TIM 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_close(hal_tim_device* pdev);

    /**
     * @brief 定时器占空比/重装载同步更新接口（热路径核心）
     * @param[in] pdev 定时器设备句柄
     * @param[in] channel 目标通道号 (1..4)
     * @param[in] frequency 目标频率 (Hz)
     * @param[in] duty 占空比值 (或者是直接对应的 Compare 寄存器数值，依底层设计而定)
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_pwm_update(hal_tim_device* pdev, uint32_t channel, uint32_t frequency, uint32_t duty);

    /**
     * @brief 定时器中断配置接口
     * @param[in] pdev 定时器设备句柄
     * @param[in] interrupt_config 中断配置
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_interrupt_config(hal_tim_device* pdev, uint32_t interrupt_config);
    /**
     * @brief 获取定时器当前计数值 (热路径读取 CNT)
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传当前计数值
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_counter(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 获取指定通道的捕获值 (输入捕获模式)
     * @param[in] pdev 定时器设备句柄
     * @param[in] channel 通道号 (1..4)
     * @param[out] value 回传捕获的计数值
     * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_capture_value(const hal_tim_device* pdev, uint32_t channel, uint32_t* value);
    /**
     * @brief 获取编码器累计计数值
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传编码器计数值
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_encoder_value(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 获取霍尔换向捕获值
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传霍尔捕获值
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_hall_value(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 强制停止定时器接口
     * @param[in] pdev 定时器设备句柄
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_force_stop(hal_tim_device* pdev);

    /**
     * @brief 启动编码器接口
     * @param[in] pdev 定时器设备句柄
     * @param[in] encoder_mode 编码器倍频模式（如：1, 2, 4 倍频）
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_encoder_start(hal_tim_device* pdev, uint32_t encoder_mode);

    /**
     * @brief 启动霍尔接口换向捕获模式
     * @param[in] pdev 定时器设备句柄
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_hall_start(hal_tim_device* pdev);

    /**
     * @brief 设置定时器计数值 (写入 CNT)
     * @param[in] pdev 定时器设备句柄
     * @param[in] value 目标计数值
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_set_counter(hal_tim_device* pdev, uint32_t value);
    /**
     * @brief 设置自动重装载值 (ARR)
     * @param[in] pdev 定时器设备句柄
     * @param[in] value 自动重装载值
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_set_autoreload(hal_tim_device* pdev, uint32_t value);
    /**
     * @brief 获取自动重装载值 (ARR)
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传自动重装载值
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_autoreload(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 设置预分频器 (PSC)
     * @param[in] pdev 定时器设备句柄
     * @param[in] value 预分频系数
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_set_prescaler(hal_tim_device* pdev, uint32_t value);
    /**
     * @brief 获取预分频器 (PSC)
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传预分频系数
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_prescaler(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 设置时钟分频因子 (用于死区/滤波采样)
     * @param[in] pdev 定时器设备句柄
     * @param[in] value 时钟分频因子
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_set_clock_division(hal_tim_device* pdev, uint32_t value);
    /**
     * @brief 获取时钟分频因子
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传时钟分频因子
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_clock_division(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 设置计数模式 (向上/向下/中心对齐)
     * @param[in] pdev 定时器设备句柄
     * @param[in] value 计数模式 (LL_TIM_COUNTERMODE_*)
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_set_counter_mode(hal_tim_device* pdev, uint32_t value);
    /**
     * @brief 获取计数模式
     * @param[in] pdev 定时器设备句柄
     * @param[out] value 回传计数模式
     * @return 成功返回 MINI_OK, pdev 或 value 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_get_counter_mode(const hal_tim_device* pdev, uint32_t* value);
    /**
     * @brief 使能 ARR 预装载 (影子寄存器)
     * @param[in] pdev 定时器设备句柄
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_enable_arr_preload(hal_tim_device* pdev);
    /**
     * @brief 禁用 ARR 预装载 (立即生效)
     * @param[in] pdev 定时器设备句柄
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_disable_arr_preload(hal_tim_device* pdev);
    /**
     * @brief 启动定时器基准计数
     * @param[in] pdev 定时器设备句柄
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_base_start(hal_tim_device* pdev);
    /**
     * @brief 停止定时器基准计数
     * @param[in] pdev 定时器设备句柄
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_base_stop(hal_tim_device* pdev);

    /**
     * @brief 清除 TIM update 标志, 供 ISR top_half 调用 (避免中断子系统依赖 LL_TIM)
     * @param[in] pdev 定时器设备句柄
     * @return 成功返回 MINI_OK; 无 update flag (spurious IRQ) 返回 MINI_ERR_IO; 非法参数返回 MINI_ERR_INVAL
     */
    int COMPAT_WARN_UNUSED_RESULT hal_tim_clear_update_flag(hal_tim_device* pdev);

#ifdef __cplusplus
}
#endif

#endif /* HAL_TIM_H */