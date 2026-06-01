#ifndef HAL_STORAGE_H
#define HAL_STORAGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 持久化存储抽象接口: 用于配置存储双槽 A/B 方案.
 * 平台实现由宿主工程提供 (如 soc_port_mcu 或 stm32_hal_port)
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

#ifdef __cplusplus
}
#endif

#endif /* HAL_STORAGE_H */
