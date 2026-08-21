/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file board_devtable.h
 *@brief board devtable 头文件
 *@author H-000-H
 *@details
 *   IDE-only stub — real header from dtc-lite at build time
 */

#ifndef BOARD_DEVTABLE_H
#define BOARD_DEVTABLE_H

#include "board_nodes.h"

#ifdef __cplusplus
extern "C"
{
#endif

    struct device;
    struct device_node;

    const struct device_node* board_node_get(device_id_t id);
    int board_dev_count(void);
    device_id_t board_dev_find(const char* name);
    device_id_t board_dev_find_by_compat(const char* compatible);
    device_id_t board_dev_find_by_label(const char* label);

    struct device* board_dev_get(device_id_t id);

    const device_id_t* board_probe_order(void);
    int board_probe_order_count(void);

    typedef int (*probe_fn_t)(struct device*);
    typedef int (*remove_fn_t)(struct device*);
    probe_fn_t board_probe_get_fn(device_id_t id);
    remove_fn_t board_remove_get_fn(device_id_t id);

    const device_id_t* board_cascade_get(device_id_t id, int* count);
    const device_id_t* board_children_get(device_id_t id, int* count);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DEVTABLE_H */
