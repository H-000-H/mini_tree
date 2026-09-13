/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: err.h
 * @brief: mini-ota 私有错误码（0 表示成功，负数为具体失败原因）
 * 用法: int rc = image_read_payload(...); if (rc != ERR_OK) { log("%s", err_str(rc)); }
 */
#ifndef MINI_OTA_ERR_H
#define MINI_OTA_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

#define ERR_OK 0

/* ---------- 通用 ---------- */
#define ERR_ARG (-128)           /**< 入参非法：空指针、不支持的模式、长度不对齐 */
#define ERR_TOO_SMALL (-129)     /**< 数据长度不足：小于该布局/字段所需的最小长度 */
#define ERR_OVERFLOW (-130)      /**< 长度计算溢出 */
#define ERR_NOT_SUPPORTED (-131) /**< 功能未编译进来（如未开启加密支持时的 SHA/GCM/CBC） */
#define ERR_UNSUPPORTED (-131)   /**< 旧名，与上同义（保留兼容） */
#define ERR_BUF_TOO_SMALL (-132) /**< 输出缓冲区容量不足 */

/* ---------- 校验/加解密 ---------- */
#define ERR_CRC_MISMATCH (-133)   /**< CRC32 不一致 */
#define ERR_HASH_MISMATCH (-134)  /**< SHA-256 摘要不一致 */
#define ERR_AUTH_FAILED (-135)    /**< 认证失败：GCM tag 或 HMAC 不匹配 */
#define ERR_DECRYPT_FAILED (-136) /**< 解密失败（底层算法库返回错误） */
#define ERR_HASH_FAILED (-137)    /**< 摘要/HMAC 计算失败（底层算法库返回错误） */
#define ERR_PADDING (-138)        /**< PKCS#7 填充非法（CBC 去填充时校验） */
#define ERR_OTA_OPEN (-139)       /**< OTA 未开启 */
#define ERR_TRANSMIT (-140)       /**< 传输错误 */
#define ERR_OTA_STATE (-141)      /**< OTA 持久化状态缺失或损坏 */

/**
 * @brief 错误码转可读字符串
 * @param err [in] 本文件定义的错误码
 * @return 静态字符串，不会为 NULL
 */
const char *err_str(int err);

#ifdef __cplusplus
}
#endif

#endif /* MINI_OTA_ERR_H */
