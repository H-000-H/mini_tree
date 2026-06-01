#ifndef VFS_PWM_H
#define VFS_PWM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWM_CMD_SET_DUTY      0x01
#define PWM_CMD_GET_DUTY      0x02
#define PWM_CMD_SET_FREQ      0x03
#define PWM_CMD_DEINIT        0x04

#ifdef __cplusplus
}
#endif

#endif /* VFS_PWM_H */
