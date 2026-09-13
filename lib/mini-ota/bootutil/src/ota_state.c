/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file ota_state.c
 * @brief OTA 持久化状态核心：记录组装/校验 + 后端分发。与具体介质无关。
 */
#include "ota_state.h"
#include "err.h"
#include "crc.h"
#include <stddef.h>

static const ota_state_ops_t *s_state_ops = NULL;

uint32_t ota_state_crc32(uint32_t state_word)
{
    /* 格式写死了如果要换随便换一下就行就是状态字的crc值而已想用什么都无所谓 */
    crc_stream_t stream;
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(state_word >> 0);
    bytes[1] = (uint8_t)(state_word >> 8);
    bytes[2] = (uint8_t)(state_word >> 16);
    bytes[3] = (uint8_t)(state_word >> 24);

    crc_stream_start(&stream, 0xFFFFFFFFu, 1, 1, 0xFFFFFFFFu, 0x04C11DB7u, 32u);
    crc_stream_feed(&stream, bytes, sizeof(bytes));
    return crc_stream_finish(&stream);
}

static uint32_t ota_state_with_magic(uint32_t state_word)
{
    return (state_word & ~OTA_STATE_MAGIC_MASK) |((OTA_STATE_MAGIC_VALUE << OTA_STATE_MAGIC_SHIFT) & OTA_STATE_MAGIC_MASK);
}

void ota_state_record_build(uint32_t record[OTA_STATE_RECORD_WORDS], uint32_t state_word)
{
    uint32_t word = ota_state_with_magic(state_word);

    record[0] = word;
    record[1] = ota_state_crc32(word);
}

int ota_state_record_valid(const uint32_t record[OTA_STATE_RECORD_WORDS])
{
    if ((record[0] & OTA_STATE_MAGIC_MASK) !=((OTA_STATE_MAGIC_VALUE << OTA_STATE_MAGIC_SHIFT) & OTA_STATE_MAGIC_MASK))
    {
        return 0;
    }
    return (record[1] == ota_state_crc32(record[0])) ? 1 : 0;
}

int ota_state_resolve_pending(uint32_t *state, uint32_t fail_code)
{
    uint32_t current;

    if (state == NULL)
    {
        return 0;
    }
    if (ota_state_pending_get(*state) == 0u)
    {
        return 0;
    }

    /* 只有两个镜像区，回滚目标就是另一个分区，无需额外保存 old_partition */
    current = ota_state_partition_get(*state);
    *state = ota_state_bit_put(*state, OTA_STATE_BIT_CURRENT,
                               (current == OTA_STATE_PARTITION_IMAGE_1)? OTA_STATE_PARTITION_IMAGE_0: OTA_STATE_PARTITION_IMAGE_1);
    *state = ota_state_bit_put(*state, OTA_STATE_BIT_PENDING, 0u);
    *state = ota_state_bit_put(*state, OTA_STATE_BIT_TRIAL, 0u);
    *state = ota_state_fail_put(*state, fail_code);
    return 1;
}

int ota_state_ops_register(const ota_state_ops_t *ops)
{
    if ((ops == NULL) || (ops->load == NULL) || (ops->store == NULL))
    {
        return ERR_ARG;
    }
    s_state_ops = ops;
    return ERR_OK;
}

int ota_state_load(uint32_t *state_word)
{
    uint32_t record[OTA_STATE_RECORD_WORDS];
    int rc;

    if (state_word == NULL)
    {
        return ERR_ARG;
    }
    if (s_state_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }

    rc = s_state_ops->load(record);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (!ota_state_record_valid(record))
    {
        return ERR_OTA_STATE;
    }

    *state_word = record[0];
    return ERR_OK;
}

int ota_state_store(uint32_t state_word)
{
    uint32_t record[OTA_STATE_RECORD_WORDS];

    if (s_state_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }

    ota_state_record_build(record, state_word);
    return s_state_ops->store(record);
}

int ota_state_update(uint32_t mask, uint32_t value)
{
    uint32_t cur = 0u;

    if (s_state_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    if (ota_state_load(&cur) != ERR_OK)
    {
        cur = 0u; /* 无有效记录：以 0 为底，只写入本次指定的位 */
    }
    return ota_state_store(((cur & ~mask) | (value & mask)) & OTA_STATE_DURABLE_MASK);
}
