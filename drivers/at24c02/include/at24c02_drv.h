/**
 * SPDX-License-Identifier: Apache-2.0
 * @file at24c02_drv.h
 */
#ifndef AT24C02_DRV_H
#define AT24C02_DRV_H

#include <stddef.h>
#include <stdint.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AT24C02_SIZE             256U

#define AT24C02_CMD_BASE         COMPAT_MAGIC(AT24C02)
#define AT24C02_CMD_READ         (AT24C02_CMD_BASE + 0x01)
#define AT24C02_CMD_WRITE        (AT24C02_CMD_BASE + 0x02)
#define AT24C02_CMD_COUNT        2

struct at24c02_io_arg {
    uint8_t  offset;
    uint8_t* buf;
    size_t   len;
};

#ifdef __cplusplus
}
#endif

#endif /* AT24C02_DRV_H */
