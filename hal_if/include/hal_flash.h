#ifndef HAL_FLASH_H
#define HAL_FLASH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash 抽象接口: 用于巡检/CRC 校验.
 * 平台实现由宿主工程提供 (如 soc_port_mcu 或 stm32_hal_port)
 */

bool     hal_flash_read(uint32_t addr, uint8_t* buf, size_t len);
uint32_t hal_flash_get_app_addr(void);
uint32_t hal_flash_get_app_size(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_FLASH_H */
