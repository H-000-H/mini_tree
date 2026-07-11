#ifndef HAL_I2C_H
#define HAL_I2C_H
/** 
 * @license: SPDX-License-Identifier: Apache-2.0 
 * @file: hal_i2c.h
 * @brief: I2C 层 — 硬件抽象接口,硬件直投层    
 * @note 所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note 文件约定：返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 返回值不允许void，必须使用int，并且错误码必须使用VFS.h中的错误码 
 * @note 接收的参数必须为指针，并且必须为合法的指针，不能为空指针
 * @note 禁止使用enum,enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */
#include "VFS.h"

/**
 * @brief I2C GPIO 配置结构体
 * @note 配置结构体用于配置I2C的GPIO引脚
*/
struct hal_i2c_gpio_cfg
{
    uintptr_t port;         /**< GPIO 端口基地址或端口号 (支持 32/64 位平台指针转换) */
    uint16_t  pin;          /**< 引脚编号 (如 GPIO_PIN_0) */
    uint32_t  clk_bus;      /**< 该引脚所属的外设时钟总线 */
    uint32_t  af;           /**< 引脚复用功能设置 (Alternate Function 选择) */
    uint32_t  output_type;  /**< 引脚输出类型 */
    uint32_t  speed;        /**< 引脚速度 */
    uint32_t  mode;         /**< 引脚模式 */
    uint32_t  pull;         /**< 引脚上拉/下拉 */
};


/**
 * @brief I2C 配置结构体
 * @note 配置结构体用于配置I2C的参数
*/
struct hal_i2c_config
{
    int  slk_speed;   /*< I2C时钟速度 */
    int  ack_enable;  /*< I2C应答使能 */
    int  address;     /*< I2C从机地址 */
    int  addr_width;  /*< I2C地址宽度 0:7位 1:10位 */
    int  data_width;  /*< I2C数据宽度 0:8位 1:16位 */
    uintptr_t i2c_base;    /*< I2C基地址 */
};

/**
 * @brief I2C DMA 配置结构体
 * @note 配置结构体用于配置I2C的DMA参数
*/
struct hal_i2c_dma_config
{
    int dma_enable;
    int dma_stream;
    int dma_channel;
    int dma_priority;
    int dma_memory_size;
};

/**
 * @brief I2C IRQ 配置结构体
 * @note 配置结构体用于配置I2C的IRQ参数
*/  
struct hal_i2c_irq_config
{
    int irq_enable;
    int irq_stream;
    int irq_channel;
    int irq_priority;
    int irq_memory_size;
};

/**
 * @brief I2C 主机配置结构体
 * @note 配置结构体用于配置I2C的主机参数
*/
struct hal_i2c_host_config
{
    struct hal_i2c_config       config;       /**< I2C 配置 */
    struct hal_i2c_dma_config   dma_config;   /**< DMA 配置 */
    struct hal_i2c_gpio_cfg     gpio_config;   /**< GPIO 配置 */
    struct hal_i2c_irq_config   irq_config;    /**< IRQ 配置 */
};

struct hal_i2c_unique
{
    uintptr_t private_cfg; /**< 平台私有运行时状态 */
};

struct hal_i2c_dev
{
    struct hal_i2c_host_config host_config;
    struct hal_i2c_unique      unique;
};
#endif