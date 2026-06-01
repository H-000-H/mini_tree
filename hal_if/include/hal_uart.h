#ifndef HAL_UART_H
#define HAL_UART_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_uart hal_uart_t;

typedef struct
{
    int tx_pin;
    int rx_pin;
    int rts_pin;   /* -1 = 未使用 */
    int cts_pin;   /* -1 = 未使用 */
    int baud_rate;
    int data_bits; /* 5、6、7、8 */
    int stop_bits; /* 1 或 2 */
    int parity;    /* 0 = 无, 1 = 奇校验, 2 = 偶校验 */
} hal_uart_config_t;

struct hal_uart
{
    int (*init)(hal_uart_t* uart, const hal_uart_config_t* cfg);
    int (*write)(hal_uart_t* uart, const uint8_t* data, size_t len);
    int (*read)(hal_uart_t* uart, uint8_t* data, size_t len, uint32_t timeout_ms);
    int (*deinit)(hal_uart_t* uart);
    void* _impl;
};

void hal_uart_init_struct(hal_uart_t* uart);

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
