/*
 * hal_init_stm32.c — STM32 多级初始化参考 (4 种开发风格 + 设备树适配)
 *
 * 展示同一套初始化逻辑的 4 种写法, 全部包含 goto 多级错误回退,
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
 *       tx-pin    = <9>;        /* USART1_TX (PA9)  */
 *       rx-pin    = <10>;       /* USART1_RX (PA10) */
 *       baud-rate = <115200>;
 *   };
 *
 *   spi0: spi@0 {
 *       compatible = "myplatform,spi-bus";
 *       reg = <0>;
 *       mosi        = <7>;      /* SPI1_MOSI (PA7) */
 *       miso        = <6>;      /* SPI1_MISO (PA6) */
 *       sclk        = <5>;      /* SPI1_SCK  (PA5) */
 *       cs-gpios    = <4>;      /* SPI1_CS   (PA4) */
 *       clock-speed = <8000000>;
 *       mode        = <0>;
 *   };
 *
 *   dma0: dma@0 {
 *       compatible = "myplatform,dma";
 *       reg = <0>;
 *       stream   = <5>;         /* DMA2 Stream5 */
 *       channel  = <3>;         /* Channel 3    */
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
 * fixed_clock_enable() 和 gpio_af_mode() 这类不变硬件操作
 * 直接在各 style 中硬编码, 不经过 DTS.
 * ══════════════════════════════════════════════════════════════════
 *
 * 目标芯片: STM32F4xx (UART1=PA9/PA10, SPI1=PA5/PA6/PA7, TIM2=PA0)
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
 * 3. 各 style 函数调用 board_dts_load() 获取最终配置
 * ═══════════════════════════════════════════════════════════════════════ */

/* ── 默认配置 (基节点: 无 DTS 时的回退值) ── */
#define DTS_DEF_UART_ID       0
#define DTS_DEF_UART_TX_PIN   9
#define DTS_DEF_UART_RX_PIN   10
#define DTS_DEF_UART_BAUD     115200

#define DTS_DEF_SPI_HOST      0
#define DTS_DEF_SPI_MOSI      7
#define DTS_DEF_SPI_MISO      6
#define DTS_DEF_SPI_SCLK      5
#define DTS_DEF_SPI_CS        4
#define DTS_DEF_SPI_CLOCK     8000000
#define DTS_DEF_SPI_MODE      0

#define DTS_DEF_DMA_ID        0
#define DTS_DEF_DMA_STREAM    5
#define DTS_DEF_DMA_CH        3
#define DTS_DEF_DMA_PRIO      2

#define DTS_DEF_TIMER_ID      2
#define DTS_DEF_TIMER_FREQ    100000000
#define DTS_DEF_TIMER_PERIOD  10000
#define DTS_DEF_TIMER_PULSE   5000

/* ── 运行时 DTS 解析结果 ── */
typedef struct
{
    int uart_id, uart_tx, uart_rx, uart_baud;
    int spi_host, spi_mosi, spi_miso, spi_sclk, spi_cs, spi_clock, spi_mode;
    int dma_id, dma_stream, dma_ch, dma_prio;
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

    cfg->dma_id     = DTS_DEF_DMA_ID;
    cfg->dma_stream = DTS_DEF_DMA_STREAM;
    cfg->dma_ch     = DTS_DEF_DMA_CH;
    cfg->dma_prio   = DTS_DEF_DMA_PRIO;

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
            device_get_prop_int(dev, "stream",   &cfg->dma_stream);
            device_get_prop_int(dev, "channel",  &cfg->dma_ch);
            device_get_prop_int(dev, "priority", &cfg->dma_prio);
        }
    }

    /* ── &pwm0: DTS 覆盖 ── */
    {
        device_t* dev = device_find_by_label("pwm0");
        if (dev)
        {
            device_get_prop_int(dev, "reg",         &cfg->timer_id);
            device_get_prop_int(dev, "clock-freq",  &cfg->timer_freq);
            device_get_prop_int(dev, "period-us",   &cfg->timer_period);
            device_get_prop_int(dev, "default-pulse", &cfg->timer_pulse);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * 方式 1: HAL 库 (STM32Cube HAL)
 *
 * 特点: 初始化结构体 + 超时机制 + 句柄, 代码最冗长但可读性好
 * ═══════════════════════════════════════════════════════════════════ */
#if defined(HAL_EXAMPLE)

static UART_HandleTypeDef   s_uart_hal;
static SPI_HandleTypeDef    s_spi_hal;
static DMA_HandleTypeDef    s_dma_tx_hal;
static TIM_HandleTypeDef    s_timer_hal;

int board_init_hal(void)
{
    board_dts_cfg_t cfg;
    board_dts_load(&cfg);                     /* &uart0, &spi0, &dma0, &pwm0 覆盖 */
    int ret;

    /* 步骤 1: 时钟使能 (固定操作, 直写) */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();

    /* 步骤 2: GPIO (HAL_GPIO_Init) */
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.Pin       = (uint16_t)((1U << cfg.uart_tx) | (1U << cfg.uart_rx));
        gpio.Mode      = GPIO_MODE_AF_PP;
        gpio.Pull      = GPIO_PULLUP;
        gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
        gpio.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(GPIOA, &gpio);
    }

    /* 步骤 3: UART (使用 cfg.uart_baud 来自 DTS) */
    {
        s_uart_hal.Instance        = USART1;
        s_uart_hal.Init.BaudRate   = cfg.uart_baud;
        s_uart_hal.Init.WordLength = UART_WORDLENGTH_8B;
        s_uart_hal.Init.StopBits   = UART_STOPBITS_1;
        s_uart_hal.Init.Parity     = UART_PARITY_NONE;
        s_uart_hal.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
        s_uart_hal.Init.Mode       = UART_MODE_TX_RX;

        if (HAL_UART_Init(&s_uart_hal) != HAL_OK)
        {
            ret = -1;
            goto err_uart_hal;
        }
    }

    /* 步骤 4: SPI (使用 cfg.spi_* 来自 DTS) */
    {
        s_spi_hal.Instance               = SPI1;
        s_spi_hal.Init.Mode              = SPI_MODE_MASTER;
        s_spi_hal.Init.Direction         = SPI_DIRECTION_2LINES;
        s_spi_hal.Init.DataSize          = SPI_DATASIZE_8BIT;
        s_spi_hal.Init.CLKPolarity       = cfg.spi_mode & 1 ? SPI_POLARITY_HIGH : SPI_POLARITY_LOW;
        s_spi_hal.Init.CLKPhase          = cfg.spi_mode & 2 ? SPI_PHASE_2EDGE : SPI_PHASE_1EDGE;
        s_spi_hal.Init.NSS               = SPI_NSS_SOFT;
        s_spi_hal.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;
        s_spi_hal.Init.FirstBit          = SPI_FIRSTBIT_MSB;

        if (HAL_SPI_Init(&s_spi_hal) != HAL_OK)
        {
            ret = -2;
            goto err_spi_hal;
        }
    }

    /* 步骤 5: DMA TX */
    {
        s_dma_tx_hal.Instance                 = DMA2_Stream5;
        s_dma_tx_hal.Init.Channel             = (uint32_t)cfg.dma_ch;
        s_dma_tx_hal.Init.Direction           = DMA_MEMORY_TO_PERIPH;
        s_dma_tx_hal.Init.PeriphInc           = DMA_PINC_DISABLE;
        s_dma_tx_hal.Init.MemInc              = DMA_MINC_ENABLE;
        s_dma_tx_hal.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
        s_dma_tx_hal.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
        s_dma_tx_hal.Init.Mode                = DMA_NORMAL;
        s_dma_tx_hal.Init.Priority            = cfg.dma_prio == 2 ? DMA_PRIORITY_HIGH : DMA_PRIORITY_MEDIUM;

        if (HAL_DMA_Init(&s_dma_tx_hal) != HAL_OK)
        {
            ret = -3;
            goto err_dma_tx_hal;
        }

        __HAL_LINKDMA(&s_spi_hal, hdmatx, s_dma_tx_hal);
    }

    /* 步骤 6: TIM2 PWM (使用 cfg.timer_* 来自 DTS) */
    {
        s_timer_hal.Instance               = TIM2;
        s_timer_hal.Init.Prescaler         = (uint32_t)(cfg.timer_freq / 100000 - 1);
        s_timer_hal.Init.CounterMode       = TIM_COUNTERMODE_UP;
        s_timer_hal.Init.Period            = (uint32_t)((cfg.timer_period * 100) - 1);
        s_timer_hal.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;

        if (HAL_TIM_PWM_Init(&s_timer_hal) != HAL_OK)
        {
            ret = -4;
            goto err_timer_hal;
        }

        TIM_OC_InitTypeDef oc = {0};
        oc.OCMode     = TIM_OCMODE_PWM1;
        oc.Pulse      = (uint32_t)(cfg.timer_pulse);
        oc.OCPolarity = TIM_OCPOLARITY_HIGH;

        if (HAL_TIM_PWM_ConfigChannel(&s_timer_hal, &oc, TIM_CHANNEL_1) != HAL_OK)
        {
            ret = -5;
            goto err_timer_ch_hal;
        }

        if (HAL_TIM_PWM_Start(&s_timer_hal, TIM_CHANNEL_1) != HAL_OK)
        {
            ret = -6;
            goto err_timer_start_hal;
        }
    }

    return 0;

err_timer_start_hal:
    HAL_TIM_PWM_DeInit(&s_timer_hal);
err_timer_ch_hal:
    HAL_TIM_PWM_DeInit(&s_timer_hal);
err_timer_hal:
    HAL_DMA_DeInit(&s_dma_tx_hal);
err_dma_tx_hal:
    HAL_SPI_DeInit(&s_spi_hal);
err_spi_hal:
    HAL_UART_DeInit(&s_uart_hal);
err_uart_hal:
    return ret;
}

#endif /* HAL_EXAMPLE */

/* ═══════════════════════════════════════════════════════════════════
 * 方式 2: LL 库 (STM32Cube LL)
 *
 * 特点: 寄存器封装 + 内联函数, 代码简洁, 性能接近寄存器直写
 * ═══════════════════════════════════════════════════════════════════ */
#if defined(LL_EXAMPLE)

int board_init_ll(void)
{
    board_dts_cfg_t cfg;
    board_dts_load(&cfg);
    int ret = 0;  /* LL 风格无资源分配, 仅用于一致性 */

    /* 步骤 1: 时钟使能 */
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA
                             | LL_AHB1_GRP1_PERIPH_DMA2);

    /* 步骤 2: GPIO */
    LL_GPIO_SetPinMode(GPIOA, (uint32_t)cfg.uart_tx, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinSpeed(GPIOA, (uint32_t)cfg.uart_tx, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(GPIOA, (uint32_t)cfg.uart_tx, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetAFPin_0_7(GPIOA, (uint32_t)cfg.uart_tx, LL_GPIO_AF_7);
    LL_GPIO_SetPinMode(GPIOA, (uint32_t)cfg.uart_rx, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetAFPin_0_7(GPIOA, (uint32_t)cfg.uart_rx, LL_GPIO_AF_7);

    /* 步骤 3: UART (使用 cfg.uart_baud 来自 DTS) */
    {
        LL_USART_Disable(USART1);
        LL_USART_SetBaudRate(USART1, SystemCoreClock, (uint32_t)cfg.uart_baud);
        LL_USART_SetDataWidth(USART1, LL_USART_DATAWIDTH_8B);
        LL_USART_SetStopBits(USART1, LL_USART_STOPBITS_1);
        LL_USART_SetParity(USART1, LL_USART_PARITY_NONE);
        LL_USART_SetHWFlowCtrl(USART1, LL_USART_HWCONTROL_NONE);
        LL_USART_SetTransferDirection(USART1, LL_USART_DIRECTION_TX_RX);
        LL_USART_Enable(USART1);
    }

    /* 步骤 4: SPI */
    {
        LL_SPI_Disable(SPI1);
        LL_SPI_SetMode(SPI1, LL_SPI_MODE_MASTER);
        LL_SPI_SetDataWidth(SPI1, LL_SPI_DATAWIDTH_8BIT);
        LL_SPI_SetClockPolarity(SPI1, cfg.spi_mode & 1 ? LL_SPI_POLARITY_HIGH : LL_SPI_POLARITY_LOW);
        LL_SPI_SetClockPhase(SPI1, cfg.spi_mode & 2 ? LL_SPI_PHASE_2EDGE : LL_SPI_PHASE_1EDGE);
        LL_SPI_SetNSSMode(SPI1, LL_SPI_NSS_SOFT);
        LL_SPI_SetBaudRatePrescaler(SPI1, LL_SPI_BAUDRATEPRESCALER_8);
        LL_SPI_SetTransferBitOrder(SPI1, LL_SPI_MSB_FIRST);
        LL_SPI_Enable(SPI1);
    }

    /* 步骤 5: DMA TX */
    {
        LL_DMA_SetChannelSelection(DMA2, LL_DMA_STREAM_5, (uint32_t)cfg.dma_ch);
        LL_DMA_SetDataTransferDirection(DMA2, LL_DMA_STREAM_5,
                                        LL_DMA_DIRECTION_MEMORY_TO_PERIPH);
        LL_DMA_SetMemoryIncMode(DMA2, LL_DMA_STREAM_5, LL_DMA_MEMORY_INCREMENT);
        LL_DMA_SetPeriphIncMode(DMA2, LL_DMA_STREAM_5, LL_DMA_PERIPH_NOINCREMENT);
        LL_DMA_SetDataSize(DMA2, LL_DMA_STREAM_5,
                           LL_DMA_PDATAALIGN_BYTE, LL_DMA_MDATAALIGN_BYTE);
        LL_DMA_SetMode(DMA2, LL_DMA_STREAM_5, LL_DMA_MODE_NORMAL);
        LL_DMA_SetStreamPriority(DMA2, LL_DMA_STREAM_5,
                                 cfg.dma_prio == 2 ? LL_DMA_PRIORITY_HIGH : LL_DMA_PRIORITY_MEDIUM);
        LL_DMA_EnableStream(DMA2, LL_DMA_STREAM_5);
    }

    /* 步骤 6: TIM2 PWM */
    {
        LL_TIM_SetPrescaler(TIM2, (uint32_t)(cfg.timer_freq / 100000 - 1));
        LL_TIM_SetCounterMode(TIM2, LL_TIM_COUNTERMODE_UP);
        LL_TIM_SetAutoReload(TIM2, (uint32_t)((cfg.timer_period * 100) - 1));
        LL_TIM_OC_SetMode(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_OCMODE_PWM1);
        LL_TIM_OC_SetCompareCH1(TIM2, (uint32_t)cfg.timer_pulse);
        LL_TIM_OC_SetPolarity(TIM2, LL_TIM_CHANNEL_CH1, LL_TIM_OCPOLARITY_HIGH);
        LL_TIM_EnableCounter(TIM2);
        LL_TIM_CC_EnableChannel(TIM2, LL_TIM_CHANNEL_CH1);
    }

    return ret;
}

#endif /* LL_EXAMPLE */

/* ═══════════════════════════════════════════════════════════════════
 * 方式 3: 标准外设库 (STM32 SPL — Standard Peripheral Library)
 *
 * 特点: 命名规范类似 HAL 但更轻薄, 无句柄依赖, 直接操作外设
 * ═══════════════════════════════════════════════════════════════════ */
#if defined(SPL_EXAMPLE)

int board_init_spl(void)
{
    board_dts_cfg_t cfg;
    board_dts_load(&cfg);
    int ret = 0;

    /* 步骤 1: 时钟使能 */
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_DMA2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_SPI1, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    /* 步骤 2: GPIO */
    {
        GPIO_InitTypeDef gpio = {0};
        gpio.GPIO_Pin   = (uint16_t)((1U << cfg.uart_tx) | (1U << cfg.uart_rx));
        gpio.GPIO_Mode  = GPIO_Mode_AF;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        gpio.GPIO_OType = GPIO_OType_PP;
        gpio.GPIO_PuPd  = GPIO_PuPd_UP;
        GPIO_Init(GPIOA, &gpio);

        GPIO_PinAFConfig(GPIOA, (uint16_t)cfg.uart_tx, GPIO_AF_USART1);
        GPIO_PinAFConfig(GPIOA, (uint16_t)cfg.uart_rx, GPIO_AF_USART1);
    }

    /* 步骤 3: UART (使用 cfg.uart_baud) */
    {
        USART_InitTypeDef uart = {0};
        uart.USART_BaudRate            = cfg.uart_baud;
        uart.USART_WordLength          = USART_WordLength_8b;
        uart.USART_StopBits            = USART_StopBits_1;
        uart.USART_Parity              = USART_Parity_No;
        uart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        uart.USART_Mode                = USART_Mode_Tx | USART_Mode_Rx;

        USART_Init(USART1, &uart);
        USART_Cmd(USART1, ENABLE);
    }

    /* 步骤 4: SPI */
    {
        SPI_InitTypeDef spi = {0};
        spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
        spi.SPI_Mode              = SPI_Mode_Master;
        spi.SPI_DataSize          = SPI_DataSize_8b;
        spi.SPI_CPOL              = cfg.spi_mode & 1 ? SPI_CPOL_High : SPI_CPOL_Low;
        spi.SPI_CPHA              = cfg.spi_mode & 2 ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
        spi.SPI_NSS               = SPI_NSS_Soft;
        spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
        spi.SPI_FirstBit          = SPI_FirstBit_MSB;

        SPI_Init(SPI1, &spi);
        SPI_Cmd(SPI1, ENABLE);
    }

    /* 步骤 5: DMA */
    {
        DMA_InitTypeDef dma = {0};
        dma.DMA_Channel            = (uint32_t)cfg.dma_ch;
        dma.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
        dma.DMA_Memory0BaseAddr    = 0;
        dma.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
        dma.DMA_BufferSize         = 0;
        dma.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
        dma.DMA_MemoryInc          = DMA_MemoryInc_Enable;
        dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
        dma.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
        dma.DMA_Mode               = DMA_Mode_Normal;
        dma.DMA_Priority           = cfg.dma_prio == 2 ? DMA_Priority_High : DMA_Priority_Medium;
        dma.DMA_FIFOMode           = DMA_FIFOMode_Disable;

        DMA_Init(DMA2_Stream5, &dma);
        DMA_Cmd(DMA2_Stream5, ENABLE);
    }

    /* 步骤 6: TIM2 PWM */
    {
        TIM_TimeBaseInitTypeDef tb = {0};
        tb.TIM_Prescaler         = (uint16_t)(cfg.timer_freq / 100000 - 1);
        tb.TIM_CounterMode       = TIM_CounterMode_Up;
        tb.TIM_Period            = (uint16_t)((cfg.timer_period * 100) - 1);
        tb.TIM_ClockDivision     = TIM_CKD_DIV1;
        TIM_TimeBaseInit(TIM2, &tb);

        TIM_OCInitTypeDef oc = {0};
        oc.TIM_OCMode     = TIM_OCMode_PWM1;
        oc.TIM_Pulse      = (uint16_t)cfg.timer_pulse;
        oc.TIM_OCPolarity = TIM_OCPolarity_High;
        TIM_OC1Init(TIM2, &oc);

        TIM_Cmd(TIM2, ENABLE);
    }

    return ret;
}

#endif /* SPL_EXAMPLE */

/* ═══════════════════════════════════════════════════════════════════
 * 方式 4: 寄存器直写 (CMSIS — Cortex Microcontroller Software Interface Standard)
 *
 * 特点: 无库依赖, 直接操作外设寄存器, 性能最高但需对照手册
 * 包含完整的 goto 多级错误回退
 * ═══════════════════════════════════════════════════════════════════ */
#if defined(REG_EXAMPLE)

static hal_uart_t        s_uart;
static hal_spi_bus_t     s_spi;

int board_init_reg(void)
{
    board_dts_cfg_t cfg;
    board_dts_load(&cfg);
    int ret;

    /* ── 步骤 1: RCC 时钟使能寄存器 (固定操作) ── */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_DMA2EN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_SPI1EN;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)__DSB();

    /* ── 步骤 2: GPIO 复用寄存器 ── */
    {
        uint32_t moder_mask = 0;
        uint32_t afr_val    = 0;
        if (cfg.uart_tx <= 7)
        {
            moder_mask |= (3U << (cfg.uart_tx * 2));
            afr_val    |= (7U << (cfg.uart_tx * 4));
        }
        if (cfg.uart_rx <= 7)
        {
            moder_mask |= (3U << (cfg.uart_rx * 2));
            afr_val    |= (7U << (cfg.uart_rx * 4));
        }
        GPIOA->MODER  &= ~moder_mask;
        GPIOA->MODER  |= (moder_mask & 0xAAAAAAAA);  /* AF = 10b per pin */
        GPIOA->AFR[0] |= afr_val;
    }

    /* ── 步骤 3: UART (使用 cfg.uart_baud) ── */
    {
        hal_uart_config_t uart_cfg = {
            .uart_id   = cfg.uart_id,
            .tx_pin    = cfg.uart_tx,
            .rx_pin    = cfg.uart_rx,
            .baud_rate = cfg.uart_baud,
            .data_bits = 8,
            .stop_bits = 1,
            .parity    = 0,
        };
        hal_uart_init_struct(&s_uart);
        ret = s_uart.init(&s_uart, &uart_cfg);
        if (ret) goto err_uart_reg;
    }

    /* ── 步骤 4: SPI ── */
    {
        hal_spi_bus_config_t bus_cfg = {
            .host_id         = cfg.spi_host,
            .mosi            = cfg.spi_mosi,
            .miso            = cfg.spi_miso,
            .sclk            = cfg.spi_sclk,
            .max_transfer_sz = 4096,
            .dma_chan        = cfg.dma_id,
        };
        hal_spi_device_config_t dev_cfg = {
            .mode            = cfg.spi_mode,
            .clock_speed_hz  = cfg.spi_clock,
            .cs_pin          = cfg.spi_cs,
            .queue_size      = 4,
        };
        hal_spi_bus_init_struct(&s_spi);
        ret = s_spi.init(&s_spi, &bus_cfg, &dev_cfg);
        if (ret) goto err_spi_reg;
    }

    /* ── 步骤 5: DMA 寄存器 ── */
    {
        DMA2_Stream5->CR   = 0;
        DMA2_Stream5->CR  |= (uint32_t)((cfg.dma_ch & 7) << 25);  /* CHSEL */
        DMA2_Stream5->CR  |= DMA_SxCR_DIR_0;
        DMA2_Stream5->CR  |= DMA_SxCR_MINC;
        if (cfg.dma_prio >= 2)
            DMA2_Stream5->CR |= DMA_SxCR_PL_1;
        DMA2_Stream5->PAR  = (uint32_t)&SPI1->DR;
        DMA2_Stream5->M0AR = 0;
        DMA2_Stream5->NDTR = 0;
        DMA2_Stream5->CR  |= DMA_SxCR_EN;

        if (!(DMA2_Stream5->CR & DMA_SxCR_EN))
        {
            ret = -3;
            goto err_dma_reg;
        }
    }

    /* ── 步骤 6: TIM2 寄存器 ── */
    {
        TIM2->PSC  = (uint32_t)(cfg.timer_freq / 100000 - 1);
        TIM2->ARR  = (uint32_t)((cfg.timer_period * 100) - 1);
        TIM2->CNT  = 0;

        TIM2->CCMR1 |= TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1;
        TIM2->CCMR1 |= TIM_CCMR1_OC1PE;
        TIM2->CCER  |= TIM_CCER_CC1E;
        TIM2->CCR1  = (uint32_t)cfg.timer_pulse;

        if (TIM2->ARR != (uint32_t)((cfg.timer_period * 100) - 1) ||
            TIM2->CCR1 != (uint32_t)cfg.timer_pulse)
        {
            ret = -4;
            goto err_timer_reg;
        }

        TIM2->CR1 |= TIM_CR1_CEN;
    }

    return 0;

err_timer_reg:
    DMA2_Stream5->CR = 0;
err_dma_reg:
    s_spi.deinit(&s_spi);
err_spi_reg:
    s_uart.deinit(&s_uart);
err_uart_reg:
    return ret;
}

#endif /* REG_EXAMPLE */

/* ═══════════════════════════════════════════════════════════════════
 * 写法对比总结
 *
 * | 特性        | HAL 库          | LL 库        | SPL 库       | 寄存器直写   |
 * |------------|-----------------|-------------|-------------|------------|
 * | 代码量      | 最多            | 中等         | 中等         | 最少         |
 * | 可读性      | 最好 (结构体直观) | 好 (函数名规范) | 好           | 差 (需手册)  |
 * | 性能        | 最慢 (超时+断言)  | 快 (内联封装)  | 中等         | 最快         |
 * | 库依赖      | STM32Cube HAL   | STM32Cube LL | SPL v3.5+   | 仅 CMSIS   |
 * | 调试友好度   | 高 (句柄可查看)   | 中            | 中           | 低 (看寄存器) |
 * | 可移植性     | 仅 STM32        | 仅 STM32     | 仅 STM32    | 可迁移其他ARM |
 * | goto 回退   | 每步 HAL_DeInit  | 无 (LL 无资源) | 无 (SPL 少资源) | 逐级 DMA/DeInit |
 * | DTS 适配    | board_dts_load  | board_dts_load | board_dts_load | board_dts_load |
 * ═══════════════════════════════════════════════════════════════════ */
