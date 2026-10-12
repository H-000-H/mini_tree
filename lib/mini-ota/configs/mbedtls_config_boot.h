/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @file: mbedtls_config_ota.h
 * @brief: mini-boot OTA mbedtls 配置，仅启用 OTA 所需算法：
 *         AES / GCM（依赖 cipher 层）与 SHA-256（依赖 md 层）。
 *         通过编译宏 MBEDTLS_CONFIG_FILE="mbedtls_config_ota.h" 启用，
 *         替代默认的 mbedtls_config.h；
 */
#ifndef MBEDTLS_CONFIG_H
#define MBEDTLS_CONFIG_H

/*
 * 平台：裸机 Cortex-M4，不启用 MBEDTLS_PLATFORM_C，
 * 直接使用标准 C 库的 malloc/printf/memcpy 等，无 FS/网络/线程。
 */
#if __has_include(<mini-os\inc\memory.h>)
#define MBEDTLS_PLATFORM_MEMORY_C
#define MBEDTLS_PLATFORM_TIME_ALT

#endif
#define MBEDTLS_PLATFORM_C
/* 对称加密 */
#define MBEDTLS_CIPHER_C
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_AES_C
#define MBEDTLS_GCM_C

/* 摘要 */
#define MBEDTLS_MD_C
#define MBEDTLS_SHA256_C

#include "mbedtls/check_config.h"

#endif /* MBEDTLS_CONFIG_H */
