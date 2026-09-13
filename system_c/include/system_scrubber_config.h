/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_scrubber_config.h
 *@brief system scrubber config 头文件
 *@author H-000-H
 *@details
 *   system_scrubber_config — Flash bit-rot 巡检策略 (非 DTS 派生)
 *   校验原语与 CRC 模型统一由 mini-ota 提供 (image_verify_area → crc_stream),
 *   本模块不再自带 CRC 表。
 *   基线由构建期 system_scrubber_crc_gen.h 提供 (post_build_crc.py 生成)。
 *   ⚠ 基线的 CRC 与长度必须同源: 都由同一次构建的同一个固件二进制算出。
 *     若长度口径与扫描范围不一致, 基线永远对不上 (旧实现即用"整个分区"去比
 *     "二进制长度"的 CRC, 属口径错配)。
 */

#ifndef SYSTEM_SCRUBBER_CONFIG_H
#define SYSTEM_SCRUBBER_CONFIG_H

/** 两次完整巡检之间的间隔 (毫秒); 单次巡检的分块大小由 mini-ota 的 IMAGE_VERIFY_CHUNK 决定 */
#define SYSTEM_SCRUBBER_INTERVAL_MS 200

#if defined __has_include
#if __has_include("system_scrubber_crc_gen.h")
#include "system_scrubber_crc_gen.h"
#endif
#else
#include "system_scrubber_crc_gen.h"
#endif

/* 构建期由 post_build_crc.py 写入; IDE / 未跑该脚本时退化为"未配置" → 巡检自动不启动 */
#ifndef SYSTEM_SCRUBBER_CRC_BASELINE
#define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000U
#endif
#ifndef SYSTEM_SCRUBBER_IMAGE_LEN
#define SYSTEM_SCRUBBER_IMAGE_LEN 0U
#endif

#endif /* SYSTEM_SCRUBBER_CONFIG_H */
