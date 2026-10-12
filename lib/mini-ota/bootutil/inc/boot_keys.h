/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 * @file boot_keys.h
 * @brief OTA 密钥槽：GCM/CBC/CBC_SHA 加密模式的密钥中转与用后清零。
 *        密钥只经 boot_key_set() 拷入内部静态缓冲，校验/解密完成后 wipe，
 *        避免密钥常驻 RAM，也避免密钥参数穿调用链扩散到应用层；
 *        密钥来源（出厂 flash 槽位 / eFuse / 调试默认值）由调用方决定，本模块不关心
 * @author H-000-H
 * @note  缓冲本体是 boot_keys.c 内 static：符号不导出，外部无法 extern 触碰；
 *        CBC_SHA 暂未提供独立 MAC 密钥槽，read 层会回退用同一把密钥（见 read.h）
 * @note  仅在 IMAGE_CRYPTO_ENABLE=1 时编入：关闭加密时密钥槽整体不存在（含声明），
 *        这样“不编加密”的固件连一把密钥都不碰，也不依赖 mbedtls
 */
#ifndef BOOTUTIL_INC_BOOT_KEYS_H
#define BOOTUTIL_INC_BOOT_KEYS_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include <stddef.h>
#include <stdint.h>
#include "boot_config.h" /* IMAGE_CRYPTO_ENABLE */

#if IMAGE_CRYPTO_ENABLE

/**
 * @brief 装入密钥（拷贝进内部静态缓冲，覆盖旧值）
 * @param key [in] 密钥，不可为 NULL
 * @param len [in] 密钥长度，仅允许 16/24/32 字节（AES-128/192/256）
 * @return ERR_OK 成功；ERR_ARG 入参非法
 */
int boot_key_set(const uint8_t *key, size_t len);

/**
 * @brief 取当前密钥（指向内部静态缓冲，仅在下次 set/wipe 前有效）
 * @param out_len [out] 密钥长度（字节），可为 NULL
 * @return 密钥指针；未装入时返回 NULL（CRC/SHA 模式无需密钥，可直接忽略）
 */
const uint8_t *boot_key_get(size_t *out_len);

/**
 * @brief 清零密钥槽（防优化清零；校验/解密结束后无论成败都应调用）
 */
void boot_key_wipe(void);

#else /* !IMAGE_CRYPTO_ENABLE */

#if defined(__GNUC__)
int boot_key_set(const uint8_t *key, size_t len)
    __attribute__((error("boot_key_set: 需要 IMAGE_CRYPTO_ENABLE=1（当前为 0，密钥槽未编入）")));
const uint8_t *boot_key_get(size_t *out_len)
    __attribute__((error("boot_key_get: 需要 IMAGE_CRYPTO_ENABLE=1（当前为 0，密钥槽未编入）")));
void boot_key_wipe(void)
    __attribute__((error("boot_key_wipe: 需要 IMAGE_CRYPTO_ENABLE=1（当前为 0，密钥槽未编入）")));
#endif

#endif /* IMAGE_CRYPTO_ENABLE */

#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_BOOT_KEYS_H */
