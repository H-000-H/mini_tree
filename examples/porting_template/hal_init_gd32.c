/*
 * hal_init_gd32.c — GD32 多级初始化参考 (goto 错误处理模式 + 设备树适配)
 *
 * 展示 GD32 标准外设库下的多外设分级初始化,
 * 任一阶段失败时通过 goto 标签逐级回退释放已占用资源。
 * 可配置参数通过设备树 (DTS) 读取, 固定不变的硬件操作直写.
 *
 * ══════════════════════════════════════════════════════════════════
 * 设备树继承模式
 *
 * 本模板架构要求: 移植时在 board/<platform>/board.dts 中定义外设节点,
 * 驱动通过 device_find_by_label + device_get_prop_int 读取属性。
 *
 * 继承方式:
 *   基节点 → 默认值硬编码在 C 中 (如 DTS_DEF_UART_TX_PIN)
 *   覆盖   → device_find_by_label("uart0") 查找 DTS 节点
 *           device_get_prop_int(dev, "tx-pin", &val) 读属性
 *           属性不存在 → 保持默认值
 *
 * 以本文件为例, 以下 DTS 节点应在板级 board.dts 中定义:
 *
 *   uart0: uart@0 {
 *       compatible = "myplatform,uart";
 *       reg = <0>;
 *       tx-pin    = <9>;        /* USART0_TX (PA9)  */
 *       rx-pin    = <10>;       /* USART0_RX (PA10) */
 *       baud-rate = <115200>;
 *   };
 *
 *   spi0: spi@0 {
 *       compatible = "myplatform,spi-bus";
 *       reg = <0>;
 *       mosi        = <15>;     /* SPI0_MOSI (PB15) */
 *       miso        = <14>;     /* SPI0_MISO (PB14) */
 *       sclk        = <13>;     /* SPI0_SCLK (PB13) */
 *       cs-gpios    = <12>;     /* SPI0_CS   (PB12) */
 *       clock-speed = <8000000>;
 *       mode        = <0>;
 *   };
 *
 *   dma0: dma@0 {
 *       compatible = "myplatform,dma";
 *       reg = <0>;
 *       channel  = <0>;
 *       priority = <2>;
 *   };
 *
 *   pwm0: pwm@0 {
 *       compatible = "myplatform,pwm";
 *       channels = <8>;
 *       clock-freq    = <100000000>;   /* 定时器时钟 */
 *       period-us     = <10000>;       /* 10 ms 周期  */
 *       default-pulse = <5000>;        /* 50% 占空比  */
 *   };
 *
 * periph_clock_enable() 和 gpio_af_set() 这类不变硬件操作
 * 直接硬编码, 不经过 DTS.
 * ══════════════════════════════════════════════════════════════════
 *
 * 目标芯片: GD32F30x / GD32F4xx (GD32 标准外设库风格)
 */

#include "hal_uart.h"
#include "hal_spi_bus.h"
#include "hal_dma.h"
#include "hal_timer.h"
#include "device.h"
#include <stdint.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════════
 * 设备树适配层
 *
 * 1. 定义默认值 ("基节点", 固定不变)
 * 2. board_dts_load() 通过 device_find_by_label 查找 DTS 节点,
 *    通过 device_get_prop_int 读取属性覆盖默认值
 *    (类似 DTS 的 &uart0 { property = <val>; })
 * 3. board_periph_init() 调用 board_dts_load() 获取最终配置
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── 默认配置 (基节点: 无 DTS 时的回退值) ── */
#define DTS_DEF_UART_ID       0
#define DTS_DEF_UART_TX_PIN   9
#define DTS_DEF_UART_RX_PIN   10
#define DTS_DEF_UART_BAUD     115200

#define DTS_DEF_SPI_HOST      0
#define DTS_DEF_SPI_MOSI      15
#define DTS_DEF_SPI_MISO      14
#define DTS_DEF_SPI_SCLK      13
#define DTS_DEF_SPI_CS        12
#define DTS_DEF_SPI_CLOCK     8000000
#define DTS_DEF_SPI_MODE      0

#define DTS_DEF_DMA_ID        0
#define DTS_DEF_DMA_CH        0
#define DTS_DEF_DMA_PRIO      2

#define DTS_DEF_TIMER_ID      1
#define DTS_DEF_TIMER_FREQ    100000000
#define DTS_DEF_TIMER_PERIOD  10000
#define DTS_DEF_TIMER_PULSE   5000

/* ── 运行时 DTS 解析结果 ── */
typedef struct
{
    int uart_id, uart_tx, uart_rx, uart_baud;
    int spi_host, spi_mosi, spi_miso, spi_sclk, spi_cs, spi_clock, spi_mode;
    int dma_id, dma_ch, dma_prio;
    int timer_id, timer_freq, timer_period, timer_pulse;
} board_dts_cfg_t;

/* ── board_dts_load: 填充默认值 → DTS 覆盖 ──
 *
 * &uart0 覆盖: device_find_by_label("uart0") 查找 DTS 节点
 *              device_get_prop_int(dev, "tx-pin", &val) 读属性
 * &spi0, &dma0, &pwm0 同理
 *
 * 无 DTS 节点或无属性 → 保持默认值
 */
static void board_dts_load(board_dts_cfg_t* cfg)
{
    if (!cfg) return;

    /* 先填充默认值 ("基节点") */
    cfg->uart_id   = DTS_DEF_UART_ID;
    cfg->uart_tx   = DTS_DEF_UART_TX_PIN;
    cfg->uart_rx   = DTS_DEF_UART_RX_PIN;
    cfg->uart_baud = DTS_DEF_UART_BAUD;

    cfg->spi_host  = DTS_DEF_SPI_HOST;
    cfg->spi_mosi  = DTS_DEF_SPI_MOSI;
    cfg->spi_miso  = DTS_DEF_SPI_MISO;
    cfg->spi_sclk  = DTS_DEF_SPI_SCLK;
    cfg->spi_cs    = DTS_DEF_SPI_CS;
    cfg->spi_clock = DTS_DEF_SPI_CLOCK;
    cfg->spi_mode  = DTS_DEF_SPI_MODE;

    cfg->dma_id   = DTS_DEF_DMA_ID;
    cfg->dma_ch   = DTS_DEF_DMA_CH;
    cfg->dma_prio = DTS_DEF_DMA_PRIO;

    cfg->timer_id     = DTS_DEF_TIMER_ID;
    cfg->timer_freq   = DTS_DEF_TIMER_FREQ;
    cfg->timer_period = DTS_DEF_TIMER_PERIOD;
    cfg->timer_pulse  = DTS_DEF_TIMER_PULSE;

    /* ── &uart0: DTS 覆盖 ── */
    {
        device_t* dev = device_find_by_label("uart0");
        if (dev)
        {
            device_get_prop_int(dev, "reg",       &cfg->uart_id);
            device_get_prop_int(dev, "tx-pin",    &cfg->uart_tx);
            device_get_prop_int(dev, "rx-pin",    &cfg->uart_rx);
            device_get_prop_int(dev, "baud-rate", &cfg->uart_baud);
        }
    }

    /* ── &spi0: DTS 覆盖 ── */
    {
        device_t* dev = device_find_by_label("spi0");
        if (dev)
        {
            device_get_prop_int(dev, "reg",         &cfg->spi_host);
            device_get_prop_int(dev, "mosi",        &cfg->spi_mosi);
            device_get_prop_int(dev, "miso",        &cfg->spi_miso);
            device_get_prop_int(dev, "sclk",        &cfg->spi_sclk);
            device_get_prop_int(dev, "cs-gpios",    &cfg->spi_cs);
            device_get_prop_int(dev, "clock-speed", &cfg->spi_clock);
            device_get_prop_int(dev, "mode",        &cfg->spi_mode);
        }
    }

    /* ── &dma0: DTS 覆盖 ── */
    {
        device_t* dev = device_find_by_label("dma0");
        if (dev)
        {
            device_get_prop_int(dev, "reg",      &cfg->dma_id);
            device_get_prop_int(dev, "channel",  &cfg->dma_ch);
            device_get_prop_int(dev, "priority", &cfg->dma_prio);
        }
    }

    /* ── &pwm0: DTS 覆盖 ── */
    {
        device_t* dev = device_find_by_label("pwm0");
        if (dev)
        {
            device_get_prop_int(dev, "reg",          &cfg->timer_id);
            device_get_prop_int(dev, "clock-freq",   &cfg->timer_freq);
            device_get_prop_int(dev, "period-us",    &cfg->timer_period);
            device_get_prop_int(dev, "default-pulse",&cfg->timer_pulse);
        }
    }
}

/* ── 外设实例 (静态分配) ── */
static hal_uart_t        s_uart;
static hal_spi_bus_t     s_spi;
static hal_dma_chan_t    s_dma_tx;
static hal_dma_chan_t    s_dma_rx;
static hal_timer_t       s_timer;

/* ── 内部辅助: RCU 时钟使能 (GD32 rcu_periph_clock_enable 封装) ── */
static int periph_clock_enable(void)
{
    /* GD32: 使能各外设时钟 (固定操作, 直写) */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_SPI0);
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_TIMER1);

    rcu_usart_clock_config(0, RCU_USART_SRC_PCLK);

    return 0;
}

/* ── 内部辅助: GPIO 引脚配置 (使用 cfg 中的引脚号) ── */
static int gpio_config_uart_pins(const board_dts_cfg_t* cfg)
{
    /* cfg->uart_tx / cfg->uart_rx 来自 DTS */
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  (uint32_t)(1U << cfg->uart_tx));
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            (uint32_t)(1U << cfg->uart_tx));
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP,
                  (uint32_t)(1U << cfg->uart_rx));
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            (uint32_t)(1U << cfg->uart_rx));

    gpio_af_set(GPIOA, GPIO_AF_1, (uint32_t)(1U << cfg->uart_tx));
    gpio_af_set(GPIOA, GPIO_AF_1, (uint32_t)(1U << cfg->uart_rx));
    return 0;
}

static int gpio_config_spi_pins(const board_dts_cfg_t* cfg)
{
    /* cfg->spi_mosi / spi_miso / spi_sclk / spi_cs 来自 DTS */
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE,
                  (uint32_t)(1U << cfg->spi_sclk) |
                  (uint32_t)(1U << cfg->spi_miso) |
                  (uint32_t)(1U << cfg->spi_mosi));
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            (uint32_t)(1U << cfg->spi_sclk) |
                            (uint32_t)(1U << cfg->spi_mosi));
    gpio_af_set(GPIOB, GPIO_AF_0,
                (uint32_t)(1U << cfg->spi_sclk) |
                (uint32_t)(1U << cfg->spi_miso) |
                (uint32_t)(1U << cfg->spi_mosi));

    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_PULLUP,
                  (uint32_t)(1U << cfg->spi_cs));
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ,
                            (uint32_t)(1U << cfg->spi_cs));
    gpio_bit_set(GPIOB, (uint32_t)(1U << cfg->spi_cs));
    return 0;
}

int board_periph_init(void)
{
    board_dts_cfg_t dt_cfg;
    board_dts_load(&dt_cfg);
    int ret;

    /* ── 步骤 1: 使能外设时钟 ── */
    ret = periph_clock_enable();
    if (ret) goto err_clk;

    /* ── 步骤 2: GPIO 引脚配置 (使用 DTS 引脚号) ── */
    ret = gpio_config_uart_pins(&dt_cfg);
    if (ret) goto err_gpio_uart;

    ret = gpio_config_spi_pins(&dt_cfg);
    if (ret) goto err_gpio_spi;

    /* ── 步骤 3: UART 初始化 (使用 dt_cfg 来自 DTS) ── */
    {
        hal_uart_config_t uart_cfg = {
            .uart_id   = dt_cfg.uart_id,
            .tx_pin    = dt_cfg.uart_tx,
            .rx_pin    = dt_cfg.uart_rx,
            .baud_rate = dt_cfg.uart_baud,
            .data_bits = 8,
            .stop_bits = 1,
            .parity    = 0,
        };
        hal_uart_init_struct(&s_uart);
        ret = s_uart.init(&s_uart, &uart_cfg);
        if (ret) goto err_uart;
    }

    /* ── 步骤 4: SPI 总线初始化 (使用 dt_cfg 来自 DTS) ── */
    {
        hal_spi_bus_config_t bus_cfg = {
            .host_id         = dt_cfg.spi_host,
            .mosi            = dt_cfg.spi_mosi,
            .miso            = dt_cfg.spi_miso,
            .sclk            = dt_cfg.spi_sclk,
            .max_transfer_sz = 4096,
            .dma_chan        = dt_cfg.dma_id,
        };
        hal_spi_device_config_t dev_cfg = {
            .mode            = dt_cfg.spi_mode,
            .clock_speed_hz  = dt_cfg.spi_clock,
            .cs_pin          = dt_cfg.spi_cs,
            .queue_size      = 4,
        };
        hal_spi_bus_init_struct(&s_spi);
        ret = s_spi.init(&s_spi, &bus_cfg, &dev_cfg);
        if (ret) goto err_spi;
    }

    /* ── 步骤 5: DMA TX 通道 ── */
    {
        hal_dma_config_t dma_cfg = {
            .dma_id    = dt_cfg.dma_id,
            .dir       = HAL_DMA_DIR_MEM_TO_PERIPH,
            .src_width = HAL_DMA_WIDTH_BYTE,
            .dst_width = HAL_DMA_WIDTH_BYTE,
            .src_inc   = HAL_DMA_INC_INCREMENT,
            .dst_inc   = HAL_DMA_INC_FIXED,
            .priority  = dt_cfg.dma_prio,
            .cir_mode  = 0,
            .irq_enable = 1,
        };
        hal_dma_init_struct(&s_dma_tx);
        ret = s_dma_tx.init(&s_dma_tx, &dma_cfg);
        if (ret) goto err_dma_tx;
    }

    /* ── 步骤 6: DMA RX 通道 ── */
    {
        hal_dma_config_t dma_cfg = {
            .dma_id    = dt_cfg.dma_id,
            .dir       = HAL_DMA_DIR_PERIPH_TO_MEM,
            .src_width = HAL_DMA_WIDTH_BYTE,
            .dst_width = HAL_DMA_WIDTH_BYTE,
            .src_inc   = HAL_DMA_INC_FIXED,
            .dst_inc   = HAL_DMA_INC_INCREMENT,
            .priority  = 1,
            .cir_mode  = 0,
            .irq_enable = 1,
        };
        hal_dma_init_struct(&s_dma_rx);
        ret = s_dma_rx.init(&s_dma_rx, &dma_cfg);
        if (ret) goto err_dma_rx;
    }

    /* ── 步骤 7: 定时器 / PWM ── */
    {
        hal_timer_config_t timer_cfg = {
            .timer_id    = dt_cfg.timer_id,
            .freq_hz     = dt_cfg.timer_freq,
            .mode        = HAL_TIMER_MODE_PERIODIC,
            .period_us   = dt_cfg.timer_period,
            .dead_time_ns = 0,
        };
        hal_timer_init_struct(&s_timer);
        ret = s_timer.init(&s_timer, &timer_cfg);
        if (ret) goto err_timer;

        hal_timer_channel_config_t ch_cfg = {
            .channel  = 0,
            .mode     = HAL_TIMER_CH_PWM,
            .pulse    = dt_cfg.timer_pulse,
            .polarity = 0,
        };
        ret = s_timer.config_channel(&s_timer, &ch_cfg);
        if (ret) goto err_timer_ch;

        ret = s_timer.start(&s_timer);
        if (ret) goto err_timer_start;
    }

    /* ── 全部初始化成功 ── */
    return 0;

    /* ══════════════════════════════════════════════════════════
     * goto 多级回退: 每级只释放已成功初始化的资源
     * 标签按初始化逆序排列
     * ══════════════════════════════════════════════════════════ */
err_timer_start:
    s_timer.deinit(&s_timer);
err_timer_ch:
    s_timer.deinit(&s_timer);
err_timer:
    s_dma_rx.deinit(&s_dma_rx);
err_dma_rx:
    s_dma_tx.deinit(&s_dma_tx);
err_dma_tx:
    s_spi.deinit(&s_spi);
err_spi:
    s_uart.deinit(&s_uart);
err_uart:
err_gpio_spi:
err_gpio_uart:
err_clk:
    return ret;
}
