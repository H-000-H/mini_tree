/*
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author: H-000-H
 * @file: flash.h
 * @brief: flash 操作接口的分发层：平台通过 flash_ops_register 注册 open/erase/write/read 实现
 *         （+ 可选的 get_sectors），未注册时所有接口返回 ERR_NOT_SUPPORTED。
 *         不依赖弱符号（GCC 扩展），PC 端测试可直接注册 mock
 */
#ifndef BOOTUTIL_INC_FLASH_H
#define BOOTUTIL_INC_FLASH_H
#if defined(__cplusplus)
extern "C" {
#endif
typedef struct flash_area flash_area_t;
typedef struct flash_sector flash_sector_t;
#include <stdint.h>

typedef enum 
{
    FLASH_AREA_ID_IMAGE_0=0,
    FLASH_AREA_ID_IMAGE_1,
    FLASH_AREA_ID_BOOTLOADER,
    FLASH_AREA_ID_STATE, /**< OTA 持久化状态区：必须独占一个擦除扇区，与镜像/引导区不共用 */
} flash_area_id_t;
/**
 * @brief: flash 扇区信息 桥接结构体
 * @param fs_off: 相对 flash 区域起始偏移（和 *_operation 的 off 一致）
 * @param fs_size: sector 大小
 */
struct flash_sector
{
    uint32_t fs_off;   // 相对 area 起始偏移（和 *_operation 的 off 一致）
    uint32_t fs_size;  // sector 大小
};

struct flash_area
{
    uint32_t fa_id;             /**< flash 区域 ID*/
    uint32_t fa_device_id;      /**< flash 外部设备 ID */
    uint32_t fa_offset;         /**< flash 区域在设备上的偏移*/
    uint32_t fa_size;           /**< flash 区域大小 */
};

/**
 * @brief: flash 移植操作表：open/erase/write/read 必填，get_sectors 可选
 */
typedef struct flash_ops
{
    int (*open)(uint32_t fa_id, const flash_area_t **area);                              /**< 打开区域 */
    int (*erase)(const flash_area_t *area, uint32_t off, uint32_t len);                  /**< 擦除（覆盖到的扇区全擦，向上取整） */
    int (*write)(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len); /**< 写入 */
    int (*read)(const flash_area_t *area, uint32_t off, void *buf, uint32_t len);        /**< 读取 */
    int (*get_sectors)(const flash_area_t *area, uint32_t max_count,
                       flash_sector_t *sectors, uint32_t *count);                        /**< 可选：扇区清单（未实现时该接口返回 ERR_NOT_SUPPORTED） */
} flash_ops_t;

/**
 * @brief: 注册平台 flash 操作表（启动阶段调用一次；ops 指针与成员运行期须保持有效）
 * @param ops: 平台实现的操作表（open/erase/write/read 必填；get_sectors 可为 NULL）
 * @return: 0 表示成功，负数为失败原因（见 err.h）；ops 或必填成员为 NULL 返回 ERR_ARG
 */
int flash_ops_register(const flash_ops_t *ops);

/**
 * @brief: 打开 flash 区域（分发到注册的 ops->open）
 * @param fa_id: flash 区域 ID
 * @param area: 输出 flash 区域描述
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_open(uint32_t fa_id, const flash_area_t **area);

/**
 * @brief: 擦除 flash 区域
 * @param area: flash 区域描述
 * @param off: 擦除偏移（相对于区域起始地址）
 * @param len: 擦除长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_erase_operation(const flash_area_t *area, uint32_t off, uint32_t len);

/**
 * @brief: 写 flash 区域
 * @param area: flash 区域描述
 * @param off: 写入偏移（相对于区域起始地址）
 * @param buf: 待写入数据缓冲区
 * @param len: 待写入数据长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_write_operation(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len);

/**
 * @brief: 读 flash 区域
 * @param area: flash 区域描述
 * @param off: 读取偏移（相对于区域起始地址）
 * @param buf: 输出缓冲区
 * @param len: 待读取数据长度
 * @return: 0 表示成功，负数为失败原因（见 err.h）
 */
int flash_area_read_operation(const flash_area_t *area, uint32_t off, void *buf, uint32_t len);

/**
 * @brief: 获取 flash 区域的扇区信息
 * @param area[in]: flash 区域描述
 * @param max_count[in]: 输入参数，sectors 数组容量
 * @param sectors[out]: 输出参数，扇区信息数组
 * @param count[out]: 输出参数，实际扇区数量
 * @return: 0 表示成功；平台未实现 get_sectors 时返回 ERR_NOT_SUPPORTED（可选能力）
 */
int flash_area_get_sectors(const flash_area_t *area, uint32_t max_count,
                           flash_sector_t *sectors, uint32_t *count);

#if defined(__cplusplus)
}
#endif
#endif /* BOOTUTIL_INC_FLASH_H */