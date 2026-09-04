/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_gpio.h
 *@brief GPIO HAL 层 — 硬件抽象接口, 硬件直投层
 *@author H-000-H
 *@details
 *   @note        所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 *   @note        由于 GPIO 是快速热路径外设所以 GPIO 的初始化与配置应该尽量在硬件直投层完成
 *   @note        文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码
 *   @note        获取参数不能直接返回，必须通过指针参数传递
 *   @note
 *禁止使用enum，enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */

#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*GPIO 电平与模式配置*/
/* -------------------------------------------------------------------------- */
#define HAL_GPIO_HIGH_LEVEL 1
#define HAL_GPIO_LOW_LEVEL 0

#ifndef HAL_GPIO_DEV_POOL_SIZE
#define HAL_GPIO_DEV_POOL_SIZE 32
#endif

/**
 * @brief GPIO 配置
 * @note 用于配置GPIO的电气特性
 */
struct hal_gpio_cfg
{
    uint32_t mode;        /**< 模式 */
    uint32_t pull;        /**< 上拉/下拉 */
    uint32_t speed;       /**< 速度 */
    uint32_t output_type; /**< 输出类型 */
    uint32_t af;          /**< 复用功能 */
    uint32_t intr;        /**< 中断类型 */
    uint32_t deinit_mode; /**< deinit 时恢复的引脚模式 (0=用安全复位值 LL_GPIO_MODE_ANALOG) */
    uint32_t deinit_pull; /**< deinit 时恢复的上下拉  (0=用安全复位值 LL_GPIO_PULL_NO) */
};
/* -------------------------------------------------------------------------- */

/**
 * @brief typedef 设备配置
 * @note  用于上层调用，避免重复定义(仅热路径可以使用该类型冷路径必须struct hal_x_cfg)
 */
typedef struct hal_gpio_cfg hal_gpio_config;

typedef struct
{
    uintptr_t       port;     /**< 端口基地址 */
    uint32_t        pin;      /**< 引脚 (LL_GPIO_PIN_x 复合值，如 STM32F1 为 32 位复合值放不进 uint16_t) */
    uint32_t        clk_bus;  /**< 时钟总线/RCC位 */
    uint8_t         virq_idx; /**< VIRQ(gpio, idx) 槽位 (DTS virq-idx, < BLOCK_SIZE) */
    hal_gpio_config cfg;      /**< 配置 */
    bool            is_used;  /**< 运行时激活状态 (VFS probe 置 true) */
} hal_gpio_dev_t;
/* -------------------------------------------------------------------------- */

/*fast path (实现在 hal_gpio_*.c, 零分支零查表)*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 快路径: 设置 GPIO 输出电平
 * @param[in] pdev   GPIO 对象指针
 * @param[in] level 目标电平 (1=高, 0=低)
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_fast_set_level(hal_gpio_dev_t* pdev, int level);

/**
 * @brief 快路径: 读取 GPIO 当前输入/输出引脚的实际电平状态
 * @param[in] pdev       GPIO 对象指针
 * @param[out] level_out 用于回传电平的指针 (1=高, 0=低)
 * @return 成功返回 MINI_OK, pdev 或 level_out 为空返回 MINI_ERR_INVAL
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_fast_get_level(hal_gpio_dev_t* pdev, int* level_out);

/**
 * @brief 快路径: 翻转 GPIO 输出电平
 * @param[in] pdev GPIO 对象指针
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_fast_toggle(hal_gpio_dev_t* pdev);
/* -------------------------------------------------------------------------- */

/*HAL API (基于对象指针)*/
/* -------------------------------------------------------------------------- */
/**
 * @brief GPIO 初始化
 * @param[in] pdev GPIO 对象指针
 * @return 成功返回 MINI_OK, pdev 或内部配置为空返回 MINI_ERR_INVAL
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_init(hal_gpio_dev_t* pdev);

/**
 * @brief GPIO 释放
 * @param[in] pdev GPIO 对象指针
 * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_deinit(hal_gpio_dev_t* pdev);

/**
 * @brief GPIO 设置模式
 * @param[in] pdev GPIO 对象指针
 * @param[in] mode 模式宏值 (如 LL_GPIO_MODE_OUTPUT)
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_mode(hal_gpio_dev_t* pdev, uint32_t mode);

/**
 * @brief GPIO 获取当前模式
 * @param[in] pdev GPIO 对象指针
 * @param[in] mode 用于回传当前模式宏值的指针
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_get_mode(hal_gpio_dev_t* pdev, uint32_t* mode);

/**
 * @brief GPIO 设置上拉/下拉
 * @param[in] pdev GPIO 对象指针
 * @param[in] pull 上拉/下拉宏值 (如 LL_GPIO_PULL_UP)
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_pull(hal_gpio_dev_t* pdev, uint32_t pull);

/**
 * @brief GPIO 获取当前上拉/下拉配置
 * @param[in] pdev GPIO 对象指针
 * @param[in] pull 用于回传上拉/下拉宏值的指针
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_get_pull(hal_gpio_dev_t* pdev, uint32_t* pull);

/**
 * @brief GPIO 设置速度
 * @param[in] pdev GPIO 对象指针
 * @param[in] speed 速度宏值 (如 LL_GPIO_SPEED_FREQ_HIGH)
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_speed(hal_gpio_dev_t* pdev, uint32_t speed);

/**
 * @brief GPIO 获取当前速度配置
 * @param[in] pdev GPIO 对象指针
 * @param[in] speed 用于回传速度宏值的指针
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_get_speed(hal_gpio_dev_t* pdev, uint32_t* speed);

/**
 * @brief GPIO 设置输出类型
 * @param[in] pdev GPIO 对象指针
 * @param[out] output_type 输出类型宏值 (如 LL_GPIO_OUTPUT_PUSHPULL)
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_output_type(hal_gpio_dev_t* pdev, uint32_t output_type);

/**
 * @brief GPIO 获取当前输出类型配置
 * @param[in] pdev GPIO 对象指针
 * @param[out] output_type 用于回传输出类型宏值的指针
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_get_output_type(hal_gpio_dev_t* pdev, uint32_t* output_type);

/**
 * @brief GPIO 设置复用功能寄存器值(AFR)
 * @param[in] pdev GPIO 对象指针
 * @param[in] af 复用功能宏值 (如 LL_GPIO_AF_1)
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_af(hal_gpio_dev_t* pdev, uint32_t af);

/**
 * @brief GPIO 获取当前引脚的复用功能寄存器值(AFR)
 * @param[in] pdev GPIO 对象指针
 * @param[in] af 用于回传复用功能宏值的指针
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_get_af(hal_gpio_dev_t* pdev, uint32_t* af);

/**
 * @brief GPIO 设置复用功能并自动将引脚切换为复用模式
 * @param[in] pdev GPIO 对象指针
 * @param[in] af 复用功能宏值
 * @return 成功返回 MINI_OK
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_set_af_mode(hal_gpio_dev_t* pdev, uint32_t af);

/**
 * @brief 使能硬件 GPIO 中断 → 仅 interrupt_virtual_dispatch(VIRQ(gpio, virq_idx))
 * @note  产品驱动用 interrupt_virtual_register 挂上下半部；禁止直挂业务 ISR
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_irq_enable(hal_gpio_dev_t* pdev);

/**
 * @brief 关闭该脚硬件 GPIO 中断路由
 */
int MINI_WARN_UNUSED_RESULT hal_gpio_irq_disable(hal_gpio_dev_t* pdev);
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif
#endif /* HAL_GPIO_H */