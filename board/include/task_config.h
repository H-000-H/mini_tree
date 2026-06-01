#ifndef TASK_CONFIG_H
#define TASK_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── RMS 单调速率调度优先级 (数值越高优先级越高) ── */

/* ── 任务配置结构 (由宿主工程定义实例, 配合 board_task_create 使用) ── */
typedef struct
{
    const char* name;
    uint32_t    stack_size;
    uint32_t    priority;
    int         core_id;        /* 目标核心, tskNO_AFFINITY = 不固定 */
} board_task_config_t;

#ifdef __cplusplus
}
#endif

#endif /* TASK_CONFIG_H */
