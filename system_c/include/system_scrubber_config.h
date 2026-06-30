/* SPDX-License-Identifier: Apache-2.0 */
/*
 * system_scrubber_config — Flash bit-rot 巡检策略 (非 DTS 派生)
 *
 * CRC 基线由构建期 system_scrubber_crc_gen.h 提供, 运行时按 chunk 比对。
 */
#ifndef SYSTEM_SCRUBBER_CONFIG_H
#define SYSTEM_SCRUBBER_CONFIG_H

#define SYSTEM_SCRUBBER_CHUNK_BYTES    32
#define SYSTEM_SCRUBBER_INTERVAL_MS    200

#if defined __has_include
#  if __has_include("system_scrubber_crc_gen.h")
#    include "system_scrubber_crc_gen.h"
#  else
#    define SYSTEM_SCRUBBER_CRC_BASELINE 0x00000000U
#  endif
#else
#  include "system_scrubber_crc_gen.h"
#endif

#endif /* SYSTEM_SCRUBBER_CONFIG_H */
