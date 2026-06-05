#include "board_nodes.h"
#include "board_devtable.h"
#include "device.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* ===== 属性表 (静态 .rodata) ===== */

/* / */
static const device_prop_t DEV__props[] = {
    {"#address-cells", "0x1"},
    {"#size-cells", "0x1"},
    {"interrupt-parent", "gic"},
    {"model", "rk3288-mcu-test"},
};

/* /cpus/cpu@500 */
static const device_prop_t DEV_cpu_500_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x500"},
    {"resets", "0x53"},
    {"operating-points", "0x188940 0x149970 0x171240 0x13d620 0x159b40 0x124f80 0x124f80 0x10c8e0 0xf6180 0x100590 0xc7380 0xf4240 0xa9ec0 0xe7ef0 0x927c0 0xdbba0 0x639c0 0xdbba0 0x4c2c0 0xdbba0 0x34bc0 0xdbba0 0x1ec30 0xdbba0"},
    {"#cooling-cells", "0x2"},
    {"clock-latency", "0x9c40"},
    {"clocks", "0x4"},
};

/* /cpus/cpu@501 */
static const device_prop_t DEV_cpu_501_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x501"},
    {"resets", "0x54"},
};

/* /cpus/cpu@502 */
static const device_prop_t DEV_cpu_502_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x502"},
    {"resets", "0x55"},
};

/* /cpus/cpu@503 */
static const device_prop_t DEV_cpu_503_props[] = {
    {"device_type", "cpu"},
    {"reg", "0x503"},
    {"resets", "0x56"},
};

/* /amba */
static const device_prop_t DEV_amba_props[] = {
    {"#address-cells", "0x1"},
    {"#size-cells", "0x1"},
    {"ranges", "true"},
};

/* /amba/dma-controller@ff250000 */
static const device_prop_t DEV_dma_controller_ff250000_props[] = {
    {"reg", "0xff250000 0x4000"},
    {"interrupts", "0x0 0x2 0x4 0x0 0x3 0x4"},
    {"#dma-cells", "0x1"},
    {"clocks", "0xc"},
    {"clock-names", "apb_pclk"},
};

/* /amba/dma-controller@ff600000 */
static const device_prop_t DEV_dma_controller_ff600000_props[] = {
    {"reg", "0xff600000 0x4000"},
    {"interrupts", "0x0 0x0 0x4 0x0 0x1 0x4"},
    {"#dma-cells", "0x1"},
    {"clocks", "0xb"},
    {"clock-names", "apb_pclk"},
};

/* /amba/dma-controller@ffb20000 */
static const device_prop_t DEV_dma_controller_ffb20000_props[] = {
    {"reg", "0xffb20000 0x4000"},
    {"interrupts", "0x0 0x0 0x4 0x0 0x1 0x4"},
    {"#dma-cells", "0x1"},
    {"clocks", "0xb"},
    {"clock-names", "apb_pclk"},
};

/* /oscillator */
static const device_prop_t DEV_oscillator_props[] = {
    {"clock-frequency", "0x16e3600"},
    {"clock-output-names", "xin24m"},
    {"#clock-cells", "0x0"},
};

/* /timer */
static const device_prop_t DEV_timer_props[] = {
    {"arm,cpu-registers-not-fw-configured", "true"},
    {"interrupts", "0x1 0xd 0x4 0x4 0x1 0xe 0x4 0x4 0x1 0xb 0x4 0x4 0x1 0xa 0x4 0x4"},
    {"clock-frequency", "0x16e3600"},
};

/* /timer@ff810000 */
static const device_prop_t DEV_timer_ff810000_props[] = {
    {"reg", "0xff810000 0x20"},
    {"interrupts", "0x0 0x48 0x4"},
    {"clocks", "0x3b"},
    {"clock-names", "timer"},
};

/* /display-subsystem */
static const device_prop_t DEV_display_subsystem_props[] = {
    {"ports", "vopl_out"},
};

/* /dwmmc@ff0c0000 */
static const device_prop_t DEV_dwmmc_ff0c0000_props[] = {
    {"clock-freq-min-max", "0x61a80 0x8f0d180"},
    {"clocks", "0xd 0xe"},
    {"clock-names", "biu"},
    {"fifo-depth", "0x100"},
    {"interrupts", "0x0 0x20 0x4"},
    {"reg", "0xff0c0000 0x4000"},
};

/* /dwmmc@ff0d0000 */
static const device_prop_t DEV_dwmmc_ff0d0000_props[] = {
    {"clock-freq-min-max", "0x61a80 0x8f0d180"},
    {"clocks", "0xf 0x10"},
    {"clock-names", "biu"},
    {"fifo-depth", "0x100"},
    {"interrupts", "0x0 0x21 0x4"},
    {"reg", "0xff0d0000 0x4000"},
};

/* /dwmmc@ff0e0000 */
static const device_prop_t DEV_dwmmc_ff0e0000_props[] = {
    {"clock-freq-min-max", "0x61a80 0x8f0d180"},
    {"clocks", "0x11 0x12"},
    {"clock-names", "biu"},
    {"fifo-depth", "0x100"},
    {"interrupts", "0x0 0x22 0x4"},
    {"reg", "0xff0e0000 0x4000"},
};

/* /dwmmc@ff0f0000 */
static const device_prop_t DEV_dwmmc_ff0f0000_props[] = {
    {"clock-freq-min-max", "0x61a80 0x8f0d180"},
    {"clocks", "0x13 0x14"},
    {"clock-names", "biu"},
    {"fifo-depth", "0x100"},
    {"interrupts", "0x0 0x23 0x4"},
    {"reg", "0xff0f0000 0x4000"},
};

/* /saradc@ff100000 */
static const device_prop_t DEV_saradc_ff100000_props[] = {
    {"reg", "0xff100000 0x100"},
    {"interrupts", "0x0 0x24 0x4"},
    {"#io-channel-cells", "0x1"},
    {"clocks", "0x15 0x16"},
    {"clock-names", "saradc"},
};

/* /spi@ff110000 */
static const device_prop_t DEV_spi_ff110000_props[] = {
    {"clocks", "0x17 0x18"},
    {"clock-names", "spiclk"},
    {"dmas", "0xb 0xc"},
    {"dma-names", "tx"},
    {"interrupts", "0x0 0x2c 0x4"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "spi0_clk"},
    {"reg", "0xff110000 0x1000"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /spi@ff120000 */
static const device_prop_t DEV_spi_ff120000_props[] = {
    {"clocks", "0x19 0x1a"},
    {"clock-names", "spiclk"},
    {"dmas", "0xd 0xe"},
    {"dma-names", "tx"},
    {"interrupts", "0x0 0x2d 0x4"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "spi1_clk"},
    {"reg", "0xff120000 0x1000"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /spi@ff130000 */
static const device_prop_t DEV_spi_ff130000_props[] = {
    {"clocks", "0x1b 0x1c"},
    {"clock-names", "spiclk"},
    {"dmas", "0xf 0x10"},
    {"dma-names", "tx"},
    {"interrupts", "0x0 0x2e 0x4"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "spi2_clk"},
    {"reg", "0xff130000 0x1000"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /i2c@ff140000 */
static const device_prop_t DEV_i2c_ff140000_props[] = {
    {"reg", "0xff140000 0x1000"},
    {"interrupts", "0x0 0x3e 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x1e"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c1_xfer"},
};

/* /i2c@ff150000 */
static const device_prop_t DEV_i2c_ff150000_props[] = {
    {"reg", "0xff150000 0x1000"},
    {"interrupts", "0x0 0x3f 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x20"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c3_xfer"},
};

/* /i2c@ff160000 */
static const device_prop_t DEV_i2c_ff160000_props[] = {
    {"reg", "0xff160000 0x1000"},
    {"interrupts", "0x0 0x40 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x21"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c4_xfer"},
};

/* /i2c@ff170000 */
static const device_prop_t DEV_i2c_ff170000_props[] = {
    {"reg", "0xff170000 0x1000"},
    {"interrupts", "0x0 0x41 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x22"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c5_xfer"},
};

/* /serial@ff180000 */
static const device_prop_t DEV_serial_ff180000_props[] = {
    {"reg", "0xff180000 0x100"},
    {"interrupts", "0x0 0x37 0x4"},
    {"reg-shift", "0x2"},
    {"reg-io-width", "0x4"},
    {"clocks", "0x23 0x24"},
    {"clock-names", "baudclk"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "uart0_xfer"},
};

/* /serial@ff190000 */
static const device_prop_t DEV_serial_ff190000_props[] = {
    {"reg", "0xff190000 0x100"},
    {"interrupts", "0x0 0x38 0x4"},
    {"reg-shift", "0x2"},
    {"reg-io-width", "0x4"},
    {"clocks", "0x25 0x26"},
    {"clock-names", "baudclk"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "uart1_xfer"},
};

/* /serial@ff690000 */
static const device_prop_t DEV_serial_ff690000_props[] = {
    {"reg", "0xff690000 0x100"},
    {"interrupts", "0x0 0x39 0x4"},
    {"reg-shift", "0x2"},
    {"reg-io-width", "0x4"},
    {"clocks", "0x27 0x28"},
    {"clock-names", "baudclk"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "uart2_xfer"},
};

/* /serial@ff1b0000 */
static const device_prop_t DEV_serial_ff1b0000_props[] = {
    {"reg", "0xff1b0000 0x100"},
    {"interrupts", "0x0 0x3a 0x4"},
    {"reg-shift", "0x2"},
    {"reg-io-width", "0x4"},
    {"clocks", "0x29 0x2a"},
    {"clock-names", "baudclk"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "uart3_xfer"},
};

/* /serial@ff1c0000 */
static const device_prop_t DEV_serial_ff1c0000_props[] = {
    {"reg", "0xff1c0000 0x100"},
    {"interrupts", "0x0 0x3b 0x4"},
    {"reg-shift", "0x2"},
    {"reg-io-width", "0x4"},
    {"clocks", "0x2b 0x2c"},
    {"clock-names", "baudclk"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "uart4_xfer"},
};

/* /tsadc@ff280000 */
static const device_prop_t DEV_tsadc_ff280000_props[] = {
    {"reg", "0xff280000 0x100"},
    {"interrupts", "0x0 0x25 0x4"},
    {"clocks", "0x2d 0x2e"},
    {"clock-names", "tsadc"},
    {"resets", "0x57"},
    {"reset-names", "tsadc-apb"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "otp_out"},
    {"#thermal-sensor-cells", "0x1"},
    {"rockchip,hw-tshut-temp", "0x17318"},
};

/* /ethernet@ff290000 */
static const device_prop_t DEV_ethernet_ff290000_props[] = {
    {"reg", "0xff290000 0x10000"},
    {"interrupts", "0x0 0x1b 0x4"},
    {"interrupt-names", "macirq"},
    {"rockchip,grf", "grf"},
    {"clocks", "0x2f 0x30 0x31 0x32 0x33 0x34 0x35"},
    {"clock-names", "stmmaceth"},
};

/* /usb@ff500000 */
static const device_prop_t DEV_usb_ff500000_props[] = {
    {"reg", "0xff500000 0x100"},
    {"interrupts", "0x0 0x18 0x4"},
    {"clocks", "0x36"},
    {"clock-names", "usbhost"},
    {"phys", "usbphy1"},
    {"phy-names", "usb"},
};

/* /usb@ff540000 */
static const device_prop_t DEV_usb_ff540000_props[] = {
    {"reg", "0xff540000 0x40000"},
    {"interrupts", "0x0 0x19 0x4"},
    {"clocks", "0x37"},
    {"clock-names", "otg"},
    {"phys", "usbphy2"},
    {"phy-names", "usb2-phy"},
};

/* /usb@ff580000 */
static const device_prop_t DEV_usb_ff580000_props[] = {
    {"reg", "0xff580000 0x40000"},
    {"interrupts", "0x0 0x17 0x4"},
    {"clocks", "0x38"},
    {"clock-names", "otg"},
    {"phys", "usbphy0"},
    {"phy-names", "usb2-phy"},
};

/* /usb@ff5c0000 */
static const device_prop_t DEV_usb_ff5c0000_props[] = {
    {"reg", "0xff5c0000 0x100"},
    {"interrupts", "0x0 0x1a 0x4"},
    {"clocks", "0x39"},
    {"clock-names", "usbhost"},
};

/* /i2c@ff650000 */
static const device_prop_t DEV_i2c_ff650000_props[] = {
    {"reg", "0xff650000 0x1000"},
    {"interrupts", "0x0 0x3c 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x1d"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c0_xfer"},
};

/* /i2c@ff660000 */
static const device_prop_t DEV_i2c_ff660000_props[] = {
    {"reg", "0xff660000 0x1000"},
    {"interrupts", "0x0 0x3d 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"clock-names", "i2c"},
    {"clocks", "0x1f"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2c2_xfer"},
};

/* /pwm@ff680000 */
static const device_prop_t DEV_pwm_ff680000_props[] = {
    {"reg", "0xff680000 0x10"},
    {"#pwm-cells", "0x3"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "pwm0_pin"},
    {"clocks", "0x3a"},
    {"clock-names", "pwm"},
};

/* /pwm@ff680010 */
static const device_prop_t DEV_pwm_ff680010_props[] = {
    {"reg", "0xff680010 0x10"},
    {"#pwm-cells", "0x3"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "pwm1_pin"},
    {"clocks", "0x3a"},
    {"clock-names", "pwm"},
};

/* /pwm@ff680020 */
static const device_prop_t DEV_pwm_ff680020_props[] = {
    {"reg", "0xff680020 0x10"},
    {"#pwm-cells", "0x3"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "pwm2_pin"},
    {"clocks", "0x3a"},
    {"clock-names", "pwm"},
};

/* /pwm@ff680030 */
static const device_prop_t DEV_pwm_ff680030_props[] = {
    {"reg", "0xff680030 0x10"},
    {"#pwm-cells", "0x2"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "pwm3_pin"},
    {"clocks", "0x3a"},
    {"clock-names", "pwm"},
};

/* /bus_intmem@ff700000 */
static const device_prop_t DEV_bus_intmem_ff700000_props[] = {
    {"reg", "0xff700000 0x18000"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x1"},
    {"ranges", "0x0 0xff700000 0x18000"},
};

/* /bus_intmem@ff700000/smp-sram@0 */
static const device_prop_t DEV_smp_sram_0_props[] = {
    {"reg", "0x0 0x10"},
};

/* /sram@ff720000 */
static const device_prop_t DEV_sram_ff720000_props[] = {
    {"reg", "0xff720000 0x1000"},
};

/* /power-management@ff730000 */
static const device_prop_t DEV_power_management_ff730000_props[] = {
    {"reg", "0xff730000 0x100"},
};

/* /syscon@ff740000 */
static const device_prop_t DEV_syscon_ff740000_props[] = {
    {"reg", "0xff740000 0x1000"},
};

/* /clock-controller@ff760000 */
static const device_prop_t DEV_clock_controller_ff760000_props[] = {
    {"reg", "0xff760000 0x1000"},
    {"rockchip,grf", "grf"},
    {"#clock-cells", "0x1"},
    {"#reset-cells", "0x1"},
    {"assigned-clocks", "0x1 0x2 0x3 0x5 0x6 0x7 0x8 0x9 0xa"},
    {"assigned-clock-rates", "0x2367b880 0x17d78400 0x1dcd6500 0x11e1a300 0x8f0d180 0x47868c0 0x11e1a300 0x8f0d180 0x47868c0"},
};

/* /syscon@ff770000 */
static const device_prop_t DEV_syscon_ff770000_props[] = {
    {"reg", "0xff770000 0x1000"},
};

/* /watchdog@ff800000 */
static const device_prop_t DEV_watchdog_ff800000_props[] = {
    {"reg", "0xff800000 0x100"},
    {"clocks", "0x3c"},
    {"interrupts", "0x0 0x4f 0x4"},
};

/* /i2s@ff890000 */
static const device_prop_t DEV_i2s_ff890000_props[] = {
    {"reg", "0xff890000 0x10000"},
    {"interrupts", "0x0 0x55 0x4"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
    {"dmas", "0x0 0x1"},
    {"dma-names", "tx"},
    {"clock-names", "i2s_hclk"},
    {"clocks", "0x3d 0x3e"},
    {"pinctrl-names", "default"},
    {"pinctrl-0", "i2s0_bus"},
};

/* /vop@ff930000 */
static const device_prop_t DEV_vop_ff930000_props[] = {
    {"reg", "0xff930000 0x19c"},
    {"interrupts", "0x0 0xf 0x4"},
    {"clocks", "0x3f 0x40 0x41"},
    {"clock-names", "aclk_vop"},
    {"resets", "0x58 0x59 0x5a"},
    {"reset-names", "axi"},
    {"iommus", "vopb_mmu"},
};

/* /iommu@ff930300 */
static const device_prop_t DEV_iommu_ff930300_props[] = {
    {"reg", "0xff930300 0x100"},
    {"interrupts", "0x0 0xf 0x4"},
    {"interrupt-names", "vopb_mmu"},
    {"#iommu-cells", "0x0"},
};

/* /vop@ff940000 */
static const device_prop_t DEV_vop_ff940000_props[] = {
    {"reg", "0xff940000 0x19c"},
    {"interrupts", "0x0 0x10 0x4"},
    {"clocks", "0x42 0x43 0x44"},
    {"clock-names", "aclk_vop"},
    {"resets", "0x5b 0x5c 0x5d"},
    {"reset-names", "axi"},
    {"iommus", "vopl_mmu"},
};

/* /iommu@ff940300 */
static const device_prop_t DEV_iommu_ff940300_props[] = {
    {"reg", "0xff940300 0x100"},
    {"interrupts", "0x0 0x10 0x4"},
    {"interrupt-names", "vopl_mmu"},
    {"#iommu-cells", "0x0"},
};

/* /hdmi@ff980000 */
static const device_prop_t DEV_hdmi_ff980000_props[] = {
    {"reg", "0xff980000 0x20000"},
    {"reg-io-width", "0x4"},
    {"rockchip,grf", "grf"},
    {"interrupts", "0x0 0x67 0x4"},
    {"clocks", "0x45 0x46"},
    {"clock-names", "iahb"},
};

/* /interrupt-controller@ffc01000 */
static const device_prop_t DEV_interrupt_controller_ffc01000_props[] = {
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x3"},
    {"#address-cells", "0x0"},
    {"reg", "0xffc01000 0x1000 0xffc02000 0x1000 0xffc04000 0x2000 0xffc06000 0x2000"},
    {"interrupts", "0x1 0x9 0xf04"},
};

/* /phy */
static const device_prop_t DEV_phy_props[] = {
    {"rockchip,grf", "grf"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x0"},
};

/* /pinctrl */
static const device_prop_t DEV_pinctrl_props[] = {
    {"rockchip,grf", "grf"},
    {"rockchip,pmu", "pmu"},
    {"#address-cells", "0x1"},
    {"#size-cells", "0x1"},
    {"ranges", "true"},
};

/* /pinctrl/gpio0@ff750000 */
static const device_prop_t DEV_gpio0_ff750000_props[] = {
    {"reg", "0xff750000 0x100"},
    {"interrupts", "0x0 0x51 0x4"},
    {"clocks", "0x4a"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio1@ff780000 */
static const device_prop_t DEV_gpio1_ff780000_props[] = {
    {"reg", "0xff780000 0x100"},
    {"interrupts", "0x0 0x52 0x4"},
    {"clocks", "0x4b"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio2@ff790000 */
static const device_prop_t DEV_gpio2_ff790000_props[] = {
    {"reg", "0xff790000 0x100"},
    {"interrupts", "0x0 0x53 0x4"},
    {"clocks", "0x4c"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio3@ff7a0000 */
static const device_prop_t DEV_gpio3_ff7a0000_props[] = {
    {"reg", "0xff7a0000 0x100"},
    {"interrupts", "0x0 0x54 0x4"},
    {"clocks", "0x4d"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio4@ff7b0000 */
static const device_prop_t DEV_gpio4_ff7b0000_props[] = {
    {"reg", "0xff7b0000 0x100"},
    {"interrupts", "0x0 0x55 0x4"},
    {"clocks", "0x4e"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio5@ff7c0000 */
static const device_prop_t DEV_gpio5_ff7c0000_props[] = {
    {"reg", "0xff7c0000 0x100"},
    {"interrupts", "0x0 0x56 0x4"},
    {"clocks", "0x4f"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio6@ff7d0000 */
static const device_prop_t DEV_gpio6_ff7d0000_props[] = {
    {"reg", "0xff7d0000 0x100"},
    {"interrupts", "0x0 0x57 0x4"},
    {"clocks", "0x50"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio7@ff7e0000 */
static const device_prop_t DEV_gpio7_ff7e0000_props[] = {
    {"reg", "0xff7e0000 0x100"},
    {"interrupts", "0x0 0x58 0x4"},
    {"clocks", "0x51"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* /pinctrl/gpio8@ff7f0000 */
static const device_prop_t DEV_gpio8_ff7f0000_props[] = {
    {"reg", "0xff7f0000 0x100"},
    {"interrupts", "0x0 0x59 0x4"},
    {"clocks", "0x52"},
    {"gpio-controller", "true"},
    {"#gpio-cells", "0x2"},
    {"interrupt-controller", "true"},
    {"#interrupt-cells", "0x2"},
};

/* ===== 依赖表 ===== */

static const device_id_t DEV_gpio0_ff750000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio1_ff780000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio2_ff790000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio3_ff7a0000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio4_ff7b0000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio5_ff7c0000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio6_ff7d0000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio7_ff7e0000_deps[] = {
    DEV_ID_PINCTRL,
};

static const device_id_t DEV_gpio8_ff7f0000_deps[] = {
    DEV_ID_PINCTRL,
};

/* ===== reg 分组表 (预分组, 按 #address-cells / #size-cells) ===== */

static const uint32_t DEV_cpu_500_REG_DATA[] = {
    0x500,
};
static const uint32_t DEV_cpu_501_REG_DATA[] = {
    0x501,
};
static const uint32_t DEV_cpu_502_REG_DATA[] = {
    0x502,
};
static const uint32_t DEV_cpu_503_REG_DATA[] = {
    0x503,
};
static const uint32_t DEV_dma_controller_ff250000_REG_DATA[] = {
    0xff250000, 0x4000,
};
static const uint32_t DEV_dma_controller_ff600000_REG_DATA[] = {
    0xff600000, 0x4000,
};
static const uint32_t DEV_dma_controller_ffb20000_REG_DATA[] = {
    0xffb20000, 0x4000,
};
static const uint32_t DEV_timer_ff810000_REG_DATA[] = {
    0xff810000, 0x20,
};
static const uint32_t DEV_dwmmc_ff0c0000_REG_DATA[] = {
    0xff0c0000, 0x4000,
};
static const uint32_t DEV_dwmmc_ff0d0000_REG_DATA[] = {
    0xff0d0000, 0x4000,
};
static const uint32_t DEV_dwmmc_ff0e0000_REG_DATA[] = {
    0xff0e0000, 0x4000,
};
static const uint32_t DEV_dwmmc_ff0f0000_REG_DATA[] = {
    0xff0f0000, 0x4000,
};
static const uint32_t DEV_saradc_ff100000_REG_DATA[] = {
    0xff100000, 0x100,
};
static const uint32_t DEV_spi_ff110000_REG_DATA[] = {
    0xff110000, 0x1000,
};
static const uint32_t DEV_spi_ff120000_REG_DATA[] = {
    0xff120000, 0x1000,
};
static const uint32_t DEV_spi_ff130000_REG_DATA[] = {
    0xff130000, 0x1000,
};
static const uint32_t DEV_i2c_ff140000_REG_DATA[] = {
    0xff140000, 0x1000,
};
static const uint32_t DEV_i2c_ff150000_REG_DATA[] = {
    0xff150000, 0x1000,
};
static const uint32_t DEV_i2c_ff160000_REG_DATA[] = {
    0xff160000, 0x1000,
};
static const uint32_t DEV_i2c_ff170000_REG_DATA[] = {
    0xff170000, 0x1000,
};
static const uint32_t DEV_serial_ff180000_REG_DATA[] = {
    0xff180000, 0x100,
};
static const uint32_t DEV_serial_ff190000_REG_DATA[] = {
    0xff190000, 0x100,
};
static const uint32_t DEV_serial_ff690000_REG_DATA[] = {
    0xff690000, 0x100,
};
static const uint32_t DEV_serial_ff1b0000_REG_DATA[] = {
    0xff1b0000, 0x100,
};
static const uint32_t DEV_serial_ff1c0000_REG_DATA[] = {
    0xff1c0000, 0x100,
};
static const uint32_t DEV_tsadc_ff280000_REG_DATA[] = {
    0xff280000, 0x100,
};
static const uint32_t DEV_ethernet_ff290000_REG_DATA[] = {
    0xff290000, 0x10000,
};
static const uint32_t DEV_usb_ff500000_REG_DATA[] = {
    0xff500000, 0x100,
};
static const uint32_t DEV_usb_ff540000_REG_DATA[] = {
    0xff540000, 0x40000,
};
static const uint32_t DEV_usb_ff580000_REG_DATA[] = {
    0xff580000, 0x40000,
};
static const uint32_t DEV_usb_ff5c0000_REG_DATA[] = {
    0xff5c0000, 0x100,
};
static const uint32_t DEV_i2c_ff650000_REG_DATA[] = {
    0xff650000, 0x1000,
};
static const uint32_t DEV_i2c_ff660000_REG_DATA[] = {
    0xff660000, 0x1000,
};
static const uint32_t DEV_pwm_ff680000_REG_DATA[] = {
    0xff680000, 0x10,
};
static const uint32_t DEV_pwm_ff680010_REG_DATA[] = {
    0xff680010, 0x10,
};
static const uint32_t DEV_pwm_ff680020_REG_DATA[] = {
    0xff680020, 0x10,
};
static const uint32_t DEV_pwm_ff680030_REG_DATA[] = {
    0xff680030, 0x10,
};
static const uint32_t DEV_bus_intmem_ff700000_REG_DATA[] = {
    0xff700000, 0x18000,
};
static const uint32_t DEV_smp_sram_0_REG_DATA[] = {
    0x0, 0x10,
};
static const uint32_t DEV_sram_ff720000_REG_DATA[] = {
    0xff720000, 0x1000,
};
static const uint32_t DEV_power_management_ff730000_REG_DATA[] = {
    0xff730000, 0x100,
};
static const uint32_t DEV_syscon_ff740000_REG_DATA[] = {
    0xff740000, 0x1000,
};
static const uint32_t DEV_clock_controller_ff760000_REG_DATA[] = {
    0xff760000, 0x1000,
};
static const uint32_t DEV_syscon_ff770000_REG_DATA[] = {
    0xff770000, 0x1000,
};
static const uint32_t DEV_watchdog_ff800000_REG_DATA[] = {
    0xff800000, 0x100,
};
static const uint32_t DEV_i2s_ff890000_REG_DATA[] = {
    0xff890000, 0x10000,
};
static const uint32_t DEV_vop_ff930000_REG_DATA[] = {
    0xff930000, 0x19c,
};
static const uint32_t DEV_iommu_ff930300_REG_DATA[] = {
    0xff930300, 0x100,
};
static const uint32_t DEV_vop_ff940000_REG_DATA[] = {
    0xff940000, 0x19c,
};
static const uint32_t DEV_iommu_ff940300_REG_DATA[] = {
    0xff940300, 0x100,
};
static const uint32_t DEV_hdmi_ff980000_REG_DATA[] = {
    0xff980000, 0x20000,
};
static const uint32_t DEV_interrupt_controller_ffc01000_REG_DATA[] = {
    0xffc01000, 0x1000, 0xffc02000, 0x1000, 0xffc04000, 0x2000, 0xffc06000, 0x2000,
};
static const uint32_t DEV_gpio0_ff750000_REG_DATA[] = {
    0xff750000, 0x100,
};
static const uint32_t DEV_gpio1_ff780000_REG_DATA[] = {
    0xff780000, 0x100,
};
static const uint32_t DEV_gpio2_ff790000_REG_DATA[] = {
    0xff790000, 0x100,
};
static const uint32_t DEV_gpio3_ff7a0000_REG_DATA[] = {
    0xff7a0000, 0x100,
};
static const uint32_t DEV_gpio4_ff7b0000_REG_DATA[] = {
    0xff7b0000, 0x100,
};
static const uint32_t DEV_gpio5_ff7c0000_REG_DATA[] = {
    0xff7c0000, 0x100,
};
static const uint32_t DEV_gpio6_ff7d0000_REG_DATA[] = {
    0xff7d0000, 0x100,
};
static const uint32_t DEV_gpio7_ff7e0000_REG_DATA[] = {
    0xff7e0000, 0x100,
};
static const uint32_t DEV_gpio8_ff7f0000_REG_DATA[] = {
    0xff7f0000, 0x100,
};
static const device_reg_t DEV_cpu_500_REGS[] = {
    { .addr = &DEV_cpu_500_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const device_reg_t DEV_cpu_501_REGS[] = {
    { .addr = &DEV_cpu_501_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const device_reg_t DEV_cpu_502_REGS[] = {
    { .addr = &DEV_cpu_502_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const device_reg_t DEV_cpu_503_REGS[] = {
    { .addr = &DEV_cpu_503_REG_DATA[0], .addr_cells = 1, .size = NULL, .size_cells = 0 },
};

static const device_reg_t DEV_dma_controller_ff250000_REGS[] = {
    { .addr = &DEV_dma_controller_ff250000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dma_controller_ff250000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dma_controller_ff600000_REGS[] = {
    { .addr = &DEV_dma_controller_ff600000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dma_controller_ff600000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dma_controller_ffb20000_REGS[] = {
    { .addr = &DEV_dma_controller_ffb20000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dma_controller_ffb20000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_timer_ff810000_REGS[] = {
    { .addr = &DEV_timer_ff810000_REG_DATA[0], .addr_cells = 1, .size = &DEV_timer_ff810000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dwmmc_ff0c0000_REGS[] = {
    { .addr = &DEV_dwmmc_ff0c0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dwmmc_ff0c0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dwmmc_ff0d0000_REGS[] = {
    { .addr = &DEV_dwmmc_ff0d0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dwmmc_ff0d0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dwmmc_ff0e0000_REGS[] = {
    { .addr = &DEV_dwmmc_ff0e0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dwmmc_ff0e0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_dwmmc_ff0f0000_REGS[] = {
    { .addr = &DEV_dwmmc_ff0f0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_dwmmc_ff0f0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_saradc_ff100000_REGS[] = {
    { .addr = &DEV_saradc_ff100000_REG_DATA[0], .addr_cells = 1, .size = &DEV_saradc_ff100000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_spi_ff110000_REGS[] = {
    { .addr = &DEV_spi_ff110000_REG_DATA[0], .addr_cells = 1, .size = &DEV_spi_ff110000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_spi_ff120000_REGS[] = {
    { .addr = &DEV_spi_ff120000_REG_DATA[0], .addr_cells = 1, .size = &DEV_spi_ff120000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_spi_ff130000_REGS[] = {
    { .addr = &DEV_spi_ff130000_REG_DATA[0], .addr_cells = 1, .size = &DEV_spi_ff130000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff140000_REGS[] = {
    { .addr = &DEV_i2c_ff140000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff140000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff150000_REGS[] = {
    { .addr = &DEV_i2c_ff150000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff150000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff160000_REGS[] = {
    { .addr = &DEV_i2c_ff160000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff160000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff170000_REGS[] = {
    { .addr = &DEV_i2c_ff170000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff170000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_serial_ff180000_REGS[] = {
    { .addr = &DEV_serial_ff180000_REG_DATA[0], .addr_cells = 1, .size = &DEV_serial_ff180000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_serial_ff190000_REGS[] = {
    { .addr = &DEV_serial_ff190000_REG_DATA[0], .addr_cells = 1, .size = &DEV_serial_ff190000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_serial_ff690000_REGS[] = {
    { .addr = &DEV_serial_ff690000_REG_DATA[0], .addr_cells = 1, .size = &DEV_serial_ff690000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_serial_ff1b0000_REGS[] = {
    { .addr = &DEV_serial_ff1b0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_serial_ff1b0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_serial_ff1c0000_REGS[] = {
    { .addr = &DEV_serial_ff1c0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_serial_ff1c0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_tsadc_ff280000_REGS[] = {
    { .addr = &DEV_tsadc_ff280000_REG_DATA[0], .addr_cells = 1, .size = &DEV_tsadc_ff280000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_ethernet_ff290000_REGS[] = {
    { .addr = &DEV_ethernet_ff290000_REG_DATA[0], .addr_cells = 1, .size = &DEV_ethernet_ff290000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_usb_ff500000_REGS[] = {
    { .addr = &DEV_usb_ff500000_REG_DATA[0], .addr_cells = 1, .size = &DEV_usb_ff500000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_usb_ff540000_REGS[] = {
    { .addr = &DEV_usb_ff540000_REG_DATA[0], .addr_cells = 1, .size = &DEV_usb_ff540000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_usb_ff580000_REGS[] = {
    { .addr = &DEV_usb_ff580000_REG_DATA[0], .addr_cells = 1, .size = &DEV_usb_ff580000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_usb_ff5c0000_REGS[] = {
    { .addr = &DEV_usb_ff5c0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_usb_ff5c0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff650000_REGS[] = {
    { .addr = &DEV_i2c_ff650000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff650000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2c_ff660000_REGS[] = {
    { .addr = &DEV_i2c_ff660000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2c_ff660000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_pwm_ff680000_REGS[] = {
    { .addr = &DEV_pwm_ff680000_REG_DATA[0], .addr_cells = 1, .size = &DEV_pwm_ff680000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_pwm_ff680010_REGS[] = {
    { .addr = &DEV_pwm_ff680010_REG_DATA[0], .addr_cells = 1, .size = &DEV_pwm_ff680010_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_pwm_ff680020_REGS[] = {
    { .addr = &DEV_pwm_ff680020_REG_DATA[0], .addr_cells = 1, .size = &DEV_pwm_ff680020_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_pwm_ff680030_REGS[] = {
    { .addr = &DEV_pwm_ff680030_REG_DATA[0], .addr_cells = 1, .size = &DEV_pwm_ff680030_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_bus_intmem_ff700000_REGS[] = {
    { .addr = &DEV_bus_intmem_ff700000_REG_DATA[0], .addr_cells = 1, .size = &DEV_bus_intmem_ff700000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_smp_sram_0_REGS[] = {
    { .addr = &DEV_smp_sram_0_REG_DATA[0], .addr_cells = 1, .size = &DEV_smp_sram_0_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_sram_ff720000_REGS[] = {
    { .addr = &DEV_sram_ff720000_REG_DATA[0], .addr_cells = 1, .size = &DEV_sram_ff720000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_power_management_ff730000_REGS[] = {
    { .addr = &DEV_power_management_ff730000_REG_DATA[0], .addr_cells = 1, .size = &DEV_power_management_ff730000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_syscon_ff740000_REGS[] = {
    { .addr = &DEV_syscon_ff740000_REG_DATA[0], .addr_cells = 1, .size = &DEV_syscon_ff740000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_clock_controller_ff760000_REGS[] = {
    { .addr = &DEV_clock_controller_ff760000_REG_DATA[0], .addr_cells = 1, .size = &DEV_clock_controller_ff760000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_syscon_ff770000_REGS[] = {
    { .addr = &DEV_syscon_ff770000_REG_DATA[0], .addr_cells = 1, .size = &DEV_syscon_ff770000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_watchdog_ff800000_REGS[] = {
    { .addr = &DEV_watchdog_ff800000_REG_DATA[0], .addr_cells = 1, .size = &DEV_watchdog_ff800000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_i2s_ff890000_REGS[] = {
    { .addr = &DEV_i2s_ff890000_REG_DATA[0], .addr_cells = 1, .size = &DEV_i2s_ff890000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_vop_ff930000_REGS[] = {
    { .addr = &DEV_vop_ff930000_REG_DATA[0], .addr_cells = 1, .size = &DEV_vop_ff930000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_iommu_ff930300_REGS[] = {
    { .addr = &DEV_iommu_ff930300_REG_DATA[0], .addr_cells = 1, .size = &DEV_iommu_ff930300_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_vop_ff940000_REGS[] = {
    { .addr = &DEV_vop_ff940000_REG_DATA[0], .addr_cells = 1, .size = &DEV_vop_ff940000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_iommu_ff940300_REGS[] = {
    { .addr = &DEV_iommu_ff940300_REG_DATA[0], .addr_cells = 1, .size = &DEV_iommu_ff940300_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_hdmi_ff980000_REGS[] = {
    { .addr = &DEV_hdmi_ff980000_REG_DATA[0], .addr_cells = 1, .size = &DEV_hdmi_ff980000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_interrupt_controller_ffc01000_REGS[] = {
    { .addr = &DEV_interrupt_controller_ffc01000_REG_DATA[0], .addr_cells = 1, .size = &DEV_interrupt_controller_ffc01000_REG_DATA[1], .size_cells = 1 },
    { .addr = &DEV_interrupt_controller_ffc01000_REG_DATA[2], .addr_cells = 1, .size = &DEV_interrupt_controller_ffc01000_REG_DATA[3], .size_cells = 1 },
    { .addr = &DEV_interrupt_controller_ffc01000_REG_DATA[4], .addr_cells = 1, .size = &DEV_interrupt_controller_ffc01000_REG_DATA[5], .size_cells = 1 },
    { .addr = &DEV_interrupt_controller_ffc01000_REG_DATA[6], .addr_cells = 1, .size = &DEV_interrupt_controller_ffc01000_REG_DATA[7], .size_cells = 1 },
};

static const device_reg_t DEV_gpio0_ff750000_REGS[] = {
    { .addr = &DEV_gpio0_ff750000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio0_ff750000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio1_ff780000_REGS[] = {
    { .addr = &DEV_gpio1_ff780000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio1_ff780000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio2_ff790000_REGS[] = {
    { .addr = &DEV_gpio2_ff790000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio2_ff790000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio3_ff7a0000_REGS[] = {
    { .addr = &DEV_gpio3_ff7a0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio3_ff7a0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio4_ff7b0000_REGS[] = {
    { .addr = &DEV_gpio4_ff7b0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio4_ff7b0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio5_ff7c0000_REGS[] = {
    { .addr = &DEV_gpio5_ff7c0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio5_ff7c0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio6_ff7d0000_REGS[] = {
    { .addr = &DEV_gpio6_ff7d0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio6_ff7d0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio7_ff7e0000_REGS[] = {
    { .addr = &DEV_gpio7_ff7e0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio7_ff7e0000_REG_DATA[1], .size_cells = 1 },
};

static const device_reg_t DEV_gpio8_ff7f0000_REGS[] = {
    { .addr = &DEV_gpio8_ff7f0000_REG_DATA[0], .addr_cells = 1, .size = &DEV_gpio8_ff7f0000_REG_DATA[1], .size_cells = 1 },
};

/* ===== irq 表 (预分组, 按 #interrupt-cells) ===== */

static const device_irq_t DEV_dma_controller_ff250000_IRQS[] = {
    { .irq = 2, .type = 0, .flags = 4 },
    { .irq = 3, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dma_controller_ff600000_IRQS[] = {
    { .irq = 0, .type = 0, .flags = 4 },
    { .irq = 1, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dma_controller_ffb20000_IRQS[] = {
    { .irq = 0, .type = 0, .flags = 4 },
    { .irq = 1, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_timer_IRQS[] = {
    { .irq = 13, .type = 1, .flags = 4 },
    { .irq = 1, .type = 4, .flags = 14 },
    { .irq = 4, .type = 4, .flags = 1 },
    { .irq = 4, .type = 11, .flags = 4 },
    { .irq = 10, .type = 1, .flags = 4 },
};

static const device_irq_t DEV_timer_ff810000_IRQS[] = {
    { .irq = 72, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dwmmc_ff0c0000_IRQS[] = {
    { .irq = 32, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dwmmc_ff0d0000_IRQS[] = {
    { .irq = 33, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dwmmc_ff0e0000_IRQS[] = {
    { .irq = 34, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_dwmmc_ff0f0000_IRQS[] = {
    { .irq = 35, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_saradc_ff100000_IRQS[] = {
    { .irq = 36, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_spi_ff110000_IRQS[] = {
    { .irq = 44, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_spi_ff120000_IRQS[] = {
    { .irq = 45, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_spi_ff130000_IRQS[] = {
    { .irq = 46, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff140000_IRQS[] = {
    { .irq = 62, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff150000_IRQS[] = {
    { .irq = 63, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff160000_IRQS[] = {
    { .irq = 64, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff170000_IRQS[] = {
    { .irq = 65, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_serial_ff180000_IRQS[] = {
    { .irq = 55, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_serial_ff190000_IRQS[] = {
    { .irq = 56, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_serial_ff690000_IRQS[] = {
    { .irq = 57, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_serial_ff1b0000_IRQS[] = {
    { .irq = 58, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_serial_ff1c0000_IRQS[] = {
    { .irq = 59, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_tsadc_ff280000_IRQS[] = {
    { .irq = 37, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_ethernet_ff290000_IRQS[] = {
    { .irq = 27, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_usb_ff500000_IRQS[] = {
    { .irq = 24, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_usb_ff540000_IRQS[] = {
    { .irq = 25, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_usb_ff580000_IRQS[] = {
    { .irq = 23, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_usb_ff5c0000_IRQS[] = {
    { .irq = 26, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff650000_IRQS[] = {
    { .irq = 60, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2c_ff660000_IRQS[] = {
    { .irq = 61, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_watchdog_ff800000_IRQS[] = {
    { .irq = 79, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_i2s_ff890000_IRQS[] = {
    { .irq = 85, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_vop_ff930000_IRQS[] = {
    { .irq = 15, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_iommu_ff930300_IRQS[] = {
    { .irq = 15, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_vop_ff940000_IRQS[] = {
    { .irq = 16, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_iommu_ff940300_IRQS[] = {
    { .irq = 16, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_hdmi_ff980000_IRQS[] = {
    { .irq = 103, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_interrupt_controller_ffc01000_IRQS[] = {
    { .irq = 9, .type = 1, .flags = 3844 },
};

static const device_irq_t DEV_gpio0_ff750000_IRQS[] = {
    { .irq = 81, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio1_ff780000_IRQS[] = {
    { .irq = 82, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio2_ff790000_IRQS[] = {
    { .irq = 83, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio3_ff7a0000_IRQS[] = {
    { .irq = 84, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio4_ff7b0000_IRQS[] = {
    { .irq = 85, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio5_ff7c0000_IRQS[] = {
    { .irq = 86, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio6_ff7d0000_IRQS[] = {
    { .irq = 87, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio7_ff7e0000_IRQS[] = {
    { .irq = 88, .type = 0, .flags = 4 },
};

static const device_irq_t DEV_gpio8_ff7f0000_IRQS[] = {
    { .irq = 89, .type = 0, .flags = 4 },
};

/* ===== 主节点表 (只读 .rodata) ===== */
static const device_node_t s_nodes[DEV_ID_COUNT] = {
    [DEV_ID_] = {
        .name       = "",
        .label      = "",
        .compatible = "rockchip,rk3288",
        .path       = "/",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV__props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_CPU_500] = {
        .name       = "cpu@500",
        .label      = "cpu0",
        .compatible = "arm,cortex-a12",
        .path       = "/cpus/cpu@500",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_cpu_500_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_cpu_500_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_CPU_501] = {
        .name       = "cpu@501",
        .label      = "",
        .compatible = "arm,cortex-a12",
        .path       = "/cpus/cpu@501",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_cpu_501_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_cpu_501_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_CPU_502] = {
        .name       = "cpu@502",
        .label      = "",
        .compatible = "arm,cortex-a12",
        .path       = "/cpus/cpu@502",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_cpu_502_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_cpu_502_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_CPU_503] = {
        .name       = "cpu@503",
        .label      = "",
        .compatible = "arm,cortex-a12",
        .path       = "/cpus/cpu@503",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_cpu_503_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_cpu_503_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_AMBA] = {
        .name       = "amba",
        .label      = "",
        .compatible = "arm,amba-bus",
        .path       = "/amba",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_amba_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_DMA_CONTROLLER_FF250000] = {
        .name       = "dma-controller@ff250000",
        .label      = "dmac_peri",
        .compatible = "arm,pl330",
        .path       = "/amba/dma-controller@ff250000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_dma_controller_ff250000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dma_controller_ff250000_REGS,
        .irq_count  = 2,
        .irqs       = (const device_irq_t*)DEV_dma_controller_ff250000_IRQS,
    },
    [DEV_ID_DMA_CONTROLLER_FF600000] = {
        .name       = "dma-controller@ff600000",
        .label      = "dmac_bus_ns",
        .compatible = "arm,pl330",
        .path       = "/amba/dma-controller@ff600000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_dma_controller_ff600000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dma_controller_ff600000_REGS,
        .irq_count  = 2,
        .irqs       = (const device_irq_t*)DEV_dma_controller_ff600000_IRQS,
    },
    [DEV_ID_DMA_CONTROLLER_FFB20000] = {
        .name       = "dma-controller@ffb20000",
        .label      = "dmac_bus_s",
        .compatible = "arm,pl330",
        .path       = "/amba/dma-controller@ffb20000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_dma_controller_ffb20000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dma_controller_ffb20000_REGS,
        .irq_count  = 2,
        .irqs       = (const device_irq_t*)DEV_dma_controller_ffb20000_IRQS,
    },
    [DEV_ID_OSCILLATOR] = {
        .name       = "oscillator",
        .label      = "xin24m",
        .compatible = "fixed-clock",
        .path       = "/oscillator",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_oscillator_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_TIMER] = {
        .name       = "timer",
        .label      = "",
        .compatible = "arm,armv7-timer",
        .path       = "/timer",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_timer_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 5,
        .irqs       = (const device_irq_t*)DEV_timer_IRQS,
    },
    [DEV_ID_TIMER_FF810000] = {
        .name       = "timer@ff810000",
        .label      = "timer",
        .compatible = "rockchip,rk3288-timer",
        .path       = "/timer@ff810000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_timer_ff810000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_timer_ff810000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_timer_ff810000_IRQS,
    },
    [DEV_ID_DISPLAY_SUBSYSTEM] = {
        .name       = "display-subsystem",
        .label      = "",
        .compatible = "rockchip,display-subsystem",
        .path       = "/display-subsystem",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_display_subsystem_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_DWMMC_FF0C0000] = {
        .name       = "dwmmc@ff0c0000",
        .label      = "sdmmc",
        .compatible = "rockchip,rk3288-dw-mshc",
        .path       = "/dwmmc@ff0c0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_dwmmc_ff0c0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dwmmc_ff0c0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_dwmmc_ff0c0000_IRQS,
    },
    [DEV_ID_DWMMC_FF0D0000] = {
        .name       = "dwmmc@ff0d0000",
        .label      = "sdio0",
        .compatible = "rockchip,rk3288-dw-mshc",
        .path       = "/dwmmc@ff0d0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_dwmmc_ff0d0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dwmmc_ff0d0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_dwmmc_ff0d0000_IRQS,
    },
    [DEV_ID_DWMMC_FF0E0000] = {
        .name       = "dwmmc@ff0e0000",
        .label      = "sdio1",
        .compatible = "rockchip,rk3288-dw-mshc",
        .path       = "/dwmmc@ff0e0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_dwmmc_ff0e0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dwmmc_ff0e0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_dwmmc_ff0e0000_IRQS,
    },
    [DEV_ID_DWMMC_FF0F0000] = {
        .name       = "dwmmc@ff0f0000",
        .label      = "emmc",
        .compatible = "rockchip,rk3288-dw-mshc",
        .path       = "/dwmmc@ff0f0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_dwmmc_ff0f0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_dwmmc_ff0f0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_dwmmc_ff0f0000_IRQS,
    },
    [DEV_ID_SARADC_FF100000] = {
        .name       = "saradc@ff100000",
        .label      = "saradc",
        .compatible = "rockchip,saradc",
        .path       = "/saradc@ff100000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_saradc_ff100000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_saradc_ff100000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_saradc_ff100000_IRQS,
    },
    [DEV_ID_SPI_FF110000] = {
        .name       = "spi@ff110000",
        .label      = "spi0",
        .compatible = "rockchip,rk3288-spi",
        .path       = "/spi@ff110000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_spi_ff110000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_spi_ff110000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_spi_ff110000_IRQS,
    },
    [DEV_ID_SPI_FF120000] = {
        .name       = "spi@ff120000",
        .label      = "spi1",
        .compatible = "rockchip,rk3288-spi",
        .path       = "/spi@ff120000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_spi_ff120000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_spi_ff120000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_spi_ff120000_IRQS,
    },
    [DEV_ID_SPI_FF130000] = {
        .name       = "spi@ff130000",
        .label      = "spi2",
        .compatible = "rockchip,rk3288-spi",
        .path       = "/spi@ff130000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_spi_ff130000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_spi_ff130000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_spi_ff130000_IRQS,
    },
    [DEV_ID_I2C_FF140000] = {
        .name       = "i2c@ff140000",
        .label      = "i2c1",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff140000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff140000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff140000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff140000_IRQS,
    },
    [DEV_ID_I2C_FF150000] = {
        .name       = "i2c@ff150000",
        .label      = "i2c3",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff150000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff150000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff150000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff150000_IRQS,
    },
    [DEV_ID_I2C_FF160000] = {
        .name       = "i2c@ff160000",
        .label      = "i2c4",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff160000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff160000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff160000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff160000_IRQS,
    },
    [DEV_ID_I2C_FF170000] = {
        .name       = "i2c@ff170000",
        .label      = "i2c5",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff170000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff170000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff170000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff170000_IRQS,
    },
    [DEV_ID_SERIAL_FF180000] = {
        .name       = "serial@ff180000",
        .label      = "uart0",
        .compatible = "rockchip,rk3288-uart",
        .path       = "/serial@ff180000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_serial_ff180000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_serial_ff180000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_serial_ff180000_IRQS,
    },
    [DEV_ID_SERIAL_FF190000] = {
        .name       = "serial@ff190000",
        .label      = "uart1",
        .compatible = "rockchip,rk3288-uart",
        .path       = "/serial@ff190000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_serial_ff190000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_serial_ff190000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_serial_ff190000_IRQS,
    },
    [DEV_ID_SERIAL_FF690000] = {
        .name       = "serial@ff690000",
        .label      = "uart2",
        .compatible = "rockchip,rk3288-uart",
        .path       = "/serial@ff690000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_serial_ff690000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_serial_ff690000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_serial_ff690000_IRQS,
    },
    [DEV_ID_SERIAL_FF1B0000] = {
        .name       = "serial@ff1b0000",
        .label      = "uart3",
        .compatible = "rockchip,rk3288-uart",
        .path       = "/serial@ff1b0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_serial_ff1b0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_serial_ff1b0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_serial_ff1b0000_IRQS,
    },
    [DEV_ID_SERIAL_FF1C0000] = {
        .name       = "serial@ff1c0000",
        .label      = "uart4",
        .compatible = "rockchip,rk3288-uart",
        .path       = "/serial@ff1c0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_serial_ff1c0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_serial_ff1c0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_serial_ff1c0000_IRQS,
    },
    [DEV_ID_TSADC_FF280000] = {
        .name       = "tsadc@ff280000",
        .label      = "tsadc",
        .compatible = "rockchip,rk3288-tsadc",
        .path       = "/tsadc@ff280000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_tsadc_ff280000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_tsadc_ff280000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_tsadc_ff280000_IRQS,
    },
    [DEV_ID_ETHERNET_FF290000] = {
        .name       = "ethernet@ff290000",
        .label      = "gmac",
        .compatible = "rockchip,rk3288-gmac",
        .path       = "/ethernet@ff290000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_ethernet_ff290000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_ethernet_ff290000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_ethernet_ff290000_IRQS,
    },
    [DEV_ID_USB_FF500000] = {
        .name       = "usb@ff500000",
        .label      = "usb_host0_ehci",
        .compatible = "generic-ehci",
        .path       = "/usb@ff500000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_usb_ff500000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_usb_ff500000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_usb_ff500000_IRQS,
    },
    [DEV_ID_USB_FF540000] = {
        .name       = "usb@ff540000",
        .label      = "usb_host1",
        .compatible = "rockchip,rk3288-usb",
        .path       = "/usb@ff540000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_usb_ff540000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_usb_ff540000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_usb_ff540000_IRQS,
    },
    [DEV_ID_USB_FF580000] = {
        .name       = "usb@ff580000",
        .label      = "usb_otg",
        .compatible = "rockchip,rk3288-usb",
        .path       = "/usb@ff580000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_usb_ff580000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_usb_ff580000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_usb_ff580000_IRQS,
    },
    [DEV_ID_USB_FF5C0000] = {
        .name       = "usb@ff5c0000",
        .label      = "usb_hsic",
        .compatible = "generic-ehci",
        .path       = "/usb@ff5c0000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_usb_ff5c0000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_usb_ff5c0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_usb_ff5c0000_IRQS,
    },
    [DEV_ID_I2C_FF650000] = {
        .name       = "i2c@ff650000",
        .label      = "i2c0",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff650000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff650000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff650000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff650000_IRQS,
    },
    [DEV_ID_I2C_FF660000] = {
        .name       = "i2c@ff660000",
        .label      = "i2c2",
        .compatible = "rockchip,rk3288-i2c",
        .path       = "/i2c@ff660000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 8,
        .props      = DEV_i2c_ff660000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2c_ff660000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2c_ff660000_IRQS,
    },
    [DEV_ID_PWM_FF680000] = {
        .name       = "pwm@ff680000",
        .label      = "pwm0",
        .compatible = "rockchip,rk3288-pwm",
        .path       = "/pwm@ff680000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_pwm_ff680000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_pwm_ff680000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_PWM_FF680010] = {
        .name       = "pwm@ff680010",
        .label      = "pwm1",
        .compatible = "rockchip,rk3288-pwm",
        .path       = "/pwm@ff680010",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_pwm_ff680010_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_pwm_ff680010_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_PWM_FF680020] = {
        .name       = "pwm@ff680020",
        .label      = "pwm2",
        .compatible = "rockchip,rk3288-pwm",
        .path       = "/pwm@ff680020",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_pwm_ff680020_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_pwm_ff680020_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_PWM_FF680030] = {
        .name       = "pwm@ff680030",
        .label      = "pwm3",
        .compatible = "rockchip,rk3288-pwm",
        .path       = "/pwm@ff680030",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_pwm_ff680030_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_pwm_ff680030_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_BUS_INTMEM_FF700000] = {
        .name       = "bus_intmem@ff700000",
        .label      = "",
        .compatible = "mmio-sram",
        .path       = "/bus_intmem@ff700000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_bus_intmem_ff700000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_bus_intmem_ff700000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_SMP_SRAM_0] = {
        .name       = "smp-sram@0",
        .label      = "",
        .compatible = "rockchip,rk3066-smp-sram",
        .path       = "/bus_intmem@ff700000/smp-sram@0",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_smp_sram_0_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_smp_sram_0_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_SRAM_FF720000] = {
        .name       = "sram@ff720000",
        .label      = "",
        .compatible = "rockchip,rk3288-pmu-sram",
        .path       = "/sram@ff720000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_sram_ff720000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_sram_ff720000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_POWER_MANAGEMENT_FF730000] = {
        .name       = "power-management@ff730000",
        .label      = "pmu",
        .compatible = "rockchip,rk3288-pmu",
        .path       = "/power-management@ff730000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_power_management_ff730000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_power_management_ff730000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_SYSCON_FF740000] = {
        .name       = "syscon@ff740000",
        .label      = "sgrf",
        .compatible = "rockchip,rk3288-sgrf",
        .path       = "/syscon@ff740000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_syscon_ff740000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_syscon_ff740000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_CLOCK_CONTROLLER_FF760000] = {
        .name       = "clock-controller@ff760000",
        .label      = "cru",
        .compatible = "rockchip,rk3288-cru",
        .path       = "/clock-controller@ff760000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_clock_controller_ff760000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_clock_controller_ff760000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_SYSCON_FF770000] = {
        .name       = "syscon@ff770000",
        .label      = "grf",
        .compatible = "rockchip,rk3288-grf",
        .path       = "/syscon@ff770000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 1,
        .props      = DEV_syscon_ff770000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_syscon_ff770000_REGS,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_WATCHDOG_FF800000] = {
        .name       = "watchdog@ff800000",
        .label      = "wdt",
        .compatible = "rockchip,rk3288-wdt",
        .path       = "/watchdog@ff800000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_watchdog_ff800000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_watchdog_ff800000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_watchdog_ff800000_IRQS,
    },
    [DEV_ID_I2S_FF890000] = {
        .name       = "i2s@ff890000",
        .label      = "i2s",
        .compatible = "rockchip,rk3288-i2s",
        .path       = "/i2s@ff890000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 10,
        .props      = DEV_i2s_ff890000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_i2s_ff890000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_i2s_ff890000_IRQS,
    },
    [DEV_ID_VOP_FF930000] = {
        .name       = "vop@ff930000",
        .label      = "vopb",
        .compatible = "rockchip,rk3288-vop",
        .path       = "/vop@ff930000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_vop_ff930000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_vop_ff930000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_vop_ff930000_IRQS,
    },
    [DEV_ID_IOMMU_FF930300] = {
        .name       = "iommu@ff930300",
        .label      = "vopb_mmu",
        .compatible = "rockchip,iommu",
        .path       = "/iommu@ff930300",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_iommu_ff930300_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_iommu_ff930300_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_iommu_ff930300_IRQS,
    },
    [DEV_ID_VOP_FF940000] = {
        .name       = "vop@ff940000",
        .label      = "vopl",
        .compatible = "rockchip,rk3288-vop",
        .path       = "/vop@ff940000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_vop_ff940000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_vop_ff940000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_vop_ff940000_IRQS,
    },
    [DEV_ID_IOMMU_FF940300] = {
        .name       = "iommu@ff940300",
        .label      = "vopl_mmu",
        .compatible = "rockchip,iommu",
        .path       = "/iommu@ff940300",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 4,
        .props      = DEV_iommu_ff940300_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_iommu_ff940300_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_iommu_ff940300_IRQS,
    },
    [DEV_ID_HDMI_FF980000] = {
        .name       = "hdmi@ff980000",
        .label      = "hdmi",
        .compatible = "rockchip,rk3288-dw-hdmi",
        .path       = "/hdmi@ff980000",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 6,
        .props      = DEV_hdmi_ff980000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_hdmi_ff980000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_hdmi_ff980000_IRQS,
    },
    [DEV_ID_INTERRUPT_CONTROLLER_FFC01000] = {
        .name       = "interrupt-controller@ffc01000",
        .label      = "gic",
        .compatible = "arm,gic-400",
        .path       = "/interrupt-controller@ffc01000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_interrupt_controller_ffc01000_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 4,
        .regs       = (const device_reg_t*)DEV_interrupt_controller_ffc01000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_interrupt_controller_ffc01000_IRQS,
    },
    [DEV_ID_PHY] = {
        .name       = "phy",
        .label      = "usbphy",
        .compatible = "rockchip,rk3288-usb-phy",
        .path       = "/phy",
        .status     = DEVICE_STATUS_DISABLED,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 3,
        .props      = DEV_phy_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_PINCTRL] = {
        .name       = "pinctrl",
        .label      = "pinctrl",
        .compatible = "rockchip,rk3288-pinctrl",
        .path       = "/pinctrl",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 5,
        .props      = DEV_pinctrl_props,
        .dep_count  = 0,
        .deps       = (const device_id_t*)NULL,
        .reg_count  = 0,
        .regs       = (const device_reg_t*)NULL,
        .irq_count  = 0,
        .irqs       = (const device_irq_t*)NULL,
    },
    [DEV_ID_GPIO0_FF750000] = {
        .name       = "gpio0@ff750000",
        .label      = "gpio0",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio0@ff750000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio0_ff750000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio0_ff750000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio0_ff750000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio0_ff750000_IRQS,
    },
    [DEV_ID_GPIO1_FF780000] = {
        .name       = "gpio1@ff780000",
        .label      = "gpio1",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio1@ff780000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio1_ff780000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio1_ff780000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio1_ff780000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio1_ff780000_IRQS,
    },
    [DEV_ID_GPIO2_FF790000] = {
        .name       = "gpio2@ff790000",
        .label      = "gpio2",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio2@ff790000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio2_ff790000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio2_ff790000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio2_ff790000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio2_ff790000_IRQS,
    },
    [DEV_ID_GPIO3_FF7A0000] = {
        .name       = "gpio3@ff7a0000",
        .label      = "gpio3",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio3@ff7a0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio3_ff7a0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio3_ff7a0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio3_ff7a0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio3_ff7a0000_IRQS,
    },
    [DEV_ID_GPIO4_FF7B0000] = {
        .name       = "gpio4@ff7b0000",
        .label      = "gpio4",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio4@ff7b0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio4_ff7b0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio4_ff7b0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio4_ff7b0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio4_ff7b0000_IRQS,
    },
    [DEV_ID_GPIO5_FF7C0000] = {
        .name       = "gpio5@ff7c0000",
        .label      = "gpio5",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio5@ff7c0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio5_ff7c0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio5_ff7c0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio5_ff7c0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio5_ff7c0000_IRQS,
    },
    [DEV_ID_GPIO6_FF7D0000] = {
        .name       = "gpio6@ff7d0000",
        .label      = "gpio6",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio6@ff7d0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio6_ff7d0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio6_ff7d0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio6_ff7d0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio6_ff7d0000_IRQS,
    },
    [DEV_ID_GPIO7_FF7E0000] = {
        .name       = "gpio7@ff7e0000",
        .label      = "gpio7",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio7@ff7e0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio7_ff7e0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio7_ff7e0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio7_ff7e0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio7_ff7e0000_IRQS,
    },
    [DEV_ID_GPIO8_FF7F0000] = {
        .name       = "gpio8@ff7f0000",
        .label      = "gpio8",
        .compatible = "rockchip,gpio-bank",
        .path       = "/pinctrl/gpio8@ff7f0000",
        .status     = DEVICE_STATUS_READY,
        .criticality = DEVICE_CRIT_WARNING,
        .flags      = 0,
        .prop_count = 7,
        .props      = DEV_gpio8_ff7f0000_props,
        .dep_count  = 1,
        .deps       = (const device_id_t*)DEV_gpio8_ff7f0000_deps,
        .reg_count  = 1,
        .regs       = (const device_reg_t*)DEV_gpio8_ff7f0000_REGS,
        .irq_count  = 1,
        .irqs       = (const device_irq_t*)DEV_gpio8_ff7f0000_IRQS,
    },
};

/* ===== API 实现 ===== */

const device_node_t* board_node_get(device_id_t id) {
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return &s_nodes[id];
}

int board_dev_count(void) { return DEV_ID_COUNT; }

device_id_t board_dev_find(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (strcmp(s_nodes[i].name, name) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_compat(const char* compatible) {
    if (!compatible) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].compatible[0] &&
            strcmp(s_nodes[i].compatible, compatible) == 0)
            return (device_id_t)i;
    }
    return -1;
}

device_id_t board_dev_find_by_label(const char* label) {
    if (!label || !label[0]) return -1;
    for (int i = 0; i < DEV_ID_COUNT; i++) {
        if (s_nodes[i].label[0] &&
            strcmp(s_nodes[i].label, label) == 0)
            return (device_id_t)i;
    }
    return -1;
}
