#include "board_nodes.h"
#include "board_devtable.h"
#include "device.h"

/* ===== probe 函数声明 ===== */

/* ===== remove 函数声明 ===== */

/* ===== probe 函数表 (按 DEV_ID 索引, .rodata) ===== */
static const probe_fn_t s_probe_fns[DEV_ID_COUNT] = {
    [DEV_ID_] = NULL,
    [DEV_ID_UART_40010000] = NULL,
    [DEV_ID_I2C_40020000] = NULL,
    [DEV_ID_SPI_40030000] = NULL,
    [DEV_ID_ADC_40040000] = NULL,
    [DEV_ID_PWM_40050000] = NULL,
    [DEV_ID_GPIO_40060000] = NULL,
};

/* ===== remove 函数表 (按 DEV_ID 索引, .rodata) ===== */
static const remove_fn_t s_remove_fns[DEV_ID_COUNT] = {
    [DEV_ID_] = NULL,
    [DEV_ID_UART_40010000] = NULL,
    [DEV_ID_I2C_40020000] = NULL,
    [DEV_ID_SPI_40030000] = NULL,
    [DEV_ID_ADC_40040000] = NULL,
    [DEV_ID_PWM_40050000] = NULL,
    [DEV_ID_GPIO_40060000] = NULL,
};

/* ===== probe 顺序 (按依赖拓扑排序) ===== */
static const device_id_t s_probe_order[DEV_ID_COUNT] = {
    DEV_ID_,
    DEV_ID_UART_40010000,
    DEV_ID_I2C_40020000,
    DEV_ID_SPI_40030000,
    DEV_ID_ADC_40040000,
    DEV_ID_PWM_40050000,
    DEV_ID_GPIO_40060000,
};

/* ===== API ===== */

probe_fn_t board_probe_get_fn(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return s_probe_fns[id];
}

remove_fn_t board_remove_get_fn(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return s_remove_fns[id];
}

const device_id_t* board_probe_order(void) {
    return s_probe_order;
}

int board_probe_order_count(void) {
    return DEV_ID_COUNT;
}

/* ===== 故障传播表 (编译期预计算, 替代运行时 BFS) ===== */
