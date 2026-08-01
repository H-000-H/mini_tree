/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file st7789_nocs.c
 * @brief ST7789 无 CS 驱动入口 — 父 SPI client cs-pin = -1（软件不驱动片选）
 * @note compatible: sitronix,st7789-nocs
 */
#include "st7789_core.h"
#include "driver.h"

/**
 * @brief 无 CS 场景 probe（require_nocs=1，断言父 cs-pin < 0）
 */
static int st7789_nocs_probe(struct device* dev)
{
    return st7789_probe_common(dev, 1);
}

DRIVER_REGISTER(st7789_nocs, "sitronix,st7789-nocs", st7789_nocs_probe, st7789_remove_common)
