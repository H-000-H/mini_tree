/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_scrubber.h
 *@brief system scrubber 头文件
 *@author H-000-H
 *@details
 *   system_scrubber (C 接口) — Flash bit-rot CRC 巡检
 *   定期扫描 Flash 分区与构建期 CRC 基线比对, 检测位翻转;
 *   实现见 system_c/src/system_scrubber.c。
 */

#pragma once

#include "status.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    mt_err_t system_scrubber_start(void);
    bool system_scrubber_is_running(void);

#ifdef __cplusplus
}
#endif
