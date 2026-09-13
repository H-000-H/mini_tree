/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: verify.h
 * @brief: 纯数据校验 — 不解析镜像头 / 尾部 meta, 不用密钥, 不区分打包模式
 * @details
 *   与 read.h 的 image_verify_stream 的分工:
 *     image_verify_stream : 必须先解析镜像末尾 meta 才能知道打包模式与 aux 长度, 属"镜像校验"
 *     image_verify_raw    : 完全不看头尾, 输入是"一段连续数据 + 期望 CRC", 属"数据校验"
 *   CRC 模型固定取 boot_config.h 的 CRC_MODEL_*(默认标准 CRC-32), 与
 *   tools/post_build_crc.py 的 zlib.crc32 及 lib/mini-ota/tools/m_crc/image_crc.py 同源。
 *   典型用途: 构建期基线比对 (位翻转 / 位腐烂巡检)、boot 跳转前自检、运行期巡检。
 *   无动态内存、无静态状态, bootloader 与 app 均可调用。
 *
 *   注意: 期望值必须与"扫描范围"同源 —— CRC 和长度都要来自同一次构建的同一个
 *   二进制文件; 长度口径与扫描范围不一致时基线永远对不上。
 */
#ifndef BOOTUTIL_INC_VERIFY_H
#define BOOTUTIL_INC_VERIFY_H

#include <stdint.h>

#include "err.h"
#include "read.h" /* image_read_fn */

#if defined(__cplusplus)
extern "C" {
#endif

/** 分块校验的块大小 (栈上缓冲); 越大越快, 可编译期覆盖 */
#ifndef IMAGE_VERIFY_CHUNK
#define IMAGE_VERIFY_CHUNK 256u
#endif

/**
 * @brief: 纯数据校验 (介质无关): 流式读取 len 字节算 CRC, 与期望值比对
 * @param read_fn      [in]: 读取回调 (offset 相对本段起点), 不可为 NULL
 * @param read_ctx     [in]: 回调上下文, 可为 NULL
 * @param len          [in]: 本段字节数; 为 0 返回 ERR_ARG
 * @param expected_crc [in]: 期望 CRC (模型见 boot_config.h 的 CRC_MODEL_*)
 * @return: ERR_OK 一致; ERR_CRC_MISMATCH 不一致; ERR_ARG 入参非法;
 *          其余为 read_fn 返回的错误码
 */
int image_verify_raw(image_read_fn read_fn, void *read_ctx, uint32_t len, uint32_t expected_crc);

/**
 * @brief: 纯数据校验 (flash 区域便利版): 对区域 [off, off+len) 做同样的数据校验
 * @param area_id      [in]: flash_area_id_t: flash 区域
 * @param off          [in]: 区域内偏移
 * @param len          [in]: 字节数
 * @param expected_crc [in]: 期望 CRC
 * @return: 同 image_verify_raw; 区域未注册时返回 ERR_NOT_SUPPORTED
 */
int image_verify_area(uint32_t area_id, uint32_t off, uint32_t len, uint32_t expected_crc);

#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_VERIFY_H */
