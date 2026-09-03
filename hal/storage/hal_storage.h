/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_storage.h
 *@brief hal storage 头文件
 *@author H-000-H
 *@details
 *   Storage HAL — 双槽 A/B 持久化存储抽象
 *   slot 0/1 配合单字节 flag 标记当前有效槽位
 *   提供 blob 读写、全擦除及扇区级 ioctl (geometry/erase/wp)
 */

#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*持久化存储 API*/
/* -------------------------------------------------------------------------- */
/**
 * @brief 初始化存储介质 (探测介质并识别有效槽位)
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码 (weak stub 返回 MINI_ERR_NOTSUPP)
 */
int hal_storage_init(void);
/**
 * @brief 读取槽位元数据标记
 * @param[out] flag 回传当前有效槽位标记
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
int hal_storage_read_flag(uint8_t* flag);
/**
 * @brief 写入槽位元数据标记
 * @param[in] flag 待写入的有效槽位标记
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
int hal_storage_write_flag(uint8_t flag);
/**
 * @brief 读取指定槽位的 blob 数据
 * @param[in] slot 槽位号 (0=A 区, 1=B 区)
 * @param[out] buf 数据缓冲区
 * @param[in,out] len 入参=缓冲区容量, 出参=实际读取长度
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
int hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len);
/**
 * @brief 写入 blob 数据到指定槽位
 * @param[in] slot 槽位号 (0=A 区, 1=B 区)
 * @param[in] buf 数据源
 * @param[in] len 数据长度
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
int hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len);
/**
 * @brief 全擦除存储介质
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
int hal_storage_erase_all(void);
/* -------------------------------------------------------------------------- */

/*ioctl 命令与参数结构*/
/* -------------------------------------------------------------------------- */
#define STORAGE_IOC_GET_GEOMETRY 0x20  /* 获取扇区大小与总容量 */
#define STORAGE_IOC_ERASE_SECTOR 0x21  /* 物理擦除指定扇区 */
#define STORAGE_IOC_WRITE_PROTECT 0x22 /* 开启/关闭硬件写保护 */

/** @brief 存储介质几何参数 (扇区大小 + 总数) */
struct storage_geometry
{
    uint32_t sector_size;  /**< 扇区大小 (字节, 如 4096) */
    uint32_t sector_count; /**< 总扇区数 */
};

/** @brief 扇区擦除参数 (ioctl ERASE_SECTOR) */
struct storage_erase_arg
{
    uint32_t sector; /**< 目标扇区号 */
};

/** @brief 写保护开关参数 (ioctl WRITE_PROTECT) */
struct storage_wp_arg
{
    bool enable; /**< true = 开启写保护, false = 关闭 */
};
/* -------------------------------------------------------------------------- */

#ifdef __cplusplus
}
#endif

#endif /* HAL_STORAGE_H */
