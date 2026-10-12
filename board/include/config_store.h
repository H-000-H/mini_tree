/**
 * @file config_store.h
 * @author H-000-H
 * @brief 键值配置存储: bool/int/float/string get/set + commit 持久化
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * @brief 绑定工厂默认 JSON 配置源缓冲区
 * @param[in] json_buffer JSON 缓冲区指针
 * @param[in] size 缓冲区字节数
 * @note 必须在 config_store_init() 之前调用
 */
void config_store_bind_source(const char* json_buffer, size_t size);

/**
 * @brief 初始化配置存储 (解析绑定的 JSON 工厂默认值, 装载当前配置)
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
mt_err_t config_store_init(void);

/**
 * @brief 读取 bool 配置 (key 不存在时返回默认值)
 * @param[in] key 配置键
 * @param[in] default_value 缺省值
 * @return 配置值
 */
bool config_store_get_bool(const char* key, bool default_value);
/**
 * @brief 读取 int 配置 (key 不存在时返回默认值)
 * @param[in] key 配置键
 * @param[in] default_value 缺省值
 * @return 配置值
 */
int config_store_get_int(const char* key, int default_value);
/**
 * @brief 读取 float 配置 (key 不存在时返回默认值)
 * @param[in] key 配置键
 * @param[in] default_value 缺省值
 * @return 配置值
 */
float config_store_get_float(const char* key, float default_value);
/**
 * @brief 读取 string 配置 (key 不存在时返回默认值)
 * @param[in] key 配置键
 * @param[in] default_value 缺省值
 * @return 配置字符串指针; 键不存在且无默认值时返回 NULL
 */
const char* config_store_get_string(const char* key, const char* default_value);

/**
 * @brief 写入 bool 配置 (RAM 中生效, 需 commit 持久化)
 * @param[in] key 配置键
 * @param[in] value 配置值
 * @return MINI_OK 成功; MINI_ERR_INVAL 键无效
 */
mt_err_t config_store_set_bool(const char* key, bool value);
/**
 * @brief 写入 int 配置
 * @param[in] key 配置键
 * @param[in] value 配置值
 * @return MINI_OK 成功; MINI_ERR_INVAL 键无效
 */
mt_err_t config_store_set_int(const char* key, int value);
/**
 * @brief 写入 float 配置
 * @param[in] key 配置键
 * @param[in] value 配置值
 * @return MINI_OK 成功; MINI_ERR_INVAL 键无效
 */
mt_err_t config_store_set_float(const char* key, float value);
/**
 * @brief 写入 string 配置
 * @param[in] key 配置键
 * @param[in] value 配置值 (内部拷贝)
 * @return MINI_OK 成功; MINI_ERR_INVAL 键无效
 */
mt_err_t config_store_set_string(const char* key, const char* value);

/**
 * @brief 将 RAM 中配置持久化到底层存储
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码 (如 MINI_ERR_IO 写入失败)
 */
mt_err_t config_store_commit(void);

/**
 * @brief 恢复出厂配置 (丢弃所有改动, 重载工厂默认值)
 * @return MINI_OK 成功; 失败返回 MINI_ERR_* 负错误码
 */
mt_err_t config_store_factory_reset(void);

/**
 * @brief 查询配置存储健康状态
 * @return 0=健康; 其他=错误码
 */
int config_store_health(void);

/* -------------------------------------------------------------------------- */
/* 持久化后端回调: 不注册则 commit() 走默认 hal_storage 路径 */
typedef int (*config_store_write_hook_t)(const uint8_t* data, size_t len);
/**
 * @brief 注册持久化写入回调 (替代默认 hal_storage 路径)
 * @param[in] hook 写入回调 (可为 NULL 恢复默认)
 */
void config_store_register_write_hook(config_store_write_hook_t hook);

#ifdef __cplusplus
}
#endif

#endif
