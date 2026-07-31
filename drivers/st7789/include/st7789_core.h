/* SPDX-License-Identifier: Apache-2.0 */
#ifndef ST7789_CORE_H
#define ST7789_CORE_H

#include "device.h"

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief ST7789 公共 probe
 * @param require_nocs 非 0 时断言父 SPI client cs-pin < 0
 */
int st7789_probe_common(struct device* dev, int require_nocs);

/** @brief ST7789 公共 remove（有 CS / 无 CS 共用） */
int st7789_remove_common(struct device* dev);

#ifdef __cplusplus
}
#endif

#endif /* ST7789_CORE_H */
