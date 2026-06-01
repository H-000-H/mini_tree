#ifndef HAL_SPI_BUS_H
#define HAL_SPI_BUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_spi_bus hal_spi_bus_t;

struct device_instance;
typedef struct device_instance device_t;

/* SPI 总线配置 */
typedef struct
{
    int host_id;            /* SPI 控制器编号, 0 = SPI1 */
    int mosi;
    int miso;
    int sclk;
    int max_transfer_sz;    /* 最大传输字节 */
    int dma_chan;           /* DMA 通道, -1 = 自动 */
} hal_spi_bus_config_t;

/* SPI 设备配置 */
typedef struct
{
    int mode;               /* SPI 模式 0-3 */
    int clock_speed_hz;     /* 时钟频率(Hz) */
    int cs_pin;             /* 片选引脚, -1 = 无 */
    int queue_size;         /* 传输队列深度 */
} hal_spi_device_config_t;

struct hal_spi_bus
{
    int (*init)(hal_spi_bus_t* bus, const hal_spi_bus_config_t* bus_cfg,
                const hal_spi_device_config_t* dev_cfg);
    int (*write)(hal_spi_bus_t* bus, const uint8_t* data, size_t len);
    int (*write_top_half)(hal_spi_bus_t* bus, const uint8_t* data, size_t len);
    int (*read)(hal_spi_bus_t* bus, uint8_t* data, size_t len);
    int (*deinit)(hal_spi_bus_t* bus);
    void* _impl;
};

void hal_spi_bus_init_struct(hal_spi_bus_t* bus);

/* 总线级互斥锁 (防止多设备共线时序踩踏) */
int hal_spi_lock_bus(int bus_id, uint32_t timeout_ms);
int hal_spi_unlock_bus(int bus_id);

/* 片选控制 (由 HAL 接管, 确保多设备分时访问) */
int hal_spi_assert_cs(int bus_id, int cs_line);
int hal_spi_deassert_cs(int bus_id, int cs_line);

/* 安全停机: 复位所有 SPI 外设 (含 DMA 引擎) */
void hal_spi_force_stop(void);

/* 从 device_t 获取 SPI 总线实例 */
hal_spi_bus_t* device_get_spi_bus(device_t* dev);

/* ioctl 兼容层 */
#define SPI_CMD_DEINIT      0x40
#define SPI_CMD_READ        0x41

typedef struct
{
    uint8_t* data;
    size_t len;
} spi_read_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_SPI_BUS_H */
