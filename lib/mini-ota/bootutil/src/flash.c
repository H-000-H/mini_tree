/*
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author: H-000-H
 * @file: flash.c
 * @brief: flash 操作接口的分发层：平台通过 flash_ops_register 注册 open/erase/write/read
 *         实现（get_sectors 可选），未注册时统一返回 ERR_NOT_SUPPORTED。
 *         不依赖弱符号（GCC 扩展），PC 端测试可直接注册 mock
 */
#include "flash.h"
#include "err.h"
#include <stddef.h>

static const flash_ops_t *s_flash_ops = NULL;

int flash_ops_register(const flash_ops_t *ops)
{
    if ((ops == NULL) || (ops->open == NULL) || (ops->erase == NULL) ||
        (ops->write == NULL) || (ops->read == NULL))
    {
        return ERR_ARG;
    }
    s_flash_ops = ops;
    return ERR_OK;
}

int flash_area_open(uint32_t fa_id, const flash_area_t **area)
{
    if (s_flash_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    return s_flash_ops->open(fa_id, area);
}

int flash_area_erase_operation(const flash_area_t *area, uint32_t off, uint32_t len)
{
    if (s_flash_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    return s_flash_ops->erase(area, off, len);
}

int flash_area_write_operation(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    if (s_flash_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    return s_flash_ops->write(area, off, buf, len);
}

int flash_area_read_operation(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    if (s_flash_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    return s_flash_ops->read(area, off, buf, len);
}

int flash_area_get_sectors(const flash_area_t *area, uint32_t max_count,
                           flash_sector_t *sectors, uint32_t *count)
{
    if (s_flash_ops == NULL)
    {
        return ERR_NOT_SUPPORTED;
    }
    if (s_flash_ops->get_sectors == NULL)
    {
        return ERR_NOT_SUPPORTED; /* 可选能力：平台未实现 */
    }
    return s_flash_ops->get_sectors(area, max_count, sectors, count);
}
