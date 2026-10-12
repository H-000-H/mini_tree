/**
 * @file system_scrubber_config.h
 * @author H-000-H
 * @brief system scrubber config 头文件
 * @note system_scrubber_config — Flash bit-rot 巡检策略 (非 DTS 派生)
 * @note 校验原语与 CRC 模型统一由 mini-ota 提供 (image_verify_area → crc_stream),
 * @note 本模块不再自带 CRC 表。
 * @copyright SPDX-License-Identifier: Apache-2.0
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
