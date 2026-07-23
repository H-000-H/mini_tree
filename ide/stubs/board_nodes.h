/* IDE-only stub — real header from dtc-lite at build time.
 * Minimal device_id_t / DEV_ID_COUNT so board/device/bus/vfs headers parse.
 */
#ifndef BOARD_NODES_H
#define BOARD_NODES_H

#include <stdint.h>

typedef enum {
    DEV_ID_NONE = 0,
    DEV_ID_COUNT = 1
} device_id_t;

/* chosen 占位 (真实值由板级 DTS 生成) */
#ifndef CHOSEN_SCHEDULER_TIM
#define CHOSEN_SCHEDULER_TIM ((device_id_t)0)
#endif

#endif /* BOARD_NODES_H */
