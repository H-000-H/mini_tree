/* SPI 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 板级引脚在 board *.dts &spi1 { } / &spi_dev0 { } 中覆盖.
 * STM32: SPI1 host_id = 1
 * 注意: 仅供 dtc-lite 预处理, 勿加 #ifndef guard (会破坏宏展开).
 */

#define SPI_DEFAULT_HOST_ID           1
#define SPI_DEFAULT_MAX_FREQUENCY_HZ  10000000
#define SPI_DEFAULT_MODE              0
#define SPI_DEFAULT_BITS_PER_WORD     8
#define SPI_DEFAULT_QUEUE_SIZE        4
#define SPI_DEFAULT_DMA_CHAN          (-1)
