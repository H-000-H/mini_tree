#include "board_nodes.h"
#include "board_devtable.h"
#include "device.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ===== 属性表 (静态 .rodata) ===== */

/* / */
static const device_prop_t DEV__props[] = {
    {"model", "your-board"},
};

/* /soc/uart@40010000 */
static const device_prop_t DEV_uart_40010000_props[] = {
    {"reg", "0x40010000 0x400"},
};

/* /soc/i2c@40020000 */
static const device_prop_t DEV_i2c_40020000_props[] = {
    {"reg", "0x40020000 0x400"},
};

/* /soc/spi@40030000 */
static const device_prop_t DEV_spi_40030000_props[] = {
    {"reg", "0x40030000 0x400"},
};

/* /soc/adc@40040000 */
static const device_prop_t DEV_adc_40040000_props[] = {
    {"reg", "0x40040000 0x400"},
};

/* /soc/pwm@40050000 */
static const device_prop_t DEV_pwm_40050000_props[] = {
    {"reg", "0x40050000 0x400"},
    {"channels", "0x8"},
};

/* /soc/gpio@40060000 */
static const device_prop_t DEV_gpio_40060000_props[] = {
    {"reg", "0x40060000 0x400"},
};

/* ===== 依赖表 ===== */

/* ===== reg 分组表 (预分组, 按 #address-cells / #size-cells) ===== */

static const uint32_t DEV_uart_40010000_REG_DATA[] = {
    0x40010000, 0x400,
};
static const uint32_t DEV_i2c_40020000_REG_DATA[] = {
    0x40020000, 0x400,
};
static const uint32_t DEV_spi_40030000_REG_DATA[] = {
    0x40030000, 0x400,
};
static const uint32_t DEV_adc_40040000_REG_DATA[] = {
    0x40040000, 0x400,
};
static const uint32_t DEV_pwm_40050000_REG_DATA[] = {
    0x40050000, 0x400,
};
static const uint32_t DEV_gpio_40060000_REG_DATA[] = {
    0x40060000, 0x400,
};
static const device_reg_t DEV_uart_40010000_REGS[] = {
    { .addr = &DEV_uart_40010000_REG_DATA[0], .addr_cells = 1, .size = &DEV_uart_40010000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_40020000_REGS[] = {
    { .addr = &DEV_i2c_40020000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_40020000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_spi_40030000_REGS[] = {
    { .addr = &DEV_spi_40030000_REG_DATA[0], .addr_cells = 1, .size = &DEV_spi_40030000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_adc_40040000_REGS[] = {
    { .addr = &DEV_adc_40040000_REG_DATA[0], .addr_cells = 1, .size = &DEV_adc_40040000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_pwm_40050000_REGS[] = {
    { .addr = &DEV_pwm_40050000_REG_DATA[0], .addr_cells = 1, .size = &DEV_pwm_40050000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio_40060000_REGS[] = {
    { .addr = &DEV_gpio_40060000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio_40060000_REG_DATA[1], .size_cells = 1 },
};

/* ===== 主节点表 (只读 .rodata) ===== */
static const device_node_t s_nodes[DEV_ID_COUNT] = {
    [DEV_ID_] = {
        .name       = "",
        .label      = "",
        .compatible = "your-project",
        .path       = "/",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV__props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
    },
    [DEV_ID_UART_40010000] = {
        .name       = "uart@40010000",
        .label      = "uart0",
        .compatible = "vendor,uart",
        .path       = "/soc/uart@40010000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_uart_40010000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_uart_40010000_REGS,
    },
    [DEV_ID_I2C_40020000] = {
        .name       = "i2c@40020000",
        .label      = "i2c0",
        .compatible = "vendor,i2c-bus",
        .path       = "/soc/i2c@40020000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_i2c_40020000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_40020000_REGS,
    },
    [DEV_ID_SPI_40030000] = {
        .name       = "spi@40030000",
        .label      = "spi0",
        .compatible = "vendor,spi-bus",
        .path       = "/soc/spi@40030000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_spi_40030000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_spi_40030000_REGS,
    },
    [DEV_ID_ADC_40040000] = {
        .name       = "adc@40040000",
        .label      = "adc0",
        .compatible = "vendor,adc",
        .path       = "/soc/adc@40040000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_adc_40040000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_adc_40040000_REGS,
    },
    [DEV_ID_PWM_40050000] = {
        .name       = "pwm@40050000",
        .label      = "pwm0",
        .compatible = "vendor,pwm",
        .path       = "/soc/pwm@40050000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 2,
        .props      = DEV_pwm_40050000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_pwm_40050000_REGS,
    },
    [DEV_ID_GPIO_40060000] = {
        .name       = "gpio@40060000",
        .label      = "gpio0",
        .compatible = "vendor,gpio",
        .path       = "/soc/gpio@40060000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_gpio_40060000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio_40060000_REGS,
    },
};

/* ===== API 实现 ===== */

const device_node_t* board_node_get(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return &s_nodes[id];
}

int board_dev_count(void) { return DEV_ID_COUNT; }

device_id_t board_dev_find(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (strcmp(s_nodes[i].name, name) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_compat(const char* compatible) {
    if (!compatible) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].compatible[0] &&
            strcmp(s_nodes[i].compatible, compatible) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_label(const char* label) {
    if (!label || !label[0]) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].label[0] &&
            strcmp(s_nodes[i].label, label) == 0)
            return (device_id_t)i;
    }
    return -1;
}
