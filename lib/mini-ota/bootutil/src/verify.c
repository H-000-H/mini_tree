/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: verify.c
 * @brief: 纯数据校验实现 (见 verify.h)
 */
#include "verify.h"

#include "boot_config.h"
#include "crc.h"
#include "flash.h"

/** image_verify_area 的读回调上下文 */
typedef struct
{
    const flash_area_t *area;
    uint32_t            off;
} verify_area_ctx_t;

/**
 * @brief: image_verify_raw 的 flash 读回调: 把相对偏移平移到区域内的绝对偏移
 */
static int verify_area_read(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const verify_area_ctx_t *c = (const verify_area_ctx_t *)ctx;
    return flash_area_read_operation(c->area, c->off + offset, buf, len);
}

int image_verify_raw(image_read_fn read_fn, void *read_ctx, uint32_t len, uint32_t expected_crc)
{
    uint8_t      chunk[IMAGE_VERIFY_CHUNK];
    crc_stream_t crc;
    uint32_t     offset = 0u;

    if (read_fn == NULL || len == 0u)
        return ERR_ARG;

    crc_stream_start(&crc, CRC_MODEL_INIT, CRC_MODEL_REFIN, CRC_MODEL_REFOUT, CRC_MODEL_XOR_OUT, CRC_MODEL_POLY, 32u);

    while (offset < len)
    {
        uint32_t want = len - offset;
        int      ret;

        if (want > (uint32_t)sizeof(chunk))
            want = (uint32_t)sizeof(chunk);

        ret = read_fn(read_ctx, offset, chunk, want);
        if (ret != ERR_OK)
            return ret;

        crc_stream_feed(&crc, chunk, want);
        offset += want;
    }

    return (crc_stream_finish(&crc) == expected_crc) ? ERR_OK : ERR_CRC_MISMATCH;
}

int image_verify_area(uint32_t area_id, uint32_t off, uint32_t len, uint32_t expected_crc)
{
    const flash_area_t *area = NULL;
    verify_area_ctx_t   ctx;
    int                 ret;

    ret = flash_area_open(area_id, &area);
    if (ret != ERR_OK)
        return ret;
    if (area == NULL)
        return ERR_NOT_SUPPORTED;

    ctx.area = area;
    ctx.off  = off;
    return image_verify_raw(verify_area_read, &ctx, len, expected_crc);
}
