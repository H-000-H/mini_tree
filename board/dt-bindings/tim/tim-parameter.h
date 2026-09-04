/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file tim-parameter.h
 *@brief tim-parameter 头文件
 *@author H-000-H
 *@details
 *   TIM 模式常量 (dt-bindings, 仅供 dtsi / hal_tim.h / vfs-tim.c 共享)
 *   只放 #define 常量, 不写设备节点.
 *   HAL_TIM_MODE_* 是 DTS → VFS → HAL 三层共享的定时器模式索引,
 *   放在 dt-bindings 里作为唯一真相源, 避免 dtsi 反向 #include hal_tim.h.
 *   注意: 仅供 dtc-lite 预处理与 C 编译, 勿加 #ifndef guard 之外的逻辑.
 */

#ifndef __TIM_PARAMETER_H__
#define __TIM_PARAMETER_H__

#ifndef HAL_TIM_MODE_INDEX
#define HAL_TIM_MODE_INDEX 0
#endif
#ifndef HAL_TIM_MODE_BASE
#define HAL_TIM_MODE_BASE (HAL_TIM_MODE_INDEX + 1) /**< 基础模式 */
#endif
#ifndef HAL_TIM_MODE_OC
#define HAL_TIM_MODE_OC (HAL_TIM_MODE_INDEX + 2) /**< 输出比较模式 */
#endif
#ifndef HAL_TIM_MODE_IC
#define HAL_TIM_MODE_IC (HAL_TIM_MODE_INDEX + 3) /**< 输入捕获模式 */
#endif
#ifndef HAL_TIM_MODE_ENCODER
#define HAL_TIM_MODE_ENCODER (HAL_TIM_MODE_INDEX + 4) /**< 编码器模式 */
#endif
#ifndef HAL_TIM_MODE_HALLSENSOR
#define HAL_TIM_MODE_HALLSENSOR (HAL_TIM_MODE_INDEX + 5) /**< 霍尔传感器模式 */
#endif
#ifndef TIM_DRIVER_COUNT
#define TIM_DRIVER_COUNT (HAL_TIM_MODE_INDEX + 6) /**< 驱动数量 */
#endif

#endif /* __TIM_PARAMETER_H__ */
