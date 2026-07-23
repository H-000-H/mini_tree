/**
 * @license: SPDX-License-Identifier: Apache-2.0
 * @file: hal_usb.h
 * @brief: USB HAL — OTG 板级基建与传输路径选择
 * @note 本层只做 RCC / GPIO AF / NVIC; 协议与端点由 TinyUSB DWC2 DCD 负责。
 * @note 不含 Cube PCD/USBD。OTG 使用控制器内建 DMA (DWC2), 无 DMA1/DMA2 stream。
 * @note DTSI 属性直投; dma-enable + xfer_mode 运行时决定 AUTO/POLL/DMA。
 */
#ifndef HAL_USB_H
#define HAL_USB_H

#include <stdint.h>
#include <stdbool.h>
#include "compiler_compat.h"
#include "status.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*===========================================================================================================================================================*/
                                                              /* 传输路径 */
/*===========================================================================================================================================================*/
#define HAL_USB_XFER_AUTO 0U /**< 隐式: dma_enable 非 0 则 DMA, 否则 poll */
#define HAL_USB_XFER_POLL 1U /**< 强制 CPU↔DFIFO (Slave/FIFO) */
#define HAL_USB_XFER_DMA  2U /**< 强制 OTG 内建 DMA; 未使能则返回 NOTSUPP */

/**
 * @brief USB DMA 配置 (OTG 内建 DWC2)
 * @note 无 stream/channel; DTS 仅提供 dma-enable
 */
struct hal_usb_dma_config
{
    uint32_t dma_enable; /**< 0=禁用内建 DMA, 1=允许 DMA 路径 */
};

/**
 * @brief USB DP/DM 引脚 (HAL GPIO 宏直投)
 */
struct hal_usb_pin_cfg
{
    uintptr_t port; /**< GPIOx_BASE */
    uint32_t  pin;  /**< GPIO_PIN_x */
    uint32_t  af;   /**< GPIO_AFx_OTG_FS / HS */
};

/**
 * @brief USB host 总线配置 (DTSI 直投)
 */
struct hal_usb_bus_config
{
    uintptr_t                 usb_base;   /**< USB_OTG_FS_PERIPH_BASE 等 */
    int                       rhport;     /**< TinyUSB rhport, OTG_FS 一般为 0 */
    int                       irqn;       /**< OTG_FS_IRQn 等 */
    struct hal_usb_pin_cfg    dp;         /**< D+ */
    struct hal_usb_pin_cfg    dm;         /**< D- */
    int                       vbus_sense; /**< 0=不做 VBUS sense */
    struct hal_usb_dma_config dma_cfg;    /**< 内建 DMA 开关 */
};

/**
 * @brief USB host 运行时对象 (由 bus 层嵌入持有)
 */
struct hal_usb_bus_host
{
    struct hal_usb_bus_config cfg;    /**< 配置快照 */
    int                       inited; /**< 非 0 表示已 init */
};

/*===========================================================================================================================================================*/
                                                              /* Host 生命周期 */
/*===========================================================================================================================================================*/
/**
 * @brief 初始化 OTG 时钟、DP/DM AF、NVIC 优先级 (不启 Cube PCD)
 * @param host host 对象
 * @param cfg  DTSI 直投配置
 * @return VFS_OK 或 VFS_ERR_*
 */
int  hal_usb_bus_host_init(struct hal_usb_bus_host* host,
                           const struct hal_usb_bus_config* cfg) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 反初始化: 关 IRQ、关时钟、恢复 GPIO
 * @param host host 对象
 * @return VFS_OK 或 VFS_ERR_*
 */
int  hal_usb_bus_host_deinit(struct hal_usb_bus_host* host) COMPAT_WARN_UNUSED_RESULT;
/**
 * @brief 使能 OTG NVIC 中断
 * @param host host 对象
 */
void hal_usb_irq_enable(const struct hal_usb_bus_host* host);
/**
 * @brief 禁止 OTG NVIC 中断
 * @param host host 对象
 */
void hal_usb_irq_disable(const struct hal_usb_bus_host* host);

/**
 * @brief 按 dma_enable 与请求模式解析实际传输路径
 * @param host      已 init 的 host
 * @param xfer_mode HAL_USB_XFER_AUTO / POLL / DMA
 * @return HAL_USB_XFER_POLL / HAL_USB_XFER_DMA, 或负数 VFS_ERR_*
 */
int  hal_usb_resolve_xfer_mode(const struct hal_usb_bus_host* host, uint32_t xfer_mode)
    COMPAT_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#ifndef HAL_USB_IMPL
#pragma GCC poison hal_usb_bus_host_init hal_usb_bus_host_deinit
#pragma GCC poison hal_usb_irq_enable hal_usb_irq_disable
#pragma GCC poison hal_usb_resolve_xfer_mode
#endif

#endif /* HAL_USB_H */
