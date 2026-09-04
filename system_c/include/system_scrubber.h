/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_scrubber.h
 *@brief system scrubber 头文件
 *@author H-000-H
 *@details
 *   system_scrubber (C 接口) — Flash bit-rot CRC 巡检
 *   通过 system_scrubber.hpp 声明的 extern "C" API 供 .c 调用;
 *   定期扫描 Flash 分区与构建期 CRC 基线比对, 检测位翻转。
 */

#pragma once

#include "system_scrubber.hpp"
