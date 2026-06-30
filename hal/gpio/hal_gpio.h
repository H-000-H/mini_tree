/* SPDX-License-Identifier: Apache-2.0 */
/*
 * GPIO HAL 层 — 硬件抽象接口 (跨平台统一头)
 *
 * 设计: 结构体封装, DTSI 直投厂商宏值, HAL 零翻译零查表。
 * - STM32/WCH: gpio-port = <GPIOA_BASE>, gpio-pin = <GPIO_PIN_5>,
 *   gpio-clk  = <LL_AHB1_GRP1_PERIPH_GPIOA> / <RCC_APB2Periph_GPIOA>,
 *   gpio-mode = <LL_GPIO_MODE_OUTPUT> / <GPIO_Mode_Out_PP>,
 *   gpio-pull = <LL_GPIO_PULL_NO>  (WCH 忽略 pull, mode+pull 编码在一起)
 * - ESP32: gpio-port = <0>, gpio-pin = <5>  (SoC GPIO 编号),
 *   gpio-clk  = <0>,  gpio-mode = <GPIO_MODE_OUTPUT>,
 *   gpio-pull = <GPIO_FLOATING>
 * - hal_gpio_obj_t 嵌入 VFS priv, VFS probe 填值, HAL 无池管理
 * - fast-path 实现在各平台 hal_gpio_*.c, 直接刷寄存器/调 ESP-IDF API
 *
 * 头中立化: 本头不暴露任何 vendor 类型, 只用 uintptr_t/int/void*。
 * vendor 头由 hal_gpio_*.c 内部 include, fast-path 实现也在 .c 中。
 */
#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler_compat.h"
#include "VFS.h"

#ifdef __cplusplus
extern "C" {
#endif

                                                            /*GPIO 电平与模式配置*/
/*===========================================================================================================================================================*/
#define HAL_GPIO_HIGH_LEVEL 1
#define HAL_GPIO_LOW_LEVEL  0

#ifndef HAL_GPIO_OBJ_POOL_SIZE
#define HAL_GPIO_OBJ_POOL_SIZE 16
#endif

/* 模式配置: 直接承载厂商宏值 (LL_GPIO_MODE_* / GPIOMode_TypeDef / gpio_mode_t),
 * 拒绝二次翻译。WCH 平台 pull 字段被忽略 (mode+pull 编码在一起)。 */
struct hal_gpio_mode_cfg
{
    uint32_t mode;
    uint32_t pull;
};
/*===========================================================================================================================================================*/

                                                            /*GPIO 核心对象 (嵌入 VFS, HAL 无池管理)*/
/*===========================================================================================================================================================*/
/* 纯硬件直投实体, 所有字段由 DTSI 提供厂商宏值, HAL 零计算。
 * - 嵌入 VFS priv 结构体, 由 VFS osal_pool 管理生命周期
 * - HAL 层无池管理, 无 alloc/free, 无 pre_execution
 * - fast-path 实现在 hal_gpio_*.c, 直接解引用对象指针刷寄存器/调 API
 *
 * 跨平台字段说明:
 * - STM32/WCH: port = GPIO_TypeDef* 基地址, pin = GPIO_PIN_x, clk_periph = RCC 时钟
 * - ESP32: port = 0 (无基地址概念), pin = SoC GPIO 编号, clk_periph = 0 (内部处理)
 */
typedef struct hal_gpio_obj
{
    uintptr_t     port;
    uint16_t      pin;
    uint32_t      clk_periph;
    bool          is_used;    /* 运行时激活状态 (VFS probe 置 true) */
} hal_gpio_obj_t;
/*===========================================================================================================================================================*/

                                                            /*fast path (实现在 hal_gpio_*.c, 零分支零查表)*/
/*===========================================================================================================================================================*/
/**
 * @brief 快路径: 设置 GPIO 输出电平
 * @param obj   GPIO 对象指针
 * @param level 目标电平 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_set_level(hal_gpio_obj_t* obj, int level) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 快路径: 读取 GPIO 当前输入电平
 * @param obj       GPIO 对象指针
 * @param level_out 用于回传电平的指针 (1=高, 0=低)
 * @return 成功返回 VFS_OK, obj 或 level_out 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_get_level(hal_gpio_obj_t* obj, int *level_out) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 快路径: 翻转 GPIO 输出电平
 * @param obj GPIO 对象指针
 * @return 成功返回 VFS_OK, obj 为空返回 VFS_ERR_INVAL
 */
int hal_gpio_fast_toggle(hal_gpio_obj_t* obj) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*HAL API (基于对象指针)*/
/*===========================================================================================================================================================*/
int hal_gpio_init(hal_gpio_obj_t* obj, const struct hal_gpio_mode_cfg *cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_gpio_deinit(hal_gpio_obj_t* obj) COMPAT_WARN_UNUSED_RESULT;

/* Raw 原始接口: 直接数字强转物理硬刷, 绝不占用池子内存。
 * STM32/WCH: dts_port_base = GPIO 基地址, dts_pin_mask = GPIO_PIN_x
 * ESP32: dts_port_base = SoC GPIO 编号, dts_pin_mask 忽略 */
int hal_gpio_write_raw_dts(uint32_t dts_port_base, uint32_t dts_pin_mask, uint8_t level);
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_GPIO_H */
 