/**
 * @copyright: SPDX-License-Identifier: Apache-2.0
 * @author:  H-000-H
 * @file: sha.c
 * @brief: SHA-256 增量（流式）封装（mbedtls sha256 接口），返回 0 表示成功
 */
#include "algorithm.h"

int sha256_begin(sha256_stream_t *s)
{
    if (s == NULL)
    {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }

    mbedtls_sha256_init(&s->ctx);
    return mbedtls_sha256_starts_ret(&s->ctx, 0);
}

int sha256_feed(sha256_stream_t *s, const uint8_t *data, size_t len)
{
    if (s == NULL || data == NULL)
    {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }

    return mbedtls_sha256_update_ret(&s->ctx, data, len);
}

int sha256_end(sha256_stream_t *s, uint8_t output[32])
{
    int ret;

    if (s == NULL || output == NULL)
    {
        return MBEDTLS_ERR_SHA256_BAD_INPUT_DATA;
    }

    ret = mbedtls_sha256_finish_ret(&s->ctx, output);
    mbedtls_sha256_free(&s->ctx);
    return ret;
}
