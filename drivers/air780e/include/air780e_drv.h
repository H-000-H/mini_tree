/**
 * @file air780e_drv.h
 * @author H-000-H
 * @brief Air780E 4G 模块驱动 — 复用模组统一抽象层 (modem_drv.h)
 * @note 挂在 UART 总线 client 下的 VFS 设备驱动；
 * @note 业务经 device_open/ioctl/close 访问。
 * @note AT 命令与收发结构统一在 drivers/modem/modem_drv.h (MODEM_CMD_* /
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef AIR780E_DRV_H
#define AIR780E_DRV_H

#include "drivers/modem/include/modem_drv.h"

#endif /* AIR780E_DRV_H */
