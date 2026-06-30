/* SPDX-License-Identifier: Apache-2.0 */
/*
 * bh_config — BH 下半部可调参数默认值
 *
 * BH_QUEUE_DEPTH 必须为 2 的幂 (默认 32), 可在 board_config.h 覆盖
 * bh_os 任务默认栈深/优先级/名称, 均可被工程侧覆盖
 */
#ifndef BH_CONFIG_H
#define BH_CONFIG_H

#include <stdint.h>

/* 队列深度, 必须是 2 的幂 (可在 board_config.h 或编译选项中覆盖) */
#ifndef BH_QUEUE_DEPTH
#define BH_QUEUE_DEPTH 32U
#endif

/* bh_os 专用任务默认参数 (可在工程侧覆盖) */
#ifndef BH_TASK_STACK_SIZE
#define BH_TASK_STACK_SIZE 2048U
#endif

#ifndef BH_TASK_PRIORITY
#define BH_TASK_PRIORITY 5U
#endif

#ifndef BH_TASK_NAME
#define BH_TASK_NAME "bh_task"
#endif

#endif /* BH_CONFIG_H */
