/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 * @file boot_keys.c
 * @brief OTA 密钥槽实现：单把密钥的定长静态缓冲 + set/get/wipe。
 *        清零走 mbedtls_platform_zeroize（volatile 语义）
 * @note  仅在 IMAGE_CRYPTO_ENABLE=1 时编入：关闭加密时本文件为空编译单元，
 * @author H-000-H
 */
#include "boot_keys.h"
#include "boot_config.h"
#include "err.h"

#if IMAGE_CRYPTO_ENABLE

#include "mbedtls/platform_util.h"

/** AES 密钥长度上限（AES-256），缓冲按上限静态分配 */
#define BOOT_KEY_MAX_LEN 32u

static uint8_t s_key[BOOT_KEY_MAX_LEN]; /* 密钥槽本体，符号不导出 */
static size_t s_key_len = 0U;           /* 0 表示未装入 */

int boot_key_set(const uint8_t *key, size_t len)
{
    if ((key == NULL) || ((len != 16U) && (len != 24U) && (len != 32U)))
    {
        return ERR_ARG;
    }
    mbedtls_platform_zeroize(s_key, sizeof(s_key)); /* 覆盖旧密钥残留 */
    for (size_t i = 0U; i < len; i++)
    {
        s_key[i] = key[i];
    }
    s_key_len = len;
    return ERR_OK;
}

const uint8_t *boot_key_get(size_t *out_len)
{
    if (s_key_len == 0U)
    {
        return NULL;
    }
    if (out_len != NULL)
    {
        *out_len = s_key_len;
    }
    return s_key;
}

void boot_key_wipe(void)
{
    mbedtls_platform_zeroize(s_key, sizeof(s_key));
    s_key_len = 0U;
}

#endif /* IMAGE_CRYPTO_ENABLE */
