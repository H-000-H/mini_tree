/**
 * SPDX-License-Identifier: Apache-2.0
 * @file sht30_drv.h
 */
#ifndef SHT30_DRV_H
#define SHT30_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHT30_CMD_BASE           COMPAT_MAGIC(SHT30)
#define SHT30_CMD_READ_TEMP_RH   (SHT30_CMD_BASE + 0x01)
#define SHT30_CMD_COUNT          1

struct sht30_sample {
    int16_t temp_c_x100;  /**< 摄氏度 ×100 */
    uint16_t rh_x100;     /**< 相对湿度 ×100 (0..10000) */
};

#ifdef __cplusplus
}
#endif

#endif /* SHT30_DRV_H */
