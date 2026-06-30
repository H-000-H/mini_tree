/* SPDX-License-Identifier: Apache-2.0 */
/*
 * task_config.h — 板级任务配置结构头文件
 *
 * 定义 board_task_config 结构 (name/stack_size/priority/core_id),
 * 由用户工程定义实例, 配合 board_task_create 创建任务.
 * core_id 指定目标核心, tskNO_AFFINITY 表示不固定核心.
 */
#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/* ── RMS 单调速率调度优先级 (数值越高优先级越高) ── */

/* ── 任务配置结构 (由用户工程定义实例, 配合 board_task_create 使用) ── */
struct board_task_config

{
    const char* name;
    uint32_t    stack_size;
    uint32_t    priority;
    int         core_id;        /* 目标核心, tskNO_AFFINITY = 不固定 */
};

#ifdef __cplusplus
}
#endif

#endif /* TASK_CONFIG_H */

