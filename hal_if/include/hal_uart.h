#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_uart hal_uart_t;

/* UART 配置 */
typedef struct
{
    int     uart_id;    /* UART 控制器编号, 0 = UART0 */
    int     tx_pin;
    int     rx_pin;
    int     rts_pin;    /* -1 = 未使用 */
    int     cts_pin;    /* -1 = 未使用 */
    int     baud_rate;
    int     data_bits;  /* 5、6、7、8 */
    int     stop_bits;  /* 1 或 2 */
    int     parity;     /* 0 = 无, 1 = 奇校验, 2 = 偶校验 */
} hal_uart_config_t;

/* UART 接收回调 */
typedef void (*hal_uart_rx_callback_t)(hal_uart_t* uart, uint8_t data, void* user_data);

struct hal_uart
{
    int (*init)(hal_uart_t* uart, const hal_uart_config_t* cfg);
    int (*write)(hal_uart_t* uart, const uint8_t* data, size_t len);
    int (*read)(hal_uart_t* uart, uint8_t* data, size_t len, uint32_t timeout_ms);
    int (*set_rx_callback)(hal_uart_t* uart, hal_uart_rx_callback_t cb, void* user_data);
    int (*deinit)(hal_uart_t* uart);
    void* _impl;
};

void hal_uart_init_struct(hal_uart_t* uart);
void hal_uart_force_stop(void);

/* ioctl 兼容层 */
#define UART_CMD_READ       0x30
#define UART_CMD_DEINIT     0x31
#define UART_CMD_SET_BAUD   0x32

typedef struct
{
    uint8_t* data;
    size_t len;
    uint32_t timeout_ms;
} uart_read_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_UART_H */
