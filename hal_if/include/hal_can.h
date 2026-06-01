#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_can hal_can_t;

/* CAN 波特率 */
typedef enum
{
    HAL_CAN_SPEED_125K,     /* 125 kbit/s */
    HAL_CAN_SPEED_250K,     /* 250 kbit/s */
    HAL_CAN_SPEED_500K,     /* 500 kbit/s */
    HAL_CAN_SPEED_1M,       /* 1 Mbit/s */
} hal_can_speed_t;

/* 帧格式 */
typedef enum
{
    HAL_CAN_FRAME_STANDARD,     /* 标准帧, 11-bit ID */
    HAL_CAN_FRAME_EXTENDED,     /* 扩展帧, 29-bit ID */
} hal_can_frame_t;

/* CAN 消息结构 */
typedef struct
{
    uint32_t        id;             /* ID(标准帧使用低11位) */
    hal_can_frame_t frame_type;     /* 帧格式 */
    uint8_t         dlc;            /* 数据长度, 0-8 */
    uint8_t         data[8];        /* 数据 */
} hal_can_msg_t;

/* CAN 控制器配置 */
typedef struct
{
    int             can_id;     /* CAN 控制器编号, 0 = CAN1 */
    hal_can_speed_t speed;      /* 波特率 */
    int             tx_pin;     /* TX 引脚 */
    int             rx_pin;     /* RX 引脚 */
} hal_can_config_t;

/* 硬件过滤器 */
typedef struct
{
    uint32_t id;        /* 期望 ID */
    uint32_t mask;      /* 掩码(对应位为0表示忽略) */
    int      is_ext;    /* 0 = 标准帧, 1 = 扩展帧 */
} hal_can_filter_t;

/* 接收回调 */
typedef void (*hal_can_rx_callback_t)(hal_can_t* can, const hal_can_msg_t* msg, void* user_data);

struct hal_can
{
    int (*init)(hal_can_t* can, const hal_can_config_t* cfg);
    int (*send)(hal_can_t* can, const hal_can_msg_t* msg, uint32_t timeout_ms);
    int (*recv)(hal_can_t* can, hal_can_msg_t* msg, uint32_t timeout_ms);
    int (*set_filter)(hal_can_t* can, const hal_can_filter_t* filter, int count);
    int (*set_rx_callback)(hal_can_t* can, hal_can_rx_callback_t cb, void* user_data);
    int (*deinit)(hal_can_t* can);
    void* _impl;
};

void hal_can_init_struct(hal_can_t* can);
void hal_can_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
