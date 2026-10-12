/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file ota_state_flash.c
 * @brief 内置状态后端：flash 状态扇区（追加日志）。
 * @note  记录 2 字（8B）追加在扇区内，只有扇区写满（或遇到半写记录）才擦一次，
 *        因此"每次状态迁移"的写入不会造成频繁擦除；半写掉电时校验字失败，日志
 *        即在该处截断，上一条有效记录仍可用（校验字最后写是前提）。
 *        区域必须独占一个擦除扇区（见 flash.h 的 FLASH_AREA_ID_STATE）。
 */
#include "ota_state.h"
#include "flash.h"
#include "err.h"
#include <stddef.h>

#define OTA_STATE_REC_BYTES ((uint32_t)OTA_STATE_RECORD_WORDS * 4u)
#define OTA_STATE_BLANK_WORD 0xFFFFFFFFu

typedef struct
{
    uint32_t used;   /* 连续有效记录的总字节数（= 下一条记录的写偏移） */
    uint32_t latest[OTA_STATE_RECORD_WORDS]; /* 最近一条有效记录快照 */
    int      has;    /* 是否找到有效记录 */
} state_scan_t;

/* 从偏移 0 顺序扫描：遇到"空白"或"校验失败"即认为结束 */
static int state_scan(const flash_area_t *area, state_scan_t *out)
{
    uint32_t off = 0u;

    out->used = 0u;
    out->has = 0;

    while ((off + OTA_STATE_REC_BYTES) <= area->fa_size)
    {
        uint32_t record[OTA_STATE_RECORD_WORDS];
        int rc = flash_area_read_operation(area, off, record, OTA_STATE_REC_BYTES);
        if (rc != ERR_OK)
        {
            return rc;
        }
        if ((record[0] == OTA_STATE_BLANK_WORD) && (record[1] == OTA_STATE_BLANK_WORD))
        {
            break; /* 空白：日志正常结束 */
        }
        if (!ota_state_record_valid(record))
        {
            break; /* 半写/损坏：日志在此截断 */
        }

        out->latest[0] = record[0];
        out->latest[1] = record[1];
        out->has = 1;
        off += OTA_STATE_REC_BYTES;
        out->used = off;
    }
    return ERR_OK;
}

static int flash_state_load(uint32_t record[OTA_STATE_RECORD_WORDS])
{
    const flash_area_t *area = NULL;
    state_scan_t scan;
    int rc = flash_area_open(FLASH_AREA_ID_STATE, &area);

    if (rc != ERR_OK)
    {
        return rc;
    }
    if (area == NULL)
    {
        return ERR_ARG;
    }

    rc = state_scan(area, &scan);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if (!scan.has)
    {
        return ERR_OTA_STATE;
    }

    record[0] = scan.latest[0];
    record[1] = scan.latest[1];
    return ERR_OK;
}

static int flash_state_store(const uint32_t record[OTA_STATE_RECORD_WORDS])
{
    const flash_area_t *area = NULL;
    state_scan_t scan;
    uint32_t off;
    uint32_t probe[OTA_STATE_RECORD_WORDS];
    int rc = flash_area_open(FLASH_AREA_ID_STATE, &area);

    if (rc != ERR_OK)
    {
        return rc;
    }
    if ((area == NULL) || (area->fa_size < OTA_STATE_REC_BYTES))
    {
        return ERR_ARG;
    }

    rc = state_scan(area, &scan);
    if (rc != ERR_OK)
    {
        return rc;
    }
    off = scan.used;

    /* 落点必须空白；写满、或遇到半写记录（落点非空白）则先擦一次 */
    if ((off + OTA_STATE_REC_BYTES) > area->fa_size)
    {
        rc = flash_area_erase_operation(area, 0u, area->fa_size);
        off = 0u;
    }
    else
    {
        rc = flash_area_read_operation(area, off, probe, OTA_STATE_REC_BYTES);
        if (rc == ERR_OK)
        {
            if ((probe[0] != OTA_STATE_BLANK_WORD) || (probe[1] != OTA_STATE_BLANK_WORD))
            {
                rc = flash_area_erase_operation(area, 0u, area->fa_size);
                off = 0u;
            }
        }
    }
    if (rc != ERR_OK)
    {
        return rc;
    }

    /* 先写状态字、后写校验字：半写掉电时该条记录整体校验失败 */
    rc = flash_area_write_operation(area, off, &record[0], 4u);
    if (rc != ERR_OK)
    {
        return rc;
    }
    return flash_area_write_operation(area, off + 4u, &record[1], 4u);
}

static const ota_state_ops_t s_flash_state_ops =
{
    flash_state_load,
    flash_state_store,
};

int ota_state_flash_register(void)
{
    return ota_state_ops_register(&s_flash_state_ops);
}
