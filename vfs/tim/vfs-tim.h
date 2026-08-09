/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * TIM VFS — TIM 子系统 VFS 层
 *
 * 架构位置: [VFS Layer (本文件)] → HAL Layer
 * 职责: file_operations 挂载 + dev_lifecycle (互斥/引用计数) + DTS 解析; I/O 全走 HAL 层。
 * 隔离: 本文件定义 TIM_VFS_IMPL 可调 tim_hal API; 其他文件包含本头时 tim_hal 符号被 #pragma GCC
 * poison。
 *
 * Driver 注册:
 *   - tim_vfs: "tim"
 * TIM和I2C SPI
 * 这些需要总线的设备不同,不需要挂载到总线,直接挂载到VFS层,通过文件操作接口进行操作。和gpio类似,TIM和GPIO都是通过文件操作接口进行操作。
 * 并且TIM不需要上层抽象,所以.c中几乎全为static函数,probe和remove函数也是static函数。但是和gpio一样的是我会提供2个路径：一个是有lifeycle的,
 * 一个是没有lifeycle的.h内联版本。但是open和close必须走life版本因为这是和速度无关的。只是初始化不存在所谓冷热问题。基础定时器也是默认走life版本。
 * 只有pwm这一类和速度相关的需要走inline版本。inline是残缺的函数,我只会实现部分功能。而life是完整的函数我会全部实现,有全部完整的功能。
 * @see hal/tim/hal_tim.h  HAL 层接口
 *@=========================================================================================================================*/
#ifndef VFS_TIM_H
#define VFS_TIM_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "compiler_compat.h"
#include "hal_tim.h"
#include <stdint.h>

#define TIM_CMD_BASE COMPAT_MAGIC(TIM)
#define TIM_CMD_START (TIM_CMD_BASE + 0x01) /* 0: 启动定时器 (base/encoder/hall) */
#define TIM_CMD_STOP (TIM_CMD_BASE + 0x02) /* 1: 强制停止 */
#define TIM_CMD_PAUSE (TIM_CMD_BASE + 0x03) /* 2: 基础停止 (base_stop) */
#define TIM_CMD_RESUME (TIM_CMD_BASE + 0x04) /* 3: 基础启动 (base_start) */
#define TIM_CMD_GET_COUNTER (TIM_CMD_BASE + 0x05) /* 4: 读计数器 */
#define TIM_CMD_SET_COUNTER (TIM_CMD_BASE + 0x06) /* 5: 写计数器 */
#define TIM_CMD_PWM_UPDATE (TIM_CMD_BASE + 0x07) /* 6: PWM 更新 (ch+arr+ccr) */
#define TIM_CMD_GET_CAPTURE (TIM_CMD_BASE + 0x08) /* 7: 读输入捕获值 (ch) */
#define TIM_CMD_GET_ENCODER (TIM_CMD_BASE + 0x09) /* 8: 读编码器值 */
#define TIM_CMD_GET_HALL (TIM_CMD_BASE + 0x0A) /* 9: 读霍尔值 */
#define TIM_CMD_SET_AUTORELOAD (TIM_CMD_BASE + 0x0B) /* 10: 设置 ARR */
#define TIM_CMD_GET_AUTORELOAD (TIM_CMD_BASE + 0x0C) /* 11: 读取 ARR */
#define TIM_CMD_SET_PRESCALER (TIM_CMD_BASE + 0x0D) /* 12: 设置 PSC */
#define TIM_CMD_GET_PRESCALER (TIM_CMD_BASE + 0x0E) /* 13: 读取 PSC */
#define TIM_CMD_SET_CLOCK_DIVISION (TIM_CMD_BASE + 0x0F) /* 14: 设置时钟分频 */
#define TIM_CMD_GET_CLOCK_DIVISION (TIM_CMD_BASE + 0x10) /* 15: 读取时钟分频 */
#define TIM_CMD_SET_COUNTER_MODE (TIM_CMD_BASE + 0x11) /* 16: 设置计数模式 */
#define TIM_CMD_GET_COUNTER_MODE (TIM_CMD_BASE + 0x12) /* 17: 读取计数模式 */
#define TIM_CMD_ENABLE_ARR_PRELOAD (TIM_CMD_BASE + 0x13) /* 18: 使能 ARR 预装载 */
#define TIM_CMD_DISABLE_ARR_PRELOAD (TIM_CMD_BASE + 0x14) /* 19: 禁能 ARR 预装载 */
#define TIM_CMD_SET_INTERRUPT (TIM_CMD_BASE + 0x15) /* 20: 配置中断 (DIER) */
#define TIM_CMD_ENCODER_START (TIM_CMD_BASE + 0x16) /* 21: 启动编码器 */
#define TIM_CMD_HALL_START (TIM_CMD_BASE + 0x17) /* 22: 启动霍尔 */
#define TIM_CMD_CLEAR_UPDATE_FLAG (TIM_CMD_BASE + 0x18) /* 23: 清更新标志 (ISR 用) */
#define TIM_CMD_COUNT 24
    struct tim_start_arg
    {
        uint32_t frequency; /**< 目标频率 (Hz) */
        uint32_t prescaler; /**< 预分频值 */
        uint32_t period; /**< 周期 (ARR) */
    };

    /*===========================================================================================================================================================*/
    /* 快路径内联参数包 — 调用方填充后传给 vfs_tim_fast_* */
    /*===========================================================================================================================================================*/

    /**
     * @brief TIM 快路径操作参数包
     * @note obj 由 vfs_tim_open 成功后通过 ioctl 或 priv_data 获取
     */
    struct vfs_tim_arg
    {
        hal_tim_device* obj; /**< 指向 VFS priv 嵌入的 HAL 对象 */
        uint32_t channel; /**< 通道号 1..4 */
        uint32_t arr; /**< ARR 值 (PWM 频率 / 自动重装载) */
        uint32_t ccr; /**< CCR 值 (PWM 占空比 / 比较值) */
        uint32_t value; /**< 读出的值 (counter / capture / compare) */
    };

    /*===========================================================================================================================================================*/
    /* 快路径内联 — 调用 hal_tim.c 普通函数, 不走 lifecycle 保护 */
    /*===========================================================================================================================================================*/

    /**
     * @brief 快速 PWM 更新 (ARR + CCR 同步)
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_pwm_update(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_pwm_update(arg->obj, arg->channel, arg->arr, arg->ccr);
    }

    /**
     * @brief 快速获取计数器值
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_counter(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_counter(arg->obj, &arg->value);
    }

    /**
     * @brief 快速设置计数器值
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_set_counter(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_set_counter(arg->obj, arg->value);
    }

    /**
     * @brief 快速获取输入捕获值
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_capture(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_capture_value(arg->obj, arg->channel, &arg->value);
    }

    /**
     * @brief 快速获取编码器值
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_encoder(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_encoder_value(arg->obj, &arg->value);
    }

    /**
     * @brief 快速获取霍尔值
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_hall(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_hall_value(arg->obj, &arg->value);
    }

    /**
     * @brief 快速设置 ARR
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_set_autoreload(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_set_autoreload(arg->obj, arg->arr);
    }

    /**
     * @brief 快速获取 ARR
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_autoreload(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_autoreload(arg->obj, &arg->value);
    }

    /**
     * @brief 快速清更新标志 (ISR 上半部用, 非阻塞无生命周期)
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_clear_update_flag(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_clear_update_flag(arg->obj);
    }

    /**
     * @brief 快速设置分频器
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_set_prescaler(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_set_prescaler(arg->obj, arg->value);
    }

    /**
     * @brief 快速获取分频器
     */
    COMPAT_STATIC_INLINE int vfs_tim_fast_get_prescaler(struct vfs_tim_arg* arg)
    {
        if (!arg || !arg->obj)
            return VFS_ERR_INVAL;
        return hal_tim_get_prescaler(arg->obj, &arg->value);
    }

    /*===========================================================================================================================================================*/
    /* HAL 设备获取 — 供调度器等外部模块从 device 拿 hal_tim_device (用于 ISR top_half 清 flag) */
    /*===========================================================================================================================================================*/
    struct device;
    hal_tim_device* vfs_tim_get_hal_dev(struct device* pdev);

#ifdef __cplusplus
}
#endif

/*@=========================================================================================================================*
 * 分层隔离安全锁:
 * - vfs-tim.c 定义 TIM_VFS_IMPL, 可自由调用所有 hal_tim_* API
 * - 其他文件: hal_tim_* 热路径函数 (pwm/counter/capture/encoder/hall/arr/prescaler)
 * 允许内联快路径直透
 * - 其他文件: hal_tim_* 生命周期/配置函数 (init/open/close/stop/start/interrupt) 编译报错阻止
 *@=========================================================================================================================*/
#ifndef TIM_VFS_IMPL
#pragma GCC poison hal_tim_device_init hal_tim_device_deinit
#pragma GCC poison hal_tim_open hal_tim_close
#pragma GCC poison hal_tim_force_stop hal_tim_encoder_start hal_tim_hall_start
#pragma GCC poison hal_tim_interrupt_config
#endif

#endif /* VFS_TIM_H */
