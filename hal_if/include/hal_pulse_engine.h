#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── 协议脉冲引擎接口 ──
 *
 * 纯硬件抽象：向外发射一串精确时序的控制脉冲。
 * 可用于 WS2812/NeoPixel、红外遥控编码、单总线协议等，
 * 不绑定任何具体 MCU 或外设。
 */

typedef struct
{
    uint32_t carrier_freq_hz;   /* 脉冲载波频率 (0 = 无载波) */
    uint32_t bit_time_ns;       /* 单 bit 时间基准 (纳秒) */
} hal_pulse_config_t;

/* ── 发送脉冲序列 ──
 * engine_id:  脉冲引擎实例编号 (0 ~ N-1)
 * data:       待发送的数据缓冲区
 * len:        数据长度 (字节)
 * 返回 0 成功, -1 失败.
 */
#ifdef __cplusplus
extern "C" {
#endif

int hal_pulse_engine_send(int engine_id, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif
