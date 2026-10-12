/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file log_config.h
 * @brief Configuration options for the mini logging system.
 */
#ifndef LOG_CONFIG_H
#define LOG_CONFIG_H

#if defined(__has_include)
#  if __has_include("config.h")
#    include "config.h"
#  endif
#endif

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * 打印最大缓冲区长度
 */
#if defined(CONFIG_MINI_LOG_MAX_LEN)
#define MINI_LOG_MAX_LEN CONFIG_MINI_LOG_MAX_LEN
#elif defined(MINI_LOG_MAX_LEN)
/* 使用外部已定义的 MINI_LOG_MAX_LEN */
#else
#define MINI_LOG_MAX_LEN 128
#endif

/**
 * 控制台日志环容量 (字节, 须为 2 的幂)
 */
#if defined(CONFIG_MINI_LOG_RING_SIZE)
#define MINI_LOG_RING_SIZE CONFIG_MINI_LOG_RING_SIZE
#elif defined(MINI_LOG_RING_SIZE)
/* 使用外部已定义的 MINI_LOG_RING_SIZE */
#else
#define MINI_LOG_RING_SIZE 1024u
#endif

#if (MINI_LOG_RING_SIZE & (MINI_LOG_RING_SIZE - 1)) != 0
#error "MINI_LOG_RING_SIZE must be a power of two"
#endif

/**
 * 自动刷新配置(异步开关)
 */
#if defined(CONFIG_MINI_LOG_AUTO_FLUSH)
#define MINI_LOG_AUTO_FLUSH CONFIG_MINI_LOG_AUTO_FLUSH
#elif defined(CONFIG_SYS_LOG_USE_MINI_LOG)
/* 已接入 Kconfig 且选 mini-log: bool 关闭时生成器不输出宏, 视为 0 */
#define MINI_LOG_AUTO_FLUSH 0
#elif defined(MINI_LOG_AUTO_FLUSH)
/* 使用外部已定义的 MINI_LOG_AUTO_FLUSH */
#else
#define MINI_LOG_AUTO_FLUSH 1
#endif

#if defined(CONFIG_MINI_LOG_COLOR_ENABLE)
#define MINI_LOG_COLOR_ENABLE CONFIG_MINI_LOG_COLOR_ENABLE
#elif defined(CONFIG_SYS_LOG_USE_MINI_LOG)
#define MINI_LOG_COLOR_ENABLE 0
#elif defined(MINI_LOG_COLOR_ENABLE)
/* 使用外部已定义的 MINI_LOG_COLOR_ENABLE */
#else
#define MINI_LOG_COLOR_ENABLE 1
#endif

#if defined(CONFIG_MINI_LOG_DEFAULT_ALIGIN)
#define MINI_LOG_DEFAULT_ALIGIN CONFIG_MINI_LOG_DEFAULT_ALIGIN
#elif defined(MINI_LOG_DEFAULT_ALIGIN)
/* 使用外部已定义的 MINI_LOG_DEFAULT_ALIGIN */
#else
#define MINI_LOG_DEFAULT_ALIGIN 4
#endif

#if defined(CONFIG_MINI_LOG_MAGIC)
#define MINI_LOG_MAGIC CONFIG_MINI_LOG_MAGIC
#elif defined(MINI_LOG_MAGIC)
/* 使用外部已定义的 MINI_LOG_MAGIC */
#else
#define MINI_LOG_MAGIC 4
#endif

#if defined(CONFIG_MINI_LOG_FLASH_RING_SIZE)
#define MINI_LOG_FLASH_RING_SIZE CONFIG_MINI_LOG_FLASH_RING_SIZE
#elif defined(MINI_LOG_FLASH_RING_SIZE)
/* 使用外部已定义的 MINI_LOG_FLASH_RING_SIZE */
#else
#define MINI_LOG_FLASH_RING_SIZE 512u
#endif

#if ((MINI_LOG_FLASH_RING_SIZE & (MINI_LOG_FLASH_RING_SIZE - 1)) != 0) || \
    ((MINI_LOG_DEFAULT_ALIGIN & (MINI_LOG_DEFAULT_ALIGIN - 1)) != 0)
#error "MINI_LOG_FLASH_RING_SIZE and MINI_LOG_DEFAULT_ALIGIN must be powers of two"
#endif

#if defined(CONFIG_MINI_LOG_USE_FLASH)
#define MINI_LOG_USE_FLASH CONFIG_MINI_LOG_USE_FLASH
#elif defined(CONFIG_SYS_LOG_USE_MINI_LOG)
#define MINI_LOG_USE_FLASH 0
#elif defined(MINI_LOG_USE_FLASH)
/* 使用外部已定义的 MINI_LOG_USE_FLASH */
#else
#define MINI_LOG_USE_FLASH 1
#endif

/**
 * @brief flash 暂存环自动落盘开关 (类比 MINI_LOG_AUTO_FLUSH)
 */
#if defined(CONFIG_MINI_LOG_FLASH_AUTO_FLUSH)
#define MINI_LOG_FLASH_AUTO_FLUSH CONFIG_MINI_LOG_FLASH_AUTO_FLUSH
#elif defined(CONFIG_SYS_LOG_USE_MINI_LOG)
#define MINI_LOG_FLASH_AUTO_FLUSH 0
#elif defined(MINI_LOG_FLASH_AUTO_FLUSH)
/* 使用外部已定义的 MINI_LOG_FLASH_AUTO_FLUSH */
#else
#define MINI_LOG_FLASH_AUTO_FLUSH 0
#endif

/**
 * @brief flash 记录帧的 CRC 模型 (默认 CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, 不反射, xor 0x0000)
 */
#ifndef MINI_LOG_FRAME_CRC_WIDTH
#define MINI_LOG_FRAME_CRC_WIDTH 16u
#endif
#ifndef MINI_LOG_FRAME_CRC_POLY
#define MINI_LOG_FRAME_CRC_POLY 0x1021u
#endif
#ifndef MINI_LOG_FRAME_CRC_INIT
#define MINI_LOG_FRAME_CRC_INIT 0xFFFFu
#endif
#ifndef MINI_LOG_FRAME_CRC_REFIN
#define MINI_LOG_FRAME_CRC_REFIN 0
#endif
#ifndef MINI_LOG_FRAME_CRC_REFOUT
#define MINI_LOG_FRAME_CRC_REFOUT 0
#endif
#ifndef MINI_LOG_FRAME_CRC_XOR_OUT
#define MINI_LOG_FRAME_CRC_XOR_OUT 0x0000u
#endif
#ifdef __cplusplus
}
#endif // __cplusplus

#endif // LOG_CONFIG_H
