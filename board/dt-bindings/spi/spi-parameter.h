/* SPDX-License-Identifier: Apache-2.0 */
/* SPI 默认参数 (dt-bindings, 仅供 dtsi #include <dt-bindings/...> 引用)
 *
 * 只放 #define 常量, 不写设备节点.
 * 板级引脚/频率在平台 board *.dts 的 &spiN / client 节点中覆盖.
 * 注意: 仅供 dtc-lite 预处理, 勿加 #ifndef guard (会破坏宏展开).
 */
#define SPI_DEFAULT_HOST_ID           1
#define SPI_DEFAULT_MAX_FREQUENCY_HZ  10000000
#define SPI_DEFAULT_MODE              0
#define SPI_DEFAULT_BITS_PER_WORD     8
#define SPI_DEFAULT_QUEUE_SIZE        4
