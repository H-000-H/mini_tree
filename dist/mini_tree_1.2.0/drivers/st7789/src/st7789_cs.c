/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file st7789_cs.c
 * @brief ST7789 有 CS 驱动入口 — CS 由父 SPI master client 硬件片选完成
 * @note compatible: sitronix,st7789
 */
#include "driver.h"
#include "st7789_core.h"

/**
 * @brief 有 CS 场景 probe（require_nocs=0）
 */
static int st7789_probe(struct device* pdev) { return st7789_probe_common(pdev, 0); }

DRIVER_REGISTER(st7789, "sitronix,st7789", st7789_probe, st7789_remove_common)
