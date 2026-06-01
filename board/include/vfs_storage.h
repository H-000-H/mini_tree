#ifndef VFS_STORAGE_H
#define VFS_STORAGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 通用块存储 IOCTL 规范 ──
 * 适用于 SPI Flash、EEPROM、SD 卡等任意存储介质，
 * 不绑定任何具体存储颗粒或总线协议。
 */

#define STORAGE_IOC_GET_GEOMETRY   0x20  /* 获取扇区大小与总容量 */
#define STORAGE_IOC_ERASE_SECTOR   0x21  /* 物理擦除指定扇区 */
#define STORAGE_IOC_WRITE_PROTECT  0x22  /* 开启/关闭硬件写保护 */

typedef struct
{
    uint32_t sector_size;       /* 扇区大小 (字节, 如 4096) */
    uint32_t sector_count;      /* 总扇区数 */
} vfs_storage_geometry_t;

typedef struct
{
    uint32_t sector;            /* 目标扇区号 */
} vfs_storage_erase_arg_t;

typedef struct
{
    bool enable;                /* true = 开启写保护, false = 关闭 */
} vfs_storage_wp_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* VFS_STORAGE_H */
