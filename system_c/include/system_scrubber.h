/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_scrubber (C 接口) — Flash bit-rot CRC 巡检
 *
 * 包装 C++ system_scrubber.hpp, 供 .c 文件调用。
 * 定期扫描 Flash 分区与构建期 CRC 基线比对, 检测位翻转。
 */
#pragma once

#include "system_scrubber.hpp"
