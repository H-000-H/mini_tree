/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file printf_output.h
 *@brief printf output 头文件
 *@author H-000-H
 *@details
 *   printf_output — printf 风格格式化输出适配接口
 *   仅暴露 my_printf_output() 一个函数, 作为日志/调试 sink 回调统一入口
 *   内部转调 vprintf, 便于 poison printf 后仍保留受控的格式化输出路径
 */

#ifndef PRINTF_OUTPUT_H
#define PRINTF_OUTPUT_H

#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 受控的 printf 风格格式化输出 (日志/调试 sink 统一入口)
     * @param[in] fmt printf 格式串
     * @param[in] ... 格式化参数
     * @note 内部转调 vprintf; 便于 poison printf 后仍保留受控输出路径
     */
    void my_printf_output(const char* fmt, ...) MINI_FMT_PRINTF(1, 2);

#ifdef __cplusplus
}
#endif

#endif /* PRINTF_OUTPUT_H */
