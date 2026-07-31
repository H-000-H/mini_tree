/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ST7789 有 CS 驱动 — CS 由父 SPI master client 硬件片选完成
 * compatible: sitronix, st7789
 */
#include "st7789_core.h"
#include "driver.h"

static int st7789_probe(struct device* dev)
{
    return st7789_probe_common(dev, 0);
}

DRIVER_REGISTER(st7789, "sitronix,st7789", st7789_probe, st7789_remove_common)
