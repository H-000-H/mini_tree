/**
 * @file a7670_drv.h
 * @author H-000-H
 * @brief A7670 4G 模块驱动 — 复用模组统一抽象层 (modem_drv.h)
 * @note 挂在 UART 总线 client 下的 VFS 设备驱动；
 * @note 业务经 device_open/ioctl/close 访问。
 * @note AT 命令与收发结构统一在 drivers/modem/modem_drv.h (MODEM_CMD_* /
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef A7670_DRV_H
#define A7670_DRV_H

#include "drivers/modem/include/modem_drv.h"

#endif /* A7670_DRV_H */
