#ifndef BOARD_DEVTABLE_H
#define BOARD_DEVTABLE_H

#include "board_nodes.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 编译期节点访问 (静态 .rodata) */
const device_node_t* board_node_get(device_id_t id);
int board_dev_count(void);
device_id_t board_dev_find(const char* name);
device_id_t board_dev_find_by_compat(const char* compatible);
device_id_t board_dev_find_by_label(const char* label);

/* 运行时设备实例访问 (由 board_device.c 管理) */
device_t* board_dev_get(device_id_t id);

/* probe 顺序表 (按依赖拓扑排序) */
const device_id_t* board_probe_order(void);
int board_probe_order_count(void);

/* probe / remove 调度 */
typedef int (*probe_fn_t)(device_t*);
typedef int (*remove_fn_t)(device_t*);
probe_fn_t board_probe_get_fn(device_id_t id);
remove_fn_t board_remove_get_fn(device_id_t id);

/* 故障传播表: id 失败时应一并禁用的设备列表 */
const device_id_t* board_cascade_get(device_id_t id, int* count);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DEVTABLE_H */
