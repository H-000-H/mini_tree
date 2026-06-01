#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

/* ── 板级集中配置 (IEC 61508 §7.4.2.4: 单一配置源) ──
 *
 * 所有静态资源上限、池大小、硬件约束在此统一定义.
 * 驱动文件统一 #include "board_config.h" 获取所有宏,
 * 不得在各自的 .c 文件中魔数硬编码 POOL_SIZE.
 *
 * 来源:
 *   1. dt_config_gen.h  — DTC 编译期扫描 DTS 自动生成 (DTC_GEN_COUNT_xxx)
 *   2. 本文件            — 非 DTS 可推导的全局资源上限
 *
 * 移植到新平台/新产品时, 修改此文件即可, 无需逐个 .c 翻找.
 */

#include "dt_config_gen.h"

/* ═══════════════════════════════════════════════════════════════════
 *  DTS 自动推导的池大小 (由 dtc-lite.py 扫描 compatible 计数生成)
 *  ═══════════════════════════════════════════════════════════════════ */

/* ── 简短别名 (映射到 DTC_GEN_COUNT_xxx) ── */
#define UART_COUNT    DTC_GEN_COUNT_ESP32_UART
#define I2C_COUNT     DTC_GEN_COUNT_ESP32_I2C_BUS
#define SPI_COUNT     DTC_GEN_COUNT_ESP32_SPI_BUS
#define I2S_COUNT     DTC_GEN_COUNT_ESP32_I2S_BUS
#define ADC_COUNT     DTC_GEN_COUNT_ESP32_ADC
#define PWM_COUNT     DTC_GEN_COUNT_ESP32_PWM
#define GPIO_COUNT    DTC_GEN_COUNT_ESP32_GPIO


/* ═══════════════════════════════════════════════════════════════════
 *  全局资源上限 (非 DTS 可推导, 需人工定义)
 *  ═══════════════════════════════════════════════════════════════════ */

/* ── 安全停机 ── */
#define BOARD_MAX_SAFETY_PINS     8
#define BOARD_SAFETY_MAX_CALLBACKS 4

/* ── 栈水位监控 (Stack High Water Mark) ── */
#define BOARD_STACK_MONITOR_MAX_TASKS 8
#define BOARD_STACK_ALARM_RATIO_DEFAULT 15  /* 百分比：当剩余空间不足总栈空间的 15% 时告警 */

/* ── 安全状态 (Safe State) 硬件引脚 ──
 * 宿主工程应在自己的 board_config.h 中覆盖这些值, 例如:
 *   #define BOARD_SAFE_STATE_BUZZER_PIN    15
 *   #define BOARD_SAFE_STATE_FAULT_LED_PIN 2
 */
#ifndef BOARD_SAFE_STATE_BUZZER_PIN
#define BOARD_SAFE_STATE_BUZZER_PIN    0
#endif
#ifndef BOARD_SAFE_STATE_FAULT_LED_PIN
#define BOARD_SAFE_STATE_FAULT_LED_PIN 0
#endif

/* ── OSAL 互斥锁池 (device_t per-device lock + 驱动内部锁) ── */
#define OSAL_MUTEX_POOL_SIZE      24

/* ── 设备树运行时 ── */
#define BOARD_MAX_DEVICE_COUNT    32

/* ── I2C 设备缓存 (per-bus 从设备地址缓存) ── */
#define I2C_DEV_CACHE_SIZE        4

/* ── PWM 硬件通道 ── */
#define PWM_MAX_CHANNELS          8

/* ── Flash 位腐烂巡检 (Bit-Rot Scrubber) ──
 *
 * 块大小与间隔经过 Cache 污染分析:
 *   32 字节/次 × 200ms 间隔 = 160 字节/秒
 *   对于 4MB app 分区 ≈ 7.2 小时完成一次全巡检
 *   每次读取仅 32 字节, 不挤占 I-Cache/D-Cache 热点代码
 */
#define SYSTEM_SCRUBBER_CHUNK_BYTES    32
#define SYSTEM_SCRUBBER_INTERVAL_MS    200
#define SYSTEM_SCRUBBER_CRC_BASELINE   0x00000000  /* 由 post_build_crc.py 自动填充 */

#endif /* BOARD_CONFIG_H */