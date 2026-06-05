/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Minimal dt-bindings for rk3288.dtsi compatibility with dtc-lite.
 *
 * 仅包含 rk3288.dtsi 中实际引用的 clock/reset ID 宏.
 * 值来自 Linux 内核 v6.x include/dt-bindings/clock/rk3288-cru.h.
 *
 * dtc-lite 在预处理阶段展开这些宏, 生成 .c 时保留 int 值供驱动使用.
 */
#ifndef _DT_BINDINGS_CLOCK_RK3288_CRU_H
#define _DT_BINDINGS_CLOCK_RK3288_CRU_H

/* PLL */
#define PLL_GPLL    1
#define PLL_CPLL    2
#define PLL_NPLL    3

/* CPU & BUS */
#define ARMCLK      4
#define ACLK_CPU    5
#define HCLK_CPU    6
#define PCLK_CPU    7
#define ACLK_PERI   8
#define HCLK_PERI   9
#define PCLK_PERI   10

/* DMAC */
#define ACLK_DMAC1  11
#define ACLK_DMAC2  12

/* SD/MMC */
#define HCLK_SDMMC  13
#define SCLK_SDMMC  14
#define HCLK_SDIO0  15
#define SCLK_SDIO0  16
#define HCLK_SDIO1  17
#define SCLK_SDIO1  18
#define HCLK_EMMC   19
#define SCLK_EMMC   20

/* SARADC */
#define SCLK_SARADC 21
#define PCLK_SARADC 22

/* SPI */
#define SCLK_SPI0   23
#define PCLK_SPI0   24
#define SCLK_SPI1   25
#define PCLK_SPI1   26
#define SCLK_SPI2   27
#define PCLK_SPI2   28

/* I2C */
#define PCLK_I2C0   29
#define PCLK_I2C1   30
#define PCLK_I2C2   31
#define PCLK_I2C3   32
#define PCLK_I2C4   33
#define PCLK_I2C5   34

/* UART */
#define SCLK_UART0  35
#define PCLK_UART0  36
#define SCLK_UART1  37
#define PCLK_UART1  38
#define SCLK_UART2  39
#define PCLK_UART2  40
#define SCLK_UART3  41
#define PCLK_UART3  42
#define SCLK_UART4  43
#define PCLK_UART4  44

/* TSADC */
#define SCLK_TSADC  45
#define PCLK_TSADC  46

/* GMAC Ethernet */
#define SCLK_MAC        47
#define SCLK_MAC_RX     48
#define SCLK_MAC_TX     49
#define SCLK_MACREF     50
#define SCLK_MACREF_OUT 51
#define ACLK_GMAC       52
#define PCLK_GMAC       53

/* USB */
#define HCLK_USBHOST0   54
#define HCLK_USBHOST1   55
#define HCLK_OTG0       56
#define HCLK_HSIC       57

/* PWM */
#define PCLK_PWM    58

/* Timer */
#define PCLK_TIMER  59

/* Watchdog */
#define PCLK_WDT    60

/* I2S */
#define HCLK_I2S0   61
#define SCLK_I2S0   62

/* VOP (Video Output Processor) */
#define ACLK_VOP0   63
#define DCLK_VOP0   64
#define HCLK_VOP0   65
#define ACLK_VOP1   66
#define DCLK_VOP1   67
#define HCLK_VOP1   68

/* HDMI */
#define PCLK_HDMI_CTRL  69
#define SCLK_HDMI_HDCP  70

/* USB PHY */
#define SCLK_OTGPHY0 71
#define SCLK_OTGPHY1 72
#define SCLK_OTGPHY2 73

/* GPIO */
#define PCLK_GPIO0  74
#define PCLK_GPIO1  75
#define PCLK_GPIO2  76
#define PCLK_GPIO3  77
#define PCLK_GPIO4  78
#define PCLK_GPIO5  79
#define PCLK_GPIO6  80
#define PCLK_GPIO7  81
#define PCLK_GPIO8  82

/* Software Resets */
#define SRST_CORE0      83
#define SRST_CORE1      84
#define SRST_CORE2      85
#define SRST_CORE3      86
#define SRST_TSADC      87
#define SRST_LCDC0_AXI  88
#define SRST_LCDC0_AHB  89
#define SRST_LCDC0_DCLK 90
#define SRST_LCDC1_AXI  91
#define SRST_LCDC1_AHB  92
#define SRST_LCDC1_DCLK 93

#endif
