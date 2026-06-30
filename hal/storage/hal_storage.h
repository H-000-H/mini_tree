/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Storage HAL — 双槽 A/B 持久化存储抽象
 *
 * slot 0/1 配合单字节 flag 标记当前有效槽位
 * 提供 blob 读写、全擦除及扇区级 ioctl (geometry/erase/wp)
 */
#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

                                                            /*持久化存储 API*/
/*===========================================================================================================================================================*/
/* 持久化存储抽象接口: 用于配置存储双槽 A/B 方案
 *
 * slot: 0 = A 区, 1 = B 区
 * flag: 单字节元数据标记 (当前有效槽位)
 */
bool hal_storage_init(void);
bool hal_storage_read_flag(uint8_t* flag);
bool hal_storage_write_flag(uint8_t flag);
bool hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len);
bool hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len);
bool hal_storage_erase_all(void);
/*===========================================================================================================================================================*/

                                                            /*ioctl 命令与参数结构*/
/*===========================================================================================================================================================*/
#define STORAGE_IOC_GET_GEOMETRY   0x20  /* 获取扇区大小与总容量 */
#define STORAGE_IOC_ERASE_SECTOR   0x21  /* 物理擦除指定扇区 */
#define STORAGE_IOC_WRITE_PROTECT  0x22  /* 开启/关闭硬件写保护 */

struct storage_geometry
{
    uint32_t sector_size;       /* 扇区大小 (字节, 如 4096) */
    uint32_t sector_count;      /* 总扇区数 */
};

struct storage_erase_arg
{
    uint32_t sector;            /* 目标扇区号 */
};

struct storage_wp_arg
{
    bool enable;                /* true = 开启写保护, false = 关闭 */
};
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* HAL_STORAGE_H */
