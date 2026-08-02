/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * CAN VFS — SocketCAN 风格帧 + 现有 host/client
 *
 * Driver: can-host / heterogeneous,can-client
 * write/read: struct can_frame; ioctl: TRANSFER / SET_FILTER / GET_STATE
 *@=========================================================================================================================*/
#ifndef CAN_VFS_H
#define CAN_VFS_H

#include "compiler_compat.h"
#include "device.h"
#include "hal_can.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CAN_CMD_BASE COMPAT_MAGIC(CAN)
#define CAN_CMD_TRANSFER (CAN_CMD_BASE + 0x01)
#define CAN_CMD_SET_FILTER (CAN_CMD_BASE + 0x02)
#define CAN_CMD_GET_STATE (CAN_CMD_BASE + 0x03)
#define CAN_CMD_COUNT 3

    struct can_transfer_arg
    {
        struct can_frame tx;
        struct can_frame rx;
        uint32_t rx_fifo;
        uint32_t do_rx;
    };

    struct can_filter_arg
    {
        struct hal_can_filter_config filter;
    };

    struct can_state_arg
    {
        uint32_t state;
    };

#ifdef __cplusplus
}
#endif

#ifndef CAN_VFS_IMPL
#pragma GCC poison can_bus_host_init can_bus_host_deinit
#pragma GCC poison can_bus_client_register can_bus_client_unregister
#pragma GCC poison can_bus_open can_bus_close
#pragma GCC poison can_bus_transmit can_bus_receive can_bus_filter_config can_bus_get_state
#endif

#endif /* CAN_VFS_H */
