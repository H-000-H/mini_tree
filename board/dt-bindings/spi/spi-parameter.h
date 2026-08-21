/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file spi-parameter.h
 *@brief spi-parameter 头文件
 *@author H-000-H
 *@details
 *   SPI 板级默认参数 (dt-bindings)
 *   只放板级策略常量；spi-base 用 <hal/spi_types.h> 的 SPI2_HOST / SPI3_HOST。
 *   勿加 #ifndef guard（会破坏 dtc-lite 宏展开）。
 */

#define SPI_DEFAULT_MAX_FREQUENCY_HZ 40000000
#define SPI_DEFAULT_MODE 0
#define SPI_DEFAULT_BITS_PER_WORD 8
#define SPI_DEFAULT_QUEUE_SIZE 8
#define SPI_DEFAULT_DMA_ENABLE 1
