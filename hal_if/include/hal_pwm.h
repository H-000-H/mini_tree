#ifndef HAL_PWM_H
#define HAL_PWM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWM_CMD_SET_DUTY      0x01
#define PWM_CMD_GET_DUTY      0x02
#define PWM_CMD_SET_FREQ      0x03
#define PWM_CMD_DEINIT        0x04

typedef struct hal_pwm_channel hal_pwm_channel_t;

struct hal_pwm_channel
{
    int (*init)(hal_pwm_channel_t* pwm, int pin, int freq_hz, int resolution_bits);
    int (*set_duty)(hal_pwm_channel_t* pwm, uint32_t duty);
    int (*get_duty)(hal_pwm_channel_t* pwm, uint32_t* duty);
    int (*deinit)(hal_pwm_channel_t* pwm);
    void* _impl;
};

void hal_pwm_init_struct(hal_pwm_channel_t* pwm);

void hal_pwm_force_stop_all(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_PWM_H */
