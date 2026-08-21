/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file a7670_drv.h
 *@brief A7670 4G 模块驱动 — 复用模组统一抽象层 (modem_drv.h)
 *@author H-000-H
 *@details
 *   挂在 UART 总线 client 下的 VFS 设备驱动；
 *   业务经 device_open/ioctl/close 访问。
 *   AT 命令与收发结构统一在 drivers/modem/modem_drv.h (MODEM_CMD_* /
 *   struct modem_at_buf), 本文件仅做接口说明, 不重复定义。
 *   PPP 字节流走 fops read/write (见驱动 .c)。
 */

#ifndef A7670_DRV_H
#define A7670_DRV_H

#include "drivers/modem/include/modem_drv.h"

#endif /* A7670_DRV_H */
