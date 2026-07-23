/**
 * @license: SPDX-License-Identifier: Apache-2.0
 * @file: hal_can.h
 * @brief: CAN HAL 层 — 硬件抽象接口,硬件直投层
 * @note 所有接口设计为平台无关，由具体芯片平台(如 STM32, ESP32, CH307)进行底层硬实现。
 * @note SocketCAN 风格 can_frame；DTSI 提供厂商宏值, HAL 零翻译透传给底层驱动
 * @note F4 bxCAN 无 DMA；厂商句柄不透明嵌入 hcan_storage, 仅在平台 .c 内解释
 * @note 文件约定：返回值不允许void，必须使用int，并且错误码必须使用 status.h (VFS_ERR_*)
 * @note 接收的参数必须为指针，并且必须为合法的指针，不能为空指针
 * @note 禁止使用enum,enum的问题dts已经解决没必要在hal层重复定义去映射enum不直观而且麻烦还容易出错
 */
#ifndef HAL_CAN_H
#define HAL_CAN_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "compiler_compat.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HAL_CAN_HOST_MAX
#define HAL_CAN_HOST_MAX  2 /**< 最大 CAN host 数量 (由平台 cap/DTSI 约束) */
#endif

#ifndef HAL_CAN_FILTER_MAX
#define HAL_CAN_FILTER_MAX 28U /**< 过滤器 bank 上限 (平台相关, DTSI/实现侧校验) */
#endif

/**
 * @brief 厂商 CAN 句柄的不透明存储尺寸（字节）
 * @note 平台 .c 内 _Static_assert 校验真实 sizeof；预留余量供不同厂商句柄
 */
#ifndef HAL_CAN_HCAN_STORAGE_SIZE
#define HAL_CAN_HCAN_STORAGE_SIZE 64U
#endif

/** SocketCAN 帧标志 / 掩码 */
#define CAN_EFF_FLAG 0x80000000U /**< 扩展帧标志 (29-bit ID) */
#define CAN_RTR_FLAG 0x40000000U /**< 远程帧标志 */
#define CAN_ERR_FLAG 0x20000000U /**< 错误帧标志 (本 HAL 发送路径拒绝) */
#define CAN_SFF_MASK 0x000007FFU /**< 标准帧 ID 掩码 (11-bit) */
#define CAN_EFF_MASK 0x1FFFFFFFU /**< 扩展帧 ID 掩码 (29-bit) */
#define CAN_MAX_DLEN 8U          /**< 经典 CAN 最大数据长度 */

/**
 * @brief SocketCAN 风格经典 CAN 帧
 * @note can_id 高位复用 EFF/RTR/ERR 标志；data 8 字节对齐便于 DMA/拷贝
 */
struct can_frame
{
    uint32_t can_id;                               /**< CAN ID + 标志位 (EFF/RTR/ERR) */
    uint8_t  can_dlc;                              /**< 数据长度 0..CAN_MAX_DLEN */
    uint8_t  __pad;                                /**< 对齐填充 */
    uint8_t  __res0;                               /**< 保留 */
    uint8_t  __res1;                               /**< 保留 */
    uint8_t  data[CAN_MAX_DLEN] COMPAT_ALIGNED(8); /**< 载荷 */
};

/** 过滤器模式 / 宽度 */
#define HAL_CAN_FILTER_MODE_MASK   0U /**< 掩码模式 (id + mask) */
#define HAL_CAN_FILTER_MODE_LIST   1U /**< 列表模式 (精确 ID) */
#define HAL_CAN_FILTER_SCALE_16BIT 0U /**< 16-bit 过滤器宽度 */
#define HAL_CAN_FILTER_SCALE_32BIT 1U /**< 32-bit 过滤器宽度 */

/** 控制器状态 */
#define HAL_CAN_STATE_ERROR_ACTIVE  0U /**< error-active */
#define HAL_CAN_STATE_ERROR_PASSIVE 1U /**< error-passive */
#define HAL_CAN_STATE_BUS_OFF       2U /**< bus-off */
#define HAL_CAN_STATE_STOPPED       3U /**< 已停止 / 未启动 */

struct hal_can_dev;

/**
 * @brief CAN TX/RX 引脚配置 (硬件直投)
 * @note 纯数据实体: 字段由 DTSI 提供厂商宏值, HAL 零计算灌入 GPIO/AF 驱动
 */
struct hal_can_pin_cfg
{
    uintptr_t port;        /**< GPIOx_BASE */
    uint16_t  pin;         /**< GPIO_PIN_x */
    uint32_t  clk_bus;     /**< LL_AHBx_GRPy_PERIPH_GPIOx */
    uint32_t  af;          /**< GPIO_AFx_CANy */
    uint32_t  output_type; /**< LL_GPIO_OUTPUT_* */
    uint32_t  speed;       /**< LL_GPIO_SPEED_* */
    uint32_t  mode;        /**< LL_GPIO_MODE_* */
    uint32_t  pull;        /**< LL_GPIO_PULL_* */
};

/**
 * @brief CAN 总线配置 (host 级, DTSI 直投)
 * @note 位时序/模式等字段为厂商宏或原始寄存器语义, 由平台 .c 解释
 *       F4 bxCAN 无 DMA；收发走 it_enable + NVIC/轮询
 */
struct hal_can_bus_config
{
    uintptr_t              can;             /**< CANx_BASE */
    uint32_t               can_clk_periph;  /**< LL_APBx_GRPy_PERIPH_CANx */
    uint32_t               prescaler;       /**< 位时序分频 */
    uint32_t               mode;            /**< 工作模式 (normal/loopback/silent 等) */
    uint32_t               sjw;             /**< 同步跳转宽度 */
    uint32_t               bs1;             /**< 时间段 1 */
    uint32_t               bs2;             /**< 时间段 2 */
    uint32_t               auto_bus_off;    /**< 自动 bus-off 恢复: 0/1 */
    uint32_t               auto_wakeup;     /**< 自动唤醒: 0/1 */
    uint32_t               auto_retransmit; /**< 自动重传: 0/1 */
    uint32_t               rx_fifo_locked;  /**< RX FIFO 锁定: 0/1 */
    uint32_t               tx_fifo_prio;    /**< TX 优先级按请求顺序: 0/1 */
    uint32_t               tt_mode;         /**< 时间触发模式: 0/1 */
    int32_t                irqn;            /**< NVIC 中断号; -1=无中断 */
    uint32_t               irq_priority;    /**< NVIC 抢占优先级 */
    uint32_t               it_enable;       /**< 中断使能: 0=禁用, 1=启用 */
    struct hal_can_pin_cfg tx;              /**< TX 引脚配置 */
    struct hal_can_pin_cfg rx;              /**< RX 引脚配置 */
};

/**
 * @brief CAN 硬件过滤器配置
 */
struct hal_can_filter_config
{
    uint32_t bank;  /**< 过滤器 bank 编号 */
    uint32_t mode;  /**< HAL_CAN_FILTER_MODE_* */
    uint32_t scale; /**< HAL_CAN_FILTER_SCALE_* */
    uint32_t fifo;  /**< 分配到的 RX FIFO (0 / 1) */
    uint32_t id;    /**< 过滤器 ID (或列表项) */
    uint32_t mask;  /**< 掩码 (list 模式时语义由平台决定) */
    uint32_t ide;   /**< IDE 匹配相关位 (平台宏/位域) */
    uint32_t rtr;   /**< RTR 匹配相关位 (平台宏/位域) */
};

/**
 * @brief 厂商句柄不透明块
 * @note 仅平台 .c 内按厂商句柄类型解释; 头文件保持中立
 */
struct hal_can_hcan_blob
{
    uint8_t bytes[HAL_CAN_HCAN_STORAGE_SIZE];
};

/**
 * @brief CAN host 运行时状态 (bus 层持有)
 * @note 由 bus 层嵌入/持有; HAL 无池管理、无 vtable
 */
struct hal_can_bus_host
{
    struct hal_can_bus_config cfg;                            /**< 总线配置 (DTSI 直投) */
    struct hal_can_hcan_blob  hcan_storage COMPAT_ALIGNED(8); /**< 厂商句柄存储 */
    uintptr_t                 can;                            /**< 缓存 cfg.can, fast path */
    int                       hw_idx;                         /**< host 池下标 */
    int                       ref_count;                      /**< 引用计数 */
    bool                      bus_ready;                      /**< 总线就绪可收发 */
    bool                      hw_inited;                      /**< 硬件已初始化 */
};

/**
 * @brief CAN client 设备对象 (bus 层持有)
 */
struct hal_can_dev
{
    struct hal_can_bus_host* ctlr;    /**< 所属 host */
    int                      hw_open; /**< 硬件打开计数 */
};

int hal_can_bus_host_init(struct hal_can_bus_host* host, int hw_idx, const struct hal_can_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
int hal_can_bus_host_deinit(struct hal_can_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
int hal_can_dev_hw_open(struct hal_can_dev* dev) COMPAT_WARN_UNUSED_RESULT;
int hal_can_dev_hw_close(struct hal_can_dev* dev) COMPAT_WARN_UNUSED_RESULT;
int hal_can_dev_init(struct hal_can_dev* dev, struct hal_can_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
int hal_can_dev_deinit(struct hal_can_dev* dev) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 发送一帧 (经典 CAN)
 * @note 拒绝 CAN_ERR_FLAG；等待空闲邮箱超时返回 VFS_ERR_TIMEOUT
 */
int hal_can_transmit(struct hal_can_dev* dev, const struct can_frame* frame, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 从指定 FIFO 接收一帧
 * @param fifo RX FIFO 编号 (0 / 1)
 */
int hal_can_receive(struct hal_can_dev* dev, struct can_frame* frame, uint32_t fifo, uint32_t timeout_ms) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 配置硬件过滤器 (作用于 host/控制器)
 */
int hal_can_filter_config(struct hal_can_bus_host* host, const struct hal_can_filter_config* filter) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 查询控制器状态
 * @param out_state 输出 HAL_CAN_STATE_*
 */
int hal_can_get_state(struct hal_can_bus_host* host, uint32_t* out_state) COMPAT_WARN_UNUSED_RESULT;

/**
 * @brief 虚拟/平台 CAN 中断入口 (由中断框架回调)
 */
int hal_virtual_can_irq_callback(void* arg, uint16_t irq_num);

#ifdef __cplusplus
}
#endif

#endif /* HAL_CAN_H */
