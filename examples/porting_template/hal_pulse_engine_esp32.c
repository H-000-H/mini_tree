/*
 * hal_pulse_engine_esp32.c — ESP32 RMT 参考实现
 *
 * 本文件展示如何使用乐鑫 ESP-IDF 的 RMT 外设实现 hal_pulse_engine.h
 * 接口。移植到其他平台时，替换 rmt_transmit() 为目标芯片的定时器/PWM/IR 外设。
 */

#include "hal_pulse_engine.h"
#include <driver/rmt_tx.h>
#include <esp_log.h>
#include <stdlib.h>

static const char* TAG = "hal_pulse_engine";

/* ── WS2812 时序 (以 bit_time_ns=100ns 为基准) ── */
#define T0H  3   /* 300ns 高电平 → 码 0 */
#define T0L  7   /* 700ns 低电平 */
#define T1H  7   /* 700ns 高电平 → 码 1 */
#define T1L  3   /* 300ns 低电平 */

static rmt_channel_handle_t s_channels[8];
static int s_channel_count = 0;

int hal_pulse_engine_send(int engine_id, const uint8_t* data, size_t len)
{
    if (engine_id < 0 || engine_id >= s_channel_count || !data) return -1;

    rmt_channel_handle_t chan = s_channels[engine_id];

    /* 编码为 RMT symbols (每个 byte → 8 个 symbol, 每个 symbol = {level, duration}) */
    size_t symbol_count = len * 8;
    rmt_symbol_word_t* symbols = malloc(symbol_count * sizeof(rmt_symbol_word_t));
    if (!symbols) return -1;

    for (size_t i = 0; i < len; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            int idx = i * 8 + (7 - bit);
            if (data[i] & (1 << bit)) {
                symbols[idx].level0 = 1; symbols[idx].duration0 = T1H;
                symbols[idx].level1 = 0; symbols[idx].duration1 = T1L;
            } else {
                symbols[idx].level0 = 1; symbols[idx].duration0 = T0H;
                symbols[idx].level1 = 0; symbols[idx].duration1 = T0L;
            }
        }
    }

    rmt_tx_config_t tx_cfg = {
        .loop_count = 0,
        .flags = { .encoding_disable = 0 },
    };

    rmt_transmit_config_t config = {
        .tx_config = tx_cfg,
    };

    esp_err_t ret = rmt_transmit(chan, symbols, symbol_count, &config);
    free(symbols);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "rmt_transmit failed on channel %d", engine_id);
        return -1;
    }

    return 0;
}

/*
 * 初始化示例（由平台 HAL 初始化函数调用）:
 *
 *   rmt_tx_channel_config_t chan_cfg = {
 *       .gpio_num = CONFIG_WS2812_GPIO,
 *       .clk_src = RMT_CLK_SRC_DEFAULT,
 *       .resolution_hz = 10 * 1000 * 1000,  // 10MHz → 100ns 精度
 *       .mem_block_symbols = 64,
 *   };
 *   rmt_new_tx_channel(&chan_cfg, &s_channels[0]);
 *   rmt_enable(s_channels[0]);
 *   s_channel_count = 1;
 */
