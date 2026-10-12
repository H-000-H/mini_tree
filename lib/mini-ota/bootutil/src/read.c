/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: read.c
 * @brief: OTA 镜像读取实现（见 read.h）：布局解析（image_*）与
 *         各模式的校验/解密分支都在这里
 */
#include "read.h"

#include <string.h>

#include "boot_config.h" /* CRC_MODEL_*：镜像使用的 CRC 模型，可用 CONFIG_CRC_* 覆盖 */
#include "crc.h"

/* ============================ 镜像布局解析 ============================ */

/**
 * @brief 解码镜像尾部的自描述 meta(4B)
 * @param m:           meta 原始 4 字节（magic | mode|is_front | version_len | tag_len）
 * @param mode:        [out] 打包模式
 * @param is_front:    [out] 非 0 表示元数据在头部
 * @param version_len: [out] version 字符串长度
 * @param tag_len:     [out] tag 字符串长度
 * @return ERR_OK / ERR_ARG（magic 不符或 mode 非法）
 */
static int meta_decode(const uint8_t m[IMAGE_META_LEN], image_check_t *mode, int *is_front,
                       size_t *version_len, size_t *tag_len)
{
    if (m == NULL || mode == NULL || is_front == NULL ||
        version_len == NULL || tag_len == NULL)
    {
        return ERR_ARG;
    }
    if (m[0] != IMAGE_META_MAGIC || (m[1] & 0x7Fu) > (uint8_t)IMAGE_CHECK_CBC_SHA)
    {
        return ERR_ARG;
    }

    *mode = (image_check_t)(m[1] & 0x7Fu);
    *is_front = (m[1] & 0x80u) ? 1 : 0;
    *version_len = m[2];
    *tag_len = m[3];
    return ERR_OK;
}

int image_aux_len(image_check_t mode, size_t *out_len)
{
    size_t len;

    if (out_len == NULL)
    {
        return ERR_ARG;
    }

    switch (mode)
    {
    case IMAGE_CHECK_CRC:     len = 0u;                                     break;
    case IMAGE_CHECK_SHA:     len = 32u;                                    break;
    case IMAGE_CHECK_GCM:     len = (size_t)(12u + 16u);                    break;
    case IMAGE_CHECK_CBC:     len = 16u;                                    break;
    case IMAGE_CHECK_CBC_SHA: len = (size_t)(16u + 32u);                    break;
    default:                  return ERR_ARG;
    }

    *out_len = len;
    return ERR_OK;
}

int image_overhead(image_check_t mode, size_t version_len, size_t tag_len,
                   size_t *out_overhead)
{
    size_t aux_len = 0;
    int rc;

    if (out_overhead == NULL)
    {
        return ERR_ARG;
    }

    rc = image_aux_len(mode, &aux_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    /* 逐项比较，避免 version_len/tag_len 取极大值时加法回绕 */
    if (version_len > (SIZE_MAX - IMAGE_CRC_LEN - IMAGE_META_LEN) ||
        tag_len > (SIZE_MAX - IMAGE_CRC_LEN - IMAGE_META_LEN - version_len) ||
        aux_len > (SIZE_MAX - IMAGE_CRC_LEN - IMAGE_META_LEN - version_len - tag_len))
    {
        return ERR_OVERFLOW;
    }

    *out_overhead = IMAGE_CRC_LEN + IMAGE_META_LEN + version_len + tag_len + aux_len;
    return ERR_OK;
}

int image_parse(const uint8_t *buf, size_t len, image_view_t *out_view)
{
    size_t overhead = 0;
    size_t aux_len = 0;
    image_check_t mode;
    int is_front;
    size_t version_len;
    size_t tag_len;
    const uint8_t *crc_bytes;
    int rc;

    if (buf == NULL || out_view == NULL || len < (IMAGE_CRC_LEN + IMAGE_META_LEN))
    {
        return ERR_ARG;
    }

    /* mode/version_len/tag_len/is_front 全部来自镜像尾部的 meta，无需带外约定 */
    rc = meta_decode(buf + len - IMAGE_META_LEN, &mode, &is_front, &version_len, &tag_len);
    if (rc != ERR_OK)
    {
        return rc;
    }
    rc = image_overhead(mode, version_len, tag_len, &overhead);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (len < overhead)
    {
        return ERR_TOO_SMALL;
    }

    rc = image_aux_len(mode, &aux_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    out_view->mode = mode;
    out_view->is_front = is_front;
    out_view->aux_len = aux_len;
    out_view->version_len = version_len;
    out_view->tag_len = tag_len;
    out_view->payload_len = len - overhead;

    if (is_front)
    {
        /* crc | version | tag | aux | payload */
        crc_bytes = buf;
        out_view->version = buf + IMAGE_CRC_LEN;
        out_view->tag = out_view->version + version_len;
        out_view->aux = out_view->tag + tag_len;
        out_view->payload = out_view->aux + aux_len;
    }
    else
    {
        /* payload | aux | version | tag | crc */
        out_view->payload = buf;
        out_view->aux = out_view->payload + out_view->payload_len;
        out_view->version = out_view->aux + aux_len;
        out_view->tag = out_view->version + version_len;
        crc_bytes = out_view->tag + tag_len;
    }

    out_view->crc_stored = (uint32_t)crc_bytes[0] |
                           ((uint32_t)crc_bytes[1] << 8) |
                           ((uint32_t)crc_bytes[2] << 16) |
                           ((uint32_t)crc_bytes[3] << 24);
    return ERR_OK;
}

/* ============================ 校验与解密 ============================ */


#if IMAGE_CRYPTO_ENABLE
#include "algorithm.h"

/*
 * @brief: 加密模式下的缓冲区比较，每次都比较所有字节，避免提前返回导致加密无效增强密钥的安全性
 * @param a: 待比较的第一个缓冲区
 * @param b: 待比较的第二个缓冲区
 * @param len: 比较长度
 * @return: 相等返回 1，不等返回 0
*/
static int ct_equal(const uint8_t *a, const uint8_t *b, size_t len)
{
    uint8_t diff = 0;
    size_t i;

    for (i = 0; i < len; i++)
    {
        diff = (uint8_t)(diff | (uint8_t)(a[i] ^ b[i]));
    }
    return diff == 0;
}
#endif

static uint32_t calc_crc(const image_view_t *view)
{
    crc_stream_t cs;

    crc_stream_start(&cs, CRC_MODEL_INIT, CRC_MODEL_REFIN, CRC_MODEL_REFOUT,
                     CRC_MODEL_XOR_OUT, CRC_MODEL_POLY, CRC_MODEL_WIDTH);
    crc_stream_feed(&cs, view->payload, view->payload_len);

    return crc_stream_finish(&cs);
}

/** 明文模式（CRC/SHA）：out 为 NULL 时只报长度不拷贝 */
static int copy_plain(const image_view_t *view, uint8_t *out, size_t cap,
                      size_t *out_plain_len)
{
    if (out_plain_len != NULL)
    {
        *out_plain_len = view->payload_len;
    }
    if (out == NULL)
    {
        return ERR_OK;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }
    memcpy(out, view->payload, view->payload_len);
    return ERR_OK;
}

/* ---------------- 各模式处理分支 ---------------- */

static int check_sha(const image_view_t *view)
{
#if IMAGE_CRYPTO_ENABLE
    uint8_t digest[IMAGE_HASH_LEN];
    sha256_stream_t sh;

    if (view->aux_len < IMAGE_HASH_LEN)
    {
        return ERR_TOO_SMALL;
    }
    if (sha256_begin(&sh) != 0)
    {
        return ERR_HASH_FAILED;
    }
    if (sha256_feed(&sh, view->payload, view->payload_len) != 0)
    {
        return ERR_HASH_FAILED;
    }
    if (sha256_end(&sh, digest) != 0)
    {
        return ERR_HASH_FAILED;
    }
    return ct_equal(digest, view->aux, IMAGE_HASH_LEN) ? ERR_OK : ERR_HASH_MISMATCH;
#else
    (void)view;
    return ERR_UNSUPPORTED;
#endif
}

#if IMAGE_CRYPTO_ENABLE
/** PKCS#7 去填充，返回真实明文长度 */
static int pkcs7_unpad(const uint8_t *data, size_t len, size_t *out_len)
{
    uint8_t pad;
    size_t i;

    if (data == NULL || out_len == NULL || len == 0 || (len % IMAGE_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }

    pad = data[len - 1];
    if (pad == 0 || pad > IMAGE_CBC_BLOCK_LEN || (size_t)pad > len)
    {
        return ERR_PADDING;
    }
    for (i = 0; i < (size_t)pad; i++)
    {
        if (data[len - 1 - i] != pad)
        {
            return ERR_PADDING;
        }
    }

    *out_len = len - pad;
    return ERR_OK;
}

/** AES-CBC 解密 + 去填充（iv 取自 aux 前 16B） */
static int cbc_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                       uint8_t *out, size_t cap, size_t *out_plain_len)
{
    size_t plain_len = 0;
    cbc_stream_t cbc;
    int rc;

    if (view->aux_len < IMAGE_CBC_IV_LEN)
    {
        return ERR_TOO_SMALL;
    }
    if (view->payload_len == 0 || (view->payload_len % IMAGE_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }

    /* stream begin 内部会存 IV 副本，不会改写只读的镜像数据 */
    rc = aes_cbc_decrypt_stream_begin(&cbc, view->aux, key, key_len);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }
    rc = aes_cbc_decrypt_stream_feed(&cbc, view->payload, out, view->payload_len);
    aes_cbc_stream_free(&cbc);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }

    rc = pkcs7_unpad(out, view->payload_len, &plain_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    if (out_plain_len != NULL)
    {
        *out_plain_len = plain_len;
    }
    return ERR_OK;
}
#endif /* IMAGE_READ_ENABLE_CRYPTO */

static int gcm_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                       uint8_t *out, size_t cap, size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    gcm_stream_t g;
    int rc;

    if (view->aux_len < (size_t)(IMAGE_GCM_NONCE_LEN + IMAGE_GCM_TAG_LEN))
    {
        return ERR_TOO_SMALL;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }

    rc = aes_gcm_decrypt_stream_begin(&g, view->aux, IMAGE_GCM_NONCE_LEN, key, key_len);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }
    /* GCM 为 CTR 族模式，支持原地解密 */
    rc = aes_gcm_stream_feed(&g, view->payload, out, view->payload_len);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }
    /* GCM 解密失败只可能是标签不符或参数错误，统一按认证失败处理 */
    rc = aes_gcm_decrypt_stream_finish(&g, view->aux + IMAGE_GCM_NONCE_LEN, IMAGE_GCM_TAG_LEN);
    if (rc != 0)
    {
        return ERR_AUTH_FAILED;
    }

    if (out_plain_len != NULL)
    {
        *out_plain_len = view->payload_len;
    }
    return ERR_OK;
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

static int cbc_decrypt_dispatch(const image_view_t *view, const uint8_t *key,
                                size_t key_len, uint8_t *out, size_t cap,
                                size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    return cbc_decrypt(view, key, key_len, out, cap, out_plain_len);
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

/** CBC_SHA：先对 IV||密文 验 HMAC，通过后才解密（覆盖范围须与打包端一致） */
static int cbc_sha_decrypt(const image_view_t *view, const uint8_t *key, size_t key_len,
                           const uint8_t *mac_key, size_t mac_key_len,
                           uint8_t *out, size_t cap, size_t *out_plain_len)
{
#if IMAGE_CRYPTO_ENABLE
    uint8_t iv[IMAGE_CBC_IV_LEN];
    uint8_t calc[IMAGE_HASH_LEN];
    size_t plain_len = 0;
    hmac_sha256_stream_t h;
    cbc_stream_t cbc;
    int rc;

    if (view->aux_len < (size_t)(IMAGE_CBC_IV_LEN + IMAGE_HASH_LEN))
    {
        return ERR_TOO_SMALL;
    }
    if (view->payload_len == 0 || (view->payload_len % IMAGE_CBC_BLOCK_LEN) != 0)
    {
        return ERR_ARG;
    }
    if (cap < view->payload_len)
    {
        return ERR_BUF_TOO_SMALL;
    }

    /* 第一遍：对 IV||密文 算 HMAC 并常量时间比对（verify-then-decrypt 的验证段） */
    memcpy(iv, view->aux, IMAGE_CBC_IV_LEN);
    rc = hmac_sha256_stream_begin(&h, mac_key, mac_key_len);
    if (rc == 0)
    {
        rc = hmac_sha256_stream_feed(&h, iv, IMAGE_CBC_IV_LEN);
    }
    if (rc == 0)
    {
        rc = hmac_sha256_stream_feed(&h, view->payload, view->payload_len);
    }
    if (rc == 0)
    {
        rc = hmac_sha256_stream_end(&h, calc);
    }
    if (rc != 0)
    {
        return ERR_AUTH_FAILED;
    }
    if (!ct_equal(calc, view->aux + IMAGE_CBC_IV_LEN, IMAGE_HASH_LEN))
    {
        return ERR_AUTH_FAILED;
    }

    /* 认证通过才解密 */
    rc = aes_cbc_decrypt_stream_begin(&cbc, iv, key, key_len);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }
    rc = aes_cbc_decrypt_stream_feed(&cbc, view->payload, out, view->payload_len);
    aes_cbc_stream_free(&cbc);
    if (rc != 0)
    {
        return ERR_DECRYPT_FAILED;
    }

    rc = pkcs7_unpad(out, view->payload_len, &plain_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    if (out_plain_len != NULL)
    {
        *out_plain_len = plain_len;
    }
    return ERR_OK;
#else
    (void)view;
    (void)key;
    (void)key_len;
    (void)mac_key;
    (void)mac_key_len;
    (void)out;
    (void)cap;
    (void)out_plain_len;
    return ERR_UNSUPPORTED;
#endif
}

/* ---------------- 对外接口 ---------------- */

/** 加密模式共用的入参检查 */
static int check_crypto_args(const image_read_cfg_t *cfg, const uint8_t *out_payload)
{
    if (cfg->key == NULL)
    {
        return ERR_ARG;
    }
    if (cfg->key_len != 16u && cfg->key_len != 24u && cfg->key_len != 32u)
    {
        return ERR_ARG;
    }
    /* 解密必须给出输出缓冲区，没有地方可写就无法完成认证 */
    if (out_payload == NULL)
    {
        return ERR_ARG;
    }
    return ERR_OK;
}

int image_verify_stream(image_read_fn read_fn, void *read_ctx, uint32_t img_size,
                        const image_read_cfg_t *cfg,
                        uint8_t *scratch, uint32_t scratch_size, uint32_t *out_crc)
{
    size_t aux_len = 0;
    size_t overhead = 0;
    uint32_t crc_stored = 0;
    uint32_t payload_off;
    size_t payload_len;
    uint32_t off;
    size_t remaining;
    uint32_t chunk;
    image_check_t mode;
    int is_front;
    size_t version_len;
    size_t tag_len;
    int rc;

    if (read_fn == NULL || cfg == NULL || scratch == NULL || scratch_size < 16U ||
        img_size < (IMAGE_CRC_LEN + IMAGE_META_LEN))
    {
        return ERR_ARG;
    }

    /* meta 恒在镜像最后 4B：mode/version_len/tag_len/is_front 由此得出 */
    rc = read_fn(read_ctx, img_size - IMAGE_META_LEN, scratch, IMAGE_META_LEN);
    if (rc != ERR_OK)
    {
        return rc;
    }
    rc = meta_decode(scratch, &mode, &is_front, &version_len, &tag_len);
    if (rc != ERR_OK)
    {
        return rc;
    }

    rc = image_aux_len(mode, &aux_len);
    if (rc != ERR_OK)
    {
        return rc;
    }
    rc = image_overhead(mode, version_len, tag_len, &overhead);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (img_size < overhead)
    {
        return ERR_TOO_SMALL;
    }
    payload_len = img_size - overhead;

    /* 读镜像中记录的 CRC32（小端）：is_front=1 在头部，=0 在 meta 之前 */
    {
        uint32_t crc_off = is_front ? 0U
                                    : (uint32_t)(img_size - IMAGE_CRC_LEN - IMAGE_META_LEN);

        rc = read_fn(read_ctx, crc_off, scratch, IMAGE_CRC_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }
        crc_stored = (uint32_t)scratch[0] | ((uint32_t)scratch[1] << 8) |
                     ((uint32_t)scratch[2] << 16) | ((uint32_t)scratch[3] << 24);
    }

    /* payload 起始偏移：is_front=1 在元数据之后；=0 从镜像头开始 */
    payload_off = is_front
                  ? (uint32_t)(IMAGE_CRC_LEN + version_len + tag_len + aux_len)
                  : 0U;

    if (out_crc != NULL)
    {
        *out_crc = crc_stored;
    }

    switch (mode)
    {
    case IMAGE_CHECK_CRC:
    {
        crc_stream_t cs;
        uint32_t calc;

        crc_stream_start(&cs, CRC_MODEL_INIT, CRC_MODEL_REFIN, CRC_MODEL_REFOUT,
                         CRC_MODEL_XOR_OUT, CRC_MODEL_POLY, CRC_MODEL_WIDTH);

        off = payload_off;
        remaining = payload_len;
        while (remaining > 0)
        {
            chunk = (remaining > scratch_size) ? scratch_size : (uint32_t)remaining;
            rc = read_fn(read_ctx, off, scratch, chunk);
            if (rc != ERR_OK)
            {
                return rc;
            }
            crc_stream_feed(&cs, scratch, chunk);
            off += chunk;
            remaining -= chunk;
        }

        calc = crc_stream_finish(&cs);
        return (calc == crc_stored) ? ERR_OK : ERR_CRC_MISMATCH;
    }
    case IMAGE_CHECK_CBC:
        /* CBC 本身无认证属性，与一次性接口一致：布局检查通过即视为校验通过 */
        return ERR_OK;

#if IMAGE_CRYPTO_ENABLE
    case IMAGE_CHECK_SHA:
    {
        uint32_t aux_off;
        uint8_t expect[IMAGE_HASH_LEN];
        uint8_t digest[IMAGE_HASH_LEN];
        sha256_stream_t sh;

        /* aux 里存放期望的 SHA-256 摘要（32B） */
        aux_off = is_front
                  ? (uint32_t)(IMAGE_CRC_LEN + version_len + tag_len)
                  : (uint32_t)(img_size - IMAGE_CRC_LEN - IMAGE_META_LEN - version_len - tag_len - IMAGE_HASH_LEN);

        rc = read_fn(read_ctx, aux_off, expect, IMAGE_HASH_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }

        rc = sha256_begin(&sh);
        if (rc != 0)
        {
            return ERR_HASH_FAILED;
        }

        off = payload_off;
        remaining = payload_len;
        while (remaining > 0)
        {
            chunk = (remaining > scratch_size) ? scratch_size : (uint32_t)remaining;
            rc = read_fn(read_ctx, off, scratch, chunk);
            if (rc != ERR_OK)
            {
                return rc;
            }
            rc = sha256_feed(&sh, scratch, chunk);
            if (rc != 0)
            {
                return ERR_HASH_FAILED;
            }
            off += chunk;
            remaining -= chunk;
        }

        rc = sha256_end(&sh, digest);
        if (rc != 0)
        {
            return ERR_HASH_FAILED;
        }

        return ct_equal(digest, expect, IMAGE_HASH_LEN) ? ERR_OK : ERR_HASH_MISMATCH;
    }
    case IMAGE_CHECK_GCM:
    {
        uint32_t aux_off;
        uint8_t nonce[IMAGE_GCM_NONCE_LEN];
        uint8_t tag[IMAGE_GCM_TAG_LEN];
        gcm_stream_t g;

        if (cfg->key == NULL)
        {
            return ERR_ARG;
        }

        /* aux 里存放 nonce(12B) + tag(16B) */
        aux_off = is_front
                  ? (uint32_t)(IMAGE_CRC_LEN + version_len + tag_len)
                  : (uint32_t)(img_size - IMAGE_CRC_LEN - IMAGE_META_LEN - version_len - tag_len - IMAGE_GCM_NONCE_LEN - IMAGE_GCM_TAG_LEN);

        rc = read_fn(read_ctx, aux_off, nonce, IMAGE_GCM_NONCE_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }
        rc = read_fn(read_ctx, aux_off + IMAGE_GCM_NONCE_LEN, tag, IMAGE_GCM_TAG_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }

        rc = aes_gcm_decrypt_stream_begin(&g, nonce, IMAGE_GCM_NONCE_LEN, cfg->key, cfg->key_len);
        if (rc != 0)
        {
            return ERR_DECRYPT_FAILED;
        }

        /* 流式解密到 scratch（CTR 模式支持原地），认证通过前明文即弃 */
        off = payload_off;
        remaining = payload_len;
        while (remaining > 0)
        {
            chunk = (remaining > scratch_size) ? scratch_size : (uint32_t)remaining;
            rc = read_fn(read_ctx, off, scratch, chunk);
            if (rc != ERR_OK)
            {
                return rc;
            }
            rc = aes_gcm_stream_feed(&g, scratch, scratch, chunk);
            if (rc != 0)
            {
                return ERR_DECRYPT_FAILED;
            }
            off += chunk;
            remaining -= chunk;
        }

        rc = aes_gcm_decrypt_stream_finish(&g, tag, IMAGE_GCM_TAG_LEN);
        return (rc == 0) ? ERR_OK : ERR_AUTH_FAILED;
    }
    case IMAGE_CHECK_CBC_SHA:
    {
        uint32_t aux_off;
        uint8_t iv[IMAGE_CBC_IV_LEN];
        uint8_t hmac[IMAGE_HASH_LEN];
        uint8_t calc[IMAGE_HASH_LEN];
        const uint8_t *mac_key = (cfg->mac_key != NULL) ? cfg->mac_key : cfg->key;
        size_t mac_key_len = (cfg->mac_key != NULL) ? cfg->mac_key_len : cfg->key_len;
        hmac_sha256_stream_t h;

        if (mac_key == NULL)
        {
            return ERR_ARG;
        }

        /* aux 里存放 iv(16B) + hmac(32B) */
        aux_off = is_front
                  ? (uint32_t)(IMAGE_CRC_LEN + version_len + tag_len)
                  : (uint32_t)(img_size - IMAGE_CRC_LEN - IMAGE_META_LEN - version_len - tag_len - IMAGE_CBC_IV_LEN - IMAGE_HASH_LEN);

        rc = read_fn(read_ctx, aux_off, iv, IMAGE_CBC_IV_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }
        rc = read_fn(read_ctx, aux_off + IMAGE_CBC_IV_LEN, hmac, IMAGE_HASH_LEN);
        if (rc != ERR_OK)
        {
            return rc;
        }

        /* 第一遍：流式 HMAC 验证 IV||密文（verify-then-decrypt 的验证段） */
        rc = hmac_sha256_stream_begin(&h, mac_key, mac_key_len);
        if (rc != 0)
        {
            return ERR_AUTH_FAILED;
        }
        rc = hmac_sha256_stream_feed(&h, iv, IMAGE_CBC_IV_LEN);
        if (rc != 0)
        {
            return ERR_AUTH_FAILED;
        }

        off = payload_off;
        remaining = payload_len;
        while (remaining > 0)
        {
            chunk = (remaining > scratch_size) ? scratch_size : (uint32_t)remaining;
            rc = read_fn(read_ctx, off, scratch, chunk);
            if (rc != ERR_OK)
            {
                return rc;
            }
            rc = hmac_sha256_stream_feed(&h, scratch, chunk);
            if (rc != 0)
            {
                return ERR_AUTH_FAILED;
            }
            off += chunk;
            remaining -= chunk;
        }

        rc = hmac_sha256_stream_end(&h, calc);
        if (rc != 0)
        {
            return ERR_AUTH_FAILED;
        }

        return ct_equal(calc, hmac, IMAGE_HASH_LEN) ? ERR_OK : ERR_AUTH_FAILED;
    }
#endif /* IMAGE_CRYPTO_ENABLE */
    default:
        return ERR_NOT_SUPPORTED;
    }
}

int image_read_payload(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
                       uint8_t *out_payload, size_t payload_cap,
                       image_view_t *out_view, size_t *out_plain_len,
                       uint32_t *out_crc_calc)
{
    image_view_t view;
    int rc;

    if (buf == NULL || cfg == NULL)
    {
        return ERR_ARG;
    }

    rc = image_parse(buf, len, &view);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (out_view != NULL)
    {
        *out_view = view;
    }

    switch (view.mode)
    {
    case IMAGE_CHECK_CRC:
    {
        uint32_t calc = calc_crc(&view);

        if (out_crc_calc != NULL)
        {
            *out_crc_calc = calc;
        }
        if (calc != view.crc_stored)
        {
            return ERR_CRC_MISMATCH;
        }
        return copy_plain(&view, out_payload, payload_cap, out_plain_len);
    }
    case IMAGE_CHECK_SHA:
        rc = check_sha(&view);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return copy_plain(&view, out_payload, payload_cap, out_plain_len);
    case IMAGE_CHECK_GCM:
        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return gcm_decrypt(&view, cfg->key, cfg->key_len, out_payload, payload_cap,
                           out_plain_len);
    case IMAGE_CHECK_CBC:
        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return cbc_decrypt_dispatch(&view, cfg->key, cfg->key_len, out_payload,
                                    payload_cap, out_plain_len);
    case IMAGE_CHECK_CBC_SHA:
    {
        /* 未单独给出 MAC 密钥时回退用加密密钥 */
        const uint8_t *mac_key = (cfg->mac_key != NULL) ? cfg->mac_key : cfg->key;
        size_t mac_key_len = (cfg->mac_key != NULL) ? cfg->mac_key_len : cfg->key_len;

        rc = check_crypto_args(cfg, out_payload);
        if (rc != ERR_OK)
        {
            return rc;
        }
        return cbc_sha_decrypt(&view, cfg->key, cfg->key_len, mac_key, mac_key_len,
                               out_payload, payload_cap, out_plain_len);
    }
    default:
        return ERR_ARG;
    }
}
