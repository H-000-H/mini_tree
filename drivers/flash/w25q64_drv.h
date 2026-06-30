/* SPDX-License-Identifier: Apache-2.0 */
/*
 * W25Q64 驱动 — SPI NOR Flash 应用层接口
 *
 * 定义页/扇区/块容量与 JEDEC ID 校验 (EF 40 17)
 * MTD 风格 ioctl: SEEK / SECTOR_ERASE / READ_JEDEC_ID
 */
#ifndef W25Q64_DRV_H
#define W25Q64_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define W25Q64_PAGE_SIZE             256U
#define W25Q64_SECTOR_SIZE           4096U
#define W25Q64_BLOCK_SIZE_32K        32768U
#define W25Q64_BLOCK_SIZE_64K        65536U
#define W25Q64_FLASH_SIZE            (8U * 1024U * 1024U)
#define W25Q64_JEDEC_ID_LEN          3U
#define W25Q64_JEDEC_MANUFACTURER    0xEFU
#define W25Q64_JEDEC_MEMORY_TYPE     0x40U
#define W25Q64_JEDEC_CAPACITY        0x17U

/*
 * 应用层 (MTD 风格):
 *   open → ioctl(SEEK) → read/write → ioctl(SECTOR_ERASE) → ioctl(READ_JEDEC_ID) → close
 */
#define W25Q64_CMD_BASE              (COMPAT_MAGIC(SPI) + 0x80)
#define W25Q64_CMD_SEEK              (W25Q64_CMD_BASE + 0x01)
#define W25Q64_CMD_SECTOR_ERASE      (W25Q64_CMD_BASE + 0x02)
#define W25Q64_CMD_READ_JEDEC_ID     (W25Q64_CMD_BASE + 0x03)

struct w25q64_jedec_arg
{
    uint8_t id[W25Q64_JEDEC_ID_LEN];
};

static inline int w25q64_jedec_match_w25q64jv(const uint8_t id[W25Q64_JEDEC_ID_LEN])
{
    return id && id[0] == W25Q64_JEDEC_MANUFACTURER &&
           id[1] == W25Q64_JEDEC_MEMORY_TYPE &&
           id[2] == W25Q64_JEDEC_CAPACITY;
}

#ifdef __cplusplus
}
#endif

#endif /* W25Q64_DRV_H */
