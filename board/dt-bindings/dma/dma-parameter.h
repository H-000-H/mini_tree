/* STM32 DMA dt-bindings (dtc-lite 预处理用，勿加 #ifndef guard) */

#define DTS_DMA_CTRL_DMA1       1
#define DTS_DMA_CTRL_DMA2       2

#define DTS_DMA_STREAM_0        0
#define DTS_DMA_STREAM_1        1
#define DTS_DMA_STREAM_2        2
#define DTS_DMA_STREAM_3        3
#define DTS_DMA_STREAM_4        4
#define DTS_DMA_STREAM_5        5
#define DTS_DMA_STREAM_6        6
#define DTS_DMA_STREAM_7        7

#define DTS_DMA_CH_0            0
#define DTS_DMA_CH_1            1
#define DTS_DMA_CH_2            2
#define DTS_DMA_CH_3            3
#define DTS_DMA_CH_4            4
#define DTS_DMA_CH_5            5
#define DTS_DMA_CH_6            6
#define DTS_DMA_CH_7            7

/* DMA 通道 reg：HAL 查找 ID（与 dtsi dma@ 节点 reg 一致） */
#define DTS_DMA_ID_SPI1_RX      10
#define DTS_DMA_ID_SPI1_TX      11
#define DTS_DMA_ID_UART4_TX     20

#define HAL_DMA_BOARD_DEFAULT   (-1)
#define HAL_DMA_DISABLED        (-2)
#define HAL_DMA_MIN_BLOCK       32
