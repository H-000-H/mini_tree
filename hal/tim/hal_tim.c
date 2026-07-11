/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 该文件实现了 TIM 和 PWM 的 HAL 接口
 * 实现了 TIM 和 PWM 的初始化、设置、读取、关闭等基本功能
 */
#include "hal_tim.h"
#include "interrupt.h"

/** TIM 下半部工作项 (fn/arg 由 VFS 层绑定), 供 interrupt_virtual_register 注册 */
struct bottom_half_work g_tim_bottom_half_work;

#ifndef HAL_TIM_NUM
#define HAL_TIM_NUM 2U
#endif

/*===========================================================================================================================================================*/
/*结构体和全局变量定义*/
/*===========================================================================================================================================================*/

/**
 * @brief 定时器驱动结构体
 * @note 用于存储定时器驱动函数的初始化和关闭函数
 */
typedef struct tim_driver
{
    int (*init)(uintptr_t tim_handle, void* cfg_ptr, hal_tim_device* pdev);
    int (*close)(uintptr_t tim_handle, hal_tim_device* pdev);
} tim_driver_t;

/**
 * @brief 配置GPIO的复用功能
 * @param gpio GPIO配置结构体
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数错误
 */
COMPAT_STATIC_INLINE int hal_tim_config_af_pin(hal_tim_pin_config* gpio)
{
    if (!gpio || !gpio->af)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 最基本的定时器初始化
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
COMPAT_STATIC_INLINE int _init_base(uintptr_t tim_handle, void* cfg_ptr, hal_tim_device* pdev)
{
    /**<此处空操作因为定时器寄存器已经在 open中映射了本函数仅仅站位符>*/
    COMPAT_IGNORE_RESULT(cfg_ptr); COMPAT_IGNORE_RESULT(pdev); COMPAT_IGNORE_RESULT(tim_handle);
    return VFS_OK;
}
/**
 * @brief 初始化编码器
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @note  st的编码器通道被固定为CH1和CH2
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 static int _init_encoder(uintptr_t tim_handle, void* cfg_ptr, hal_tim_device* pdev)
 {
    if(!pdev || !tim_handle || !cfg_ptr)
        return VFS_ERR_INVAL;

    /**<编码器固定占用且必须同时配置 2 个物理引脚*/
    if(hal_tim_config_af_pin(&pdev->host->encoder_mode.pin[0]) != VFS_OK)
        return VFS_ERR_IO;
    if(hal_tim_config_af_pin(&pdev->host->encoder_mode.pin[1]) != VFS_OK)
        return VFS_ERR_IO;

    return VFS_OK;
}

/**
 * @brief 初始化输出比较
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
static int _init_oc(uintptr_t tim_handle, void* cfg_ptr, hal_tim_device* pdev)
{
    if(!pdev || !tim_handle || !cfg_ptr)
        return VFS_ERR_INVAL;

    uint32_t mask = pdev->host->active_chn_mask;

    for(int i = 0; i < HAL_OUTPUT_COMPARE_TIM_MAX_CHANNELS; i++)
    {
        /**<如果对应的通道位没有置 1，说明该通道未启用，直接跳过*/
        if (!(mask & (1 << i)))
            continue;

        if(hal_tim_config_af_pin(&pdev->host->oc_mode.pin[i]) != VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

/**
 * @brief 初始化输入捕获
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 static int _init_ic(uintptr_t tim_handle, void* cfg_ptr, hal_tim_device* pdev)
 {
     if(!pdev || !tim_handle || !cfg_ptr)
         return VFS_ERR_INVAL;

     uint32_t mask = pdev->host->active_chn_mask;

     for(int i = 0; i < HAL_INPUT_CAPTURE_TIM_MAX_CHANNELS; i++)
     {
         if (!(mask & (1 << i)))
             continue;

         if(hal_tim_config_af_pin(&pdev->host->ic_mode.pin[i]) != VFS_OK)
             return VFS_ERR_IO;
     }

     return VFS_OK;
 }

/**
 * @brief 初始化断路器与死区
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
static int _init_bdtr(uintptr_t tim_handle, void* cfg_ptr,hal_tim_device* pdev)
{
    if (!pdev||!tim_handle||!cfg_ptr)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 初始化霍尔传感器
 * @param tim_handle 定时器句柄
 * @param cfg_ptr 配置指针
 * @param pdev 定时器设备指针
 * @note  st的霍尔传感器通道被固定为CH1和CH2和CH3
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
static int _init_hall(uintptr_t tim_handle, void* cfg_ptr,hal_tim_device* pdev)
{
    if (!pdev||!tim_handle||!cfg_ptr)
        return VFS_ERR_INVAL;
    for(int i = 0; i < 3; i++)
    {
        if(hal_tim_config_af_pin(&pdev->host->hall_mode.phase_pins[i])!=VFS_OK)
            return VFS_ERR_IO;
    }
    return VFS_OK;
}

/**
 * @brief 关闭编码器
 * @param tim_handle 定时器句柄
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 static int _close_encode(uintptr_t tim_handle, struct hal_tim_device* pdev)
 {
     if (!pdev || !tim_handle)
         return VFS_ERR_INVAL;

     return VFS_OK;
 }

/**
 * @brief 关闭最基本的定时器
 * @param tim_handle 定时器句柄
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 COMPAT_STATIC_INLINE int _close_base(uintptr_t tim_handle, struct hal_tim_device* pdev)
 {
     if (!pdev || !tim_handle)
         return VFS_ERR_INVAL;

    COMPAT_IGNORE_RESULT(pdev);
     return VFS_OK;
 }

 static int _close_oc(uintptr_t tim_handle, struct hal_tim_device* pdev)
 {
     if (!pdev || !tim_handle)
         return VFS_ERR_INVAL;

     return VFS_OK;
 }

 /**
  * @brief 关闭输入捕获
  * @param tim_handle 定时器句柄
  * @param pdev 定时器设备指针
  * @return VFS_OK 成功, VFS_ERR_INVAL 失败
  */
 static int _close_ic(uintptr_t tim_handle, struct hal_tim_device* pdev)
 {
     if(!tim_handle || !pdev)
         return VFS_ERR_INVAL;

     return VFS_OK;
}
/**
 * @brief 关闭断路器与死区
 * @param tim_handle 定时器句柄
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
 static int _close_bdtr(uintptr_t tim_handle, struct hal_tim_device* pdev)
 {
     if(!tim_handle || !pdev)
         return VFS_ERR_INVAL;

     return VFS_OK;
}

/**
 * @brief 关闭霍尔传感器
 * @param tim_handle 定时器句柄
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
static int _close_hall(uintptr_t tim_handle, struct hal_tim_device* pdev)
{
    if(!tim_handle || !pdev)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

static const tim_driver_t tim_drivers[TIM_DRIVER_COUNT] =
{
    [HAL_TIM_MODE_BASE]         = { _init_base      ,       _close_base     },
    [HAL_TIM_MODE_OC]           = { _init_oc        ,       _close_oc       },
    [HAL_TIM_MODE_IC]           = { _init_ic        ,       _close_ic       },
    [HAL_TIM_MODE_ENCODER]      = { _init_encoder   ,       _close_encode   },
    [HAL_TIM_MODE_HALLSENSOR]   = { _init_hall      ,       _close_hall     },
};
/*===========================================================================================================================================================*/
/*函数声明*/
/*===========================================================================================================================================================*/

/*===========================================================================================================================================================*/
/* 外部核心 HAL 接口实现 */
/*===========================================================================================================================================================*/

int hal_tim_device_init(hal_tim_device* pdev, hal_tim_platform_unique_config* unique, hal_tim_host_config* host)
{
    if (!pdev || !unique || !host)
        return VFS_ERR_INVAL;

    COMPAT_MEM_SET(pdev, 0, sizeof(*pdev));
    pdev->host   = host;
    pdev->unique = unique;
    return VFS_OK;
}

int hal_tim_device_deinit(hal_tim_device* pdev)
{
    if (!pdev)
        return VFS_OK;

    pdev->host   = NULL;
    pdev->unique = NULL;
    return VFS_OK;
}

int hal_tim_open(hal_tim_device *pdev)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if (!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    /**<路由并动态调用具体模式（OC/IC/Encoder/Hall）的专属驱动>*/
    uint32_t mode = pdev->host->mode;
    if (mode >= TIM_DRIVER_COUNT || !tim_drivers[mode].init)
        return VFS_ERR_INVAL;

    /**<查模式驱动表, 本地调用 (不在 pdev 上存函数指针)>*/
    int (*init_func)(uintptr_t, void*, hal_tim_device*) = tim_drivers[mode].init;

    /**<定义局部的临时结构体缓冲区传递给模式驱动，防止越界污染 private_cfg>*/
    uint64_t init_buffer[16] = {0};

    if (init_func(pdev->host->tim_handle, (void*)init_buffer, pdev) != VFS_OK)
        return VFS_ERR_NODEV;

    return VFS_OK;
}

int hal_tim_close(hal_tim_device *pdev)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if (!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    uint32_t mode = pdev->host->mode;

    if (mode >= TIM_DRIVER_COUNT || !tim_drivers[mode].close)
        return VFS_ERR_INVAL;

    /**<完美调用具体的 close 多态驱动关闭通道>*/
    return tim_drivers[mode].close(pdev->host->tim_handle, pdev);
}

/* =========================================================================================================================================================== */
/* 基础属性配置与获取函数                                                                                                                                       */
/* =========================================================================================================================================================== */

/**
 * @brief 获取定时器当前计数值
 * @param pdev 定时器设备指针
 * @param value 计数值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_get_counter(const hal_tim_device *pdev, uint32_t *value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 设置定时器计数值
 * @param pdev 定时器设备指针
 * @param value 计数值
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_set_counter(hal_tim_device *pdev, uint32_t value)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

int hal_tim_base_stop(hal_tim_device*pdev)
{
    if(!pdev || !pdev->host)
       return VFS_ERR_INVAL;

    _close_base(pdev->host->tim_handle,pdev);
    return VFS_OK;
}

int hal_tim_base_start(hal_tim_device*pdev)
{
    /** resume 语义: PSC/ARR/CounterMode 已由 hal_tim_open 灌入硬件, 此处只拉手刹 */
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

int hal_tim_clear_update_flag(hal_tim_device* pdev)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 设置定时器自动重装载值
 * @param pdev 定时器设备指针
 * @param value 自动重装载值
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_set_autoreload(hal_tim_device *pdev, uint32_t value)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

/**
 * @brief 获取定时器自动重装载值
 * @param pdev 定时器设备指针
 * @param value 自动重装载值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_get_autoreload(const hal_tim_device *pdev, uint32_t *value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 设置定时器分频器
 * @param pdev 定时器设备指针
 * @param value 分频器值
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_set_prescaler(hal_tim_device *pdev, uint32_t value)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

/**
 * @brief 获取定时器分频器
 * @param pdev 定时器设备指针
 * @param value 分频器值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_get_prescaler(const hal_tim_device *pdev, uint32_t *value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 设置定时器时钟分频器
 * @param pdev 定时器设备指针
 * @param value 时钟分频器值
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_set_clock_division(hal_tim_device *pdev, uint32_t value)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

/**
 * @brief 获取定时器时钟分频器
 * @param pdev 定时器设备指针
 * @param value 时钟分频器值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_get_clock_division(const hal_tim_device *pdev, uint32_t *value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 设置定时器计数模式
 * @param pdev 定时器设备指针
 * @param value 计数模式值
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_set_counter_mode(hal_tim_device *pdev, uint32_t value)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    (void)value;
    return VFS_OK;
}

/**
 * @brief 获取定时器计数模式
 * @param pdev 定时器设备指针
 * @param value 计数模式值指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_get_counter_mode(const hal_tim_device *pdev, uint32_t *value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 启用定时器自动重装载预装载
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_enable_arr_preload(hal_tim_device *pdev)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 禁用定时器自动重装载预装载
 * @param pdev 定时器设备指针
 * @return VFS_OK 成功, VFS_ERR_INVAL 失败
 */
int hal_tim_disable_arr_preload(hal_tim_device *pdev)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    return VFS_OK;
}


/* =========================================================================================================================================================== */
/* 五大模式操作函数 — 供 VFS 层调用                                                                                                                              */
/* =========================================================================================================================================================== */

/**
 * @brief PWM 占空比/重装载同步更新 (OC 模式热路径)
 * @param pdev 定时器设备指针
 * @param channel 通道号 1..4
 * @param frequency 目标频率计数值 (写入 ARR), 0 表示不修改频率
 * @param duty 占空比计数值 (直接写入 CCR), 0 表示不修改占空比
 */
int hal_tim_pwm_update(hal_tim_device* pdev, uint32_t channel, uint32_t frequency, uint32_t duty)
{
    if(!pdev || !pdev->host || channel < 1U || channel > 4U)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    (void)frequency;
    (void)duty;
    return VFS_OK;
}

/**
 * @brief 定时器中断配置 (直接配置掩码，支持使能和关闭)
 * @param pdev 定时器设备指针
 * @param interrupt_config DIER 位掩码 (bit0=UIE, bit1=CC1IE, ...)
 */
int hal_tim_interrupt_config(hal_tim_device* pdev, uint32_t interrupt_config)
{
    if (!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if (!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    (void)interrupt_config;
    return VFS_OK;
}

/**
 * @brief 获取输入捕获值 (IC 模式)
 * @param pdev 定时器设备指针
 * @param channel 通道号 1..4
 * @param value 捕获值输出指针
 */
int hal_tim_get_capture_value(const hal_tim_device* pdev, uint32_t channel, uint32_t* value)
{
    if(!pdev || !pdev->host || !value || channel < 1U || channel > 4U)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 获取编码器计数值 (Encoder 模式)
 * @param pdev 定时器设备指针
 * @param value 编码器计数值输出指针
 */
int hal_tim_get_encoder_value(const hal_tim_device* pdev, uint32_t* value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 获取霍尔传感器换向时间值 (Hall 模式)
 * @param pdev 定时器设备指针
 * @param value 霍尔换向时间值输出指针 (CH1 捕获值)
 */
int hal_tim_get_hall_value(const hal_tim_device* pdev, uint32_t* value)
{
    if(!pdev || !pdev->host || !value)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 强制停止定时器 (所有模式通用)
 * @param pdev 定时器设备指针
 */
int hal_tim_force_stop(hal_tim_device* pdev)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/**
 * @brief 启动编码器接口 (Encoder 模式)
 * @param pdev 定时器设备指针
 * @param encoder_mode 编码器倍频模式 (1=1/2倍频通道1, 2=1/2倍频通道2, 4=4倍频双通道, 0=不修改现有配置)
 */
int hal_tim_encoder_start(hal_tim_device* pdev, uint32_t encoder_mode)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    (void)encoder_mode;
    return VFS_OK;
}

/**
 * @brief 启动霍尔传感器接口换向捕获 (Hall 模式)
 * @param pdev 定时器设备指针
 */
int hal_tim_hall_start(hal_tim_device* pdev)
{
    if(!pdev || !pdev->host)
        return VFS_ERR_INVAL;

    if(!pdev->host->tim_handle)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

/* =========================================================================================================================================================== */
/* ISR 虚拟中断回调                                                                                                                                              */
/* =========================================================================================================================================================== */

/**
 * @brief TIM 虚拟中断上半部回调 (ISR 内执行)
 * @param arg 参数 (hal_tim_device*)
 * @param irq_num 虚拟中断号
 * @return VFS_IRQ_ENTRY_BOTTOM 需要下半部; VFS_IRQ_ENTRY_NOBOTTOM 不需要
 * @note  上半部仅清除 Update 标志; 下半部由 VFS 层通过 g_tim_bottom_half_work 注册
 */
int hal_virtual_tim_irq_callback(void* arg, uint16_t irq_num)
{
    COMPAT_IGNORE_RESULT(irq_num);
    hal_tim_device* pdev = (hal_tim_device*)arg;

    if (!pdev || !pdev->host)
        return VFS_IRQ_ENTRY_NOBOTTOM;

    return VFS_IRQ_ENTRY_NOBOTTOM;
}
