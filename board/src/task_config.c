/* ═══════════════════════════════════════════════════════════════════
 *  task_config.c — 系统任务配置实例
 *
 *  本文件仅提供 board_task_config_t 类型定义与创建工具.
 *  具体的业务任务实例 (如 UI、Cloud) 由用户工程自行定义.
 *
 *  优先级说明:
 *    FreeRTOS 后端: 0 = 最低, 31 = 最高
 *    RT-Thread 后端: 0 = 最高, 31 = 最低
 *    用户任务请避开框架保留优先级:
 *      EventBus 分发: FreeRTOS=30, RT-Thread=1
 *      Scrubber:      FreeRTOS=1,  RT-Thread=30
 *
 *  用途示例 (用户工程 main.c):
 *     const board_task_config_t my_app_task = {
 *         .name       = "my_app",
 *         .stack_size = 4096,
 *         .priority   = 15,   // 中等优先级
 *         .core_id    = tskNO_AFFINITY,
 *     };
 *     board_task_create(my_app_task.name, my_app_task.stack_size,
 *                       my_app_task.priority, my_entry, NULL,
 *                       my_app_task.core_id);
 * ═══════════════════════════════════════════════════════════════════ */

#include "task_config.h"
