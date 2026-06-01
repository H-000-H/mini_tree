#ifndef HAL_SDIO_H
#define HAL_SDIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_sdio hal_sdio_t;

/* 总线宽度 */
typedef enum
{
    HAL_SDIO_BUS_1BIT,      /* 1-bit 模式 */
    HAL_SDIO_BUS_4BIT,      /* 4-bit 模式 */
    HAL_SDIO_BUS_8BIT,      /* 8-bit 模式 */
} hal_sdio_bus_width_t;

/* 速度模式 */
typedef enum
{
    HAL_SDIO_SPEED_DEFAULT, /* 默认速度 */
    HAL_SDIO_SPEED_HIGH,    /* 高速模式 */
} hal_sdio_speed_t;

/* SDIO 控制器配置 */
typedef struct
{
    int                 sdio_id;        /* SDIO 控制器编号, 0 = SDIO1 */
    hal_sdio_bus_width_t bus_width;     /* 总线宽度 */
    hal_sdio_speed_t    speed;          /* 速度模式 */
    int                 clk_pin;        /* CLK 引脚 */
    int                 cmd_pin;        /* CMD 引脚 */
    int                 d0_pin;         /* D0 引脚 */
    int                 d1_pin;         /* D1 引脚, -1 = 未用 */
    int                 d2_pin;         /* D2 引脚, -1 = 未用 */
    int                 d3_pin;         /* D3 引脚, -1 = 未用 */
} hal_sdio_config_t;

/* 卡信息 */
typedef struct
{
    uint32_t    sector_size;    /* 扇区大小(字节), 通常 512 */
    uint32_t    sector_count;   /* 总扇区数 */
    uint32_t    card_type;      /* 0 = SD, 1 = MMC, 2 = SDIO */
} hal_sdio_info_t;

struct hal_sdio
{
    int (*init)(hal_sdio_t* sdio, const hal_sdio_config_t* cfg);
    int (*read)(hal_sdio_t* sdio, uint8_t* buf, uint32_t sector, size_t count);
    int (*write)(hal_sdio_t* sdio, const uint8_t* buf, uint32_t sector, size_t count);
    int (*get_info)(hal_sdio_t* sdio, hal_sdio_info_t* info);
    int (*deinit)(hal_sdio_t* sdio);
    void* _impl;
};

void hal_sdio_init_struct(hal_sdio_t* sdio);
void hal_sdio_force_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* HAL_SDIO_H */
