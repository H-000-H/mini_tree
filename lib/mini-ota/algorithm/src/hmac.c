/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: hmac.c
 * @brief: HMAC-SHA-256 增量（流式）封装（mbedtls md 接口），返回 0 表示成功
 */
#include <stddef.h>
#include "algorithm.h"
#include "mbedtls/md.h"

int hmac_sha256_stream_begin(hmac_sha256_stream_t *s, const unsigned char *key, size_t key_len)
{
    const mbedtls_md_info_t *md_info;
    int ret;

    if (s == NULL || key == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    mbedtls_md_init(&s->md);

    ret = mbedtls_md_setup(&s->md, md_info, 1);
    if (ret != 0)
    {
        mbedtls_md_free(&s->md);
        return ret;
    }

    ret = mbedtls_md_hmac_starts(&s->md, key, key_len);
    if (ret != 0)
    {
        mbedtls_md_free(&s->md);
    }
    return ret;
}

int hmac_sha256_stream_feed(hmac_sha256_stream_t *s, const unsigned char *data, size_t len)
{
    if (s == NULL || data == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    return mbedtls_md_hmac_update(&s->md, data, len);
}

int hmac_sha256_stream_end(hmac_sha256_stream_t *s, unsigned char output[32])
{
    int ret;

    if (s == NULL || output == NULL)
    {
        return MBEDTLS_ERR_MD_BAD_INPUT_DATA;
    }

    ret = mbedtls_md_hmac_finish(&s->md, output);
    mbedtls_md_free(&s->md);
    return ret;
}
