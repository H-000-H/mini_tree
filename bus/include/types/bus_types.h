/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Bus Framework — 总线类型与配置定义
 *
 * 由旧的 hal_bus/bus/bus.h 迁移而来，保持兼容性。
 */
#ifndef BUS_TYPES_H
#define BUS_TYPES_H

#include "compiler_compat.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    BUS_TYPE_SPI   = ((0x01) << 0),
    BUS_TYPE_IIC   = ((0x01) << 1),
    BUS_TYPE_CAN   = ((0x01) << 2),
    BUS_TYPE_CANFD = ((0x01) << 3),
    BUS_TYPE_USB   = ((0x01) << 4),
    BUS_TYPE_I2S   = ((0X01) << 5),
    BUS_TYPE_PCIE  = ((0X01) << 6),
} bus_type_t;

typedef enum {
    I2C_ADDR_7BIT  = 0,
    I2C_ADDR_10BIT = 1
} i2c_addr_type_t;

typedef struct {
    uint16_t        dev_addr;
    i2c_addr_type_t addr_type;
    uint8_t         duty_cycle;
    uint8_t         general_call;
    uint32_t        timeout_ms;
} i2c_bus_config_t;

typedef enum {
    SPI_MODE_0 = 0,
    SPI_MODE_1 = 1,
    SPI_MODE_2 = 2,
    SPI_MODE_3 = 3
} spi_mode_t;

typedef enum {
    SPI_DIR_FULL_DUPLEX = 0,
    SPI_DIR_HALF_DUPLEX = 1,
    SPI_DIR_SIMPLEX_RX  = 2
} spi_dir_t;

typedef struct {
    spi_mode_t mode;
    spi_dir_t  direction;
    uint8_t    data_size;
    uint8_t    first_bit;
    uint8_t    nss_mode;
    uint8_t    wire_mode;
} bus_spi_spec_t;

typedef enum {
    I2S_STD_PHILIPS       = 0,
    I2S_STD_MSB_JUSTIFIED = 1,
    I2S_STD_LSB_JUSTIFIED = 2,
    I2S_STD_PCM           = 3
} i2s_std_t;

typedef enum {
    I2S_DATAFORMAT_16B          = 0,
    I2S_DATAFORMAT_16B_EXTENDED = 1,
    I2S_DATAFORMAT_24B          = 2,
    I2S_DATAFORMAT_32B          = 3
} i2s_format_t;

typedef struct {
    i2s_std_t    audio_std;
    i2s_format_t data_format;
    uint32_t     sample_rate;
    uint8_t      mode;
    uint8_t      mclk_output;
} i2s_bus_config_t;

typedef enum {
    USB_SPEED_LOW   = 0,
    USB_SPEED_FULL  = 1,
    USB_SPEED_HIGH  = 2,
    USB_SPEED_SUPER = 3
} usb_speed_t;

typedef enum {
    USB_MODE_DEVICE = 0,
    USB_MODE_HOST   = 1,
    USB_MODE_OTG    = 2
} usb_mode_t;

typedef struct {
    usb_mode_t  mode;
    usb_speed_t speed;
    uint8_t     dev_class;
    uint8_t     max_packet_size;
    uint16_t    vid;
    uint16_t    pid;
} usb_bus_config_t;

typedef struct {
    uint16_t prescaler;
    uint8_t  time_seg1;
    uint8_t  time_seg2;
    uint8_t  sjw;
} can_fd_timing_t;

typedef struct {
    uint32_t filter_id;
    uint32_t filter_mask;
    uint8_t  filter_bank;
    uint8_t  enable;
} can_bus_fd_filter_cfg_t;

typedef struct {
    can_fd_timing_t nominal_timing;
    can_fd_timing_t data_timing;
    uint8_t         mode;
    bool            fd_iso_mode;
    bool            tdc_enable;
    uint8_t         tdc_offset;
    can_bus_fd_filter_cfg_t *filters;
    size_t          filter_num;
} can_bus_fd_config_t;

typedef struct {
    uint32_t filter_id;
    uint32_t filter_mask;
    uint16_t brp_sjw_seg;
} can_bus_config_t;

typedef enum
{
    PCIE_GEN_1 = 1,
    PCIE_GEN_2 = 2,
    PCIE_GEN_3 = 3,
    PCIE_GEN_4 = 4
} pcie_gen_t;

typedef struct
{
    pcie_gen_t gen;
    uint8_t    lanes;
    uint8_t    mode;
    uint32_t   bar_size[6];
    uint16_t   vendor_id;
    uint16_t   device_id;
} pcie_config_cfg_t;

typedef struct
{
    uint32_t clock_speed_hz;

    union {
        bus_spi_spec_t      spi;
        i2c_bus_config_t    i2c;
        i2s_bus_config_t    i2s;
        can_bus_fd_config_t can_fd;
        can_bus_config_t    can;
        usb_bus_config_t    usb;
        pcie_config_cfg_t   pcie;
    } spec;
} bus_config_t;

#endif /* BUS_TYPES_H */
