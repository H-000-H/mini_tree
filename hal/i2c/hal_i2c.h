/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file hal_i2c.h
 *@brief hal i2c 头文件
 *@author H-000-H
 *@details
 *   用法与实现: I2C 接口平台无关, 由具体芯片 hal.c 硬实现; 约定: 返回值用 int + VFS 错误码,
 *   接收参数须为合法非空指针, 禁止 enum (dts 已解决映射)。
 */

#ifndef HAL_I2C_H
#define HAL_I2C_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef HAL_I2C_HOST_MAX
#define HAL_I2C_HOST_MAX 4 /**< 最大 host 数量 */
#endif
#ifndef HAL_I2C_MAX_XFER
#define HAL_I2C_MAX_XFER 512U /**< 单次传输最大字节数 (调用方缓冲上限校验) */
#endif

#define HAL_I2C_BUS_ROLE_SLAVE 0 /**< 从机角色 */
#define HAL_I2C_BUS_ROLE_MASTER 1 /**< 主机角色 */

/** 同步传输路径选择 (0=隐式, 兼容零初始化) */
#define HAL_I2C_XFER_AUTO 0U /**< 隐式: DMA 可用则 DMA, 否则 poll */
#define HAL_I2C_XFER_POLL 1U /**< 强制 CPU poll */
#define HAL_I2C_XFER_DMA 2U /**< 强制 DMA, 不可用则返回 NOTSUPP */

    struct hal_i2c_dev;

    /**
     * @brief I2C DMA 配置 (DTSI 直投)
     */
    struct hal_i2c_dma_config
    {
        uint32_t dma_enable; /**< 0=不使用 DMA, 1=使用 DMA */
        uintptr_t dma_handle; /**< DMA 控制器寄存器基址 (DMA1_BASE / DMA2_BASE) */
        uint32_t dma_stream; /**< DMA 流编号 (LL_DMA_STREAM_n) */
        uint32_t dma_channel; /**< DMA 通道编号 (LL_DMA_CHANNEL_n) */
        uint32_t dma_priority; /**< DMA 优先级 (LL_DMA_PRIORITY_*) */
        uint32_t dma_memory_size; /**< DMA 内存数据宽度 (LL_DMA_MDATAALIGN_*) */
        uint32_t dma_mode; /**< DMA 模式 (LL_DMA_MODE_NORMAL/CIRCULAR) */
        uint32_t dma_periph_inc; /**< DMA 外设地址递增 */
        uint32_t dma_mem_inc; /**< DMA 内存地址递增 */
        uint32_t dma_periph_data_size; /**< DMA 外设数据宽度 (LL_DMA_PDATAALIGN_*) */
        uint32_t dma_fifo_mode; /**< DMA FIFO 模式 */
        uint32_t dma_fifo_threshold; /**< DMA FIFO 阈值 */
        uint32_t dma_mem_burst; /**< DMA 内存突发 */
        uint32_t dma_periph_burst; /**< DMA 外设突发 */
    };

    /** @brief I2C 引脚配置 (SCL/SDA/Alert 共用) */
    struct hal_i2c_pin_cfg
    {
        uintptr_t port; /**< GPIOx_BASE */
        uint16_t pin; /**< GPIO_PIN_x */
        uint32_t clk_bus; /**< LL_AHBx_GRPy_PERIPH_GPIOx */
        uint32_t af; /**< GPIO_AFx_I2Cy */
        uint32_t output_type; /**< LL_GPIO_OUTPUT_* */
        uint32_t speed; /**< LL_GPIO_SPEED_* */
        uint32_t mode; /**< LL_GPIO_MODE_* */
        uint32_t pull; /**< LL_GPIO_PULL_* */
    };

    /** @brief I2C 总线配置 (host 级, DTSI 直投) */
    struct hal_i2c_bus_config
    {
        uintptr_t i2c; /**< I2Cx_BASE */
        uint32_t i2c_clk_periph; /**< LL_APBx_GRPy_PERIPH_I2Cx */
        uint32_t mode; /**< LL_I2C_MODE_* */
        uint32_t bus_role; /**< HAL_I2C_BUS_ROLE_MASTER / SLAVE */
        uint32_t smbus_timeout_enable; /**< SMBus 超时使能 */
        uint32_t smbus_alert_enable; /**< SMBus Alert 使能 */
        struct hal_i2c_pin_cfg smbus_alert; /**< SMBus Alert 引脚 */
        int32_t irqn; /**< NVIC 中断号; -1=无中断 */
        uint32_t irq_priority; /**< NVIC 抢占优先级 */
        uint32_t it_enable; /**< 中断使能: 0=禁用, 1=启用 */
        size_t max_transfer_sz; /**< 单次最大传输字节数; 0=用 HAL_I2C_MAX_XFER */
        struct hal_i2c_pin_cfg scl; /**< SCL 引脚配置 */
        struct hal_i2c_pin_cfg sda; /**< SDA 引脚配置 */
        struct hal_i2c_dma_config dma_tx; /**< TX DMA 配置 */
        struct hal_i2c_dma_config dma_rx; /**< RX DMA 配置 */
    };

    /** @brief I2C 设备配置 (client 级, DTSI 直投) */
    struct hal_i2c_device_config
    {
        uint32_t clock_speed_hz; /**< I2C 时钟频率 (Hz, 如 100000/400000) */
        uint32_t ack_enable; /**< ACK 使能 */
        uint32_t address; /**< 7-bit 从机地址 (右对齐, 如 0x50; 传输时内部左移) */
        uint32_t addr_width; /**< 0=7bit (唯一已实现); 1=10bit 返回 NOTSUPP */
        uint32_t data_width; /**< 数据位宽 */
        uint32_t duty_cycle; /**< 快速模式占空比 */
        uint32_t own_address; /**< 自身地址 (从机模式) */
        uint32_t pec_enable; /**< PEC 校验使能 */
    };

    /** @brief I2C host 运行时状态 (bus 层持有) */
    struct hal_i2c_bus_host
    {
        struct hal_i2c_bus_config cfg; /**< 总线配置 (DTSI 直投) */
        struct hal_i2c_device_config active_cfg; /**< 当前 active 设备配置 */
        uintptr_t i2c; /**< 缓存 cfg.i2c, fast path */
        int hw_idx; /**< host 池下标 */
        int ref_count; /**< 引用计数 */
        bool bus_ready; /**< 总线就绪 */
        bool hw_inited; /**< 硬件已初始化 */
    };

    /** @brief I2C client 设备对象 (bus 层持有) */
    struct hal_i2c_dev
    {
        struct hal_i2c_bus_host* ctlr; /**< 所属 host */
        struct hal_i2c_device_config cfg; /**< 设备配置 */
        int hw_open; /**< 硬件打开计数 */
    };

    /**
     * @brief 初始化 I2C 总线主机
     * @param[in] host 总线主机对象指针
     * @param[in] hw_idx dummy buffer / HW slot 索引
     * @param[in] cfg 总线配置 (DTSI 直投, 生命周期由调用方持有)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int hal_i2c_bus_host_init(struct hal_i2c_bus_host* host, int hw_idx, const struct hal_i2c_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 反初始化 I2C 总线主机, 释放硬件资源
     * @param[in] host 总线主机对象指针
     * @return 成功返回 MINI_OK, host 为空返回 MINI_ERR_INVAL
     */
    int hal_i2c_bus_host_deinit(struct hal_i2c_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 打开 I2C 设备硬件 (引用计数 +1, 首次触发底层 init)
     * @param[in] pdev I2C 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_i2c_dev_hw_open(struct hal_i2c_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭 I2C 设备硬件 (引用计数 -1, 归零触发底层 deinit)
     * @param[in] pdev I2C 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_i2c_dev_hw_close(struct hal_i2c_dev* pdev) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 绑定设备与主机并应用 client 级配置
     * @param[in] pdev I2C 设备对象指针
     * @param[in] host 所属总线主机指针
     * @param[in] dev_cfg client 级设备配置 (DTSI 直投)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_*
     */
    int hal_i2c_dev_init(struct hal_i2c_dev* pdev, struct hal_i2c_bus_host* host, const struct hal_i2c_device_config* dev_cfg) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 解绑设备并复位 client 状态
     * @param[in] pdev I2C 设备对象指针
     * @return 成功返回 MINI_OK, pdev 为空返回 MINI_ERR_INVAL
     */
    int hal_i2c_dev_deinit(struct hal_i2c_dev* pdev) COMPAT_WARN_UNUSED_RESULT;

    /**
     * @brief 主机同步传输 (组合: 先写后读 / 或分派到 write/read)
     * @param[in] pdev I2C 设备对象指针
     * @param[in] tx 发送缓冲区 (纯读时可空)
     * @param[out] rx 接收缓冲区 (纯写时可空)
     * @param[in] len 传输字节数 (tx/rx 长度, 先写后读时二者相等)
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @note  tx 非空 rx 空 → 纯写; tx 空 rx 非空 → 纯读;
     *        两者都非空 → 先写后读 (Repeated START), 长度均为 len
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
     */
    int hal_i2c_sync(struct hal_i2c_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 主机同步写 (直接走 master write, 不经 sync 绕路)
     * @param[in] pdev I2C 设备对象指针
     * @param[in] tx 发送缓冲区
     * @param[in] len 发送字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
     */
    int hal_i2c_write(struct hal_i2c_dev* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 主机同步读 (直接走 master read, 不经 sync 绕路)
     * @param[in] pdev I2C 设备对象指针
     * @param[out] rx 接收缓冲区
     * @param[in] len 接收字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, 失败返回 VFS_ERR_* (超时 MINI_ERR_TIMEOUT)
     */
    int hal_i2c_read(struct hal_i2c_dev* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 主机 DMA 异步写 (无 OS 等待, 依赖 DMA TC 中断)
     * @param[in] pdev I2C 设备对象指针
     * @param[in] tx 发送缓冲区 (DMA 期间须保持有效)
     * @param[in] len 发送字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, DMA 不可用返回 MINI_ERR_NOTSUPP, 失败返回 VFS_ERR_*
     */
    int hal_i2c_dma_write(struct hal_i2c_dev* pdev, const uint8_t* tx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief 主机 DMA 异步读 (无 OS 等待, 依赖 DMA TC 中断)
     * @param[in] pdev I2C 设备对象指针
     * @param[out] rx 接收缓冲区 (DMA 写入目标)
     * @param[in] len 接收字节数
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, DMA 不可用返回 MINI_ERR_NOTSUPP, 失败返回 VFS_ERR_*
     */
    int hal_i2c_dma_read(struct hal_i2c_dev* pdev, uint8_t* rx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;
    /**
     * @brief DMA 先写后读 (中间 Repeated START, 无 STOP)
     * @param[in] pdev I2C 设备对象指针
     * @param[in] tx 发送缓冲区 (DMA 期间须保持有效)
     * @param[out] rx 接收缓冲区 (DMA 写入目标)
     * @param[in] len 传输字节数 (tx/rx 长度, 二者相等)
     * @param[in] timeout_ms 超时毫秒数 (0=不等待)
     * @return 成功返回 MINI_OK, DMA 不可用返回 MINI_ERR_NOTSUPP, 失败返回 VFS_ERR_*
     */
    int hal_i2c_dma_write_then_read(struct hal_i2c_dev* pdev, const uint8_t* tx, uint8_t* rx, size_t len, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* HAL_I2C_H */
