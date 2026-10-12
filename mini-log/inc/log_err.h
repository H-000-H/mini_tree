/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file log_err.h
 * @brief mini-log 统一错误码定义.
 * @note 成功恒 0、失败恒负, 便于 `if (ret != MINI_LOG_OK)` 统一判断。
 */
#ifndef LOG_ERR_H
#define LOG_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 成功 */
#define MINI_LOG_OK (0)

/** @brief 入参非法: NULL / len==0 / 分区几何不合法 */
#define MINI_LOG_ERR_PARAM (-160)
/** @brief flash 上下文未注册, 或注册校验失败 */
#define MINI_LOG_ERR_NOT_INIT (-161)
/** @brief 单帧超出内部暂存上限或分区容量, 该记录放不下 */
#define MINI_LOG_ERR_TOO_LONG (-162)
/** @brief 底层 open 返回失败 */
#define MINI_LOG_ERR_FLASH_OPEN (-163)
/** @brief 底层 erase 返回失败 */
#define MINI_LOG_ERR_FLASH_ERASE (-164)
/** @brief 底层 write 返回失败 */
#define MINI_LOG_ERR_FLASH_WRITE (-165)
/** @brief 底层 read 返回失败 */
#define MINI_LOG_ERR_FLASH_READ (-166)

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // LOG_ERR_H
