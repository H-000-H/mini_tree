/* SPDX-License-Identifier: Apache-2.0 */
/*
 * ST7789 无 CS 驱动 — 父 SPI client cs-pin = -1
 * compatible: sitronix, st7789-nocs
 */
#include "st7789_core.h"
#include "driver.h"

static int st7789_nocs_probe(struct device* dev)
{
    return st7789_probe_common(dev, 1);
}

DRIVER_REGISTER(st7789_nocs, "sitronix,st7789-nocs", st7789_nocs_probe, st7789_remove_common)
