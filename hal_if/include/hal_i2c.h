#ifndef HAL_I2C_H
#define HAL_I2C_H

#include <stdint.h>
#include <stddef.h>

struct device_instance;
typedef struct device_instance device_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hal_i2c_bus hal_i2c_bus_t;

typedef struct
{
    int sda_pin;
    int scl_pin;
    uint32_t clock_hz;
    int port;
} hal_i2c_config_t;

struct hal_i2c_bus
{
    int (*init)(hal_i2c_bus_t* bus, const hal_i2c_config_t* cfg);
    int (*write)(hal_i2c_bus_t* bus, uint8_t addr, const uint8_t* data, size_t len, uint32_t time_out);
    int (*read)(hal_i2c_bus_t* bus, uint8_t addr, uint8_t* data, size_t len, uint32_t time_out);
    int (*write_read)(hal_i2c_bus_t* bus, uint8_t addr,
                      const uint8_t* wdata, size_t wlen,
                      uint8_t* rdata, size_t rlen, uint32_t time_out);
    int (*bus_recover)(hal_i2c_bus_t* bus);
    int (*deinit)(hal_i2c_bus_t* bus);
    void* _impl;
};

void hal_i2c_init_struct(hal_i2c_bus_t* bus);

/* ── 总线级互斥锁 (防止多设备共线时序踩踏) ── */
int hal_i2c_lock_bus(int port, uint32_t timeout_ms);
int hal_i2c_unlock_bus(int port);

/* ── 强类型 I2C 总线访问 (替代 ioctl, MISRA C Rule 11.3 合规) ──
 * 从已探测的 I2C device_t 中提取 hal_i2c_bus_t*, 直接调用结构体方法.
 * 返回 NULL 如果设备不是 I2C 总线或未完成探测.
 *
 * 使用方式:
 *   hal_i2c_bus_t* bus = device_get_i2c_bus(dev);
 *   if (bus) bus->write(bus, addr, data, len, timeout);
 */
hal_i2c_bus_t* device_get_i2c_bus(device_t* dev);

/* ── 以下为 ioctl 兼容层 (已弃用, 新代码请使用上述强类型 API) ── */

#define I2C_CMD_INIT        0x20
#define I2C_CMD_WRITE       0x21
#define I2C_CMD_READ        0x22
#define I2C_CMD_WRITE_READ  0x23
#define I2C_CMD_DEINIT      0x24

typedef struct
{
    uint8_t addr;
    uint8_t* data;
    size_t len;
    uint32_t timeout;
} i2c_rw_arg_t;

typedef struct
{
    uint8_t addr;
    const uint8_t* wdata;
    size_t wlen;
    uint8_t* rdata;
    size_t rlen;
    uint32_t timeout;
} i2c_wr_arg_t;

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */
