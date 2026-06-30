/* SPDX-License-Identifier: Apache-2.0 */
/*
 * config_store.h — 键值配置存储头文件
 *
 * 声明 bool/int/float/string 四类配置的 get/set API 与 commit 持久化接口.
 * 提供 config_store_bind_source 绑定 JSON 工厂默认值缓冲区 (init 前调用).
 * 支持 factory_reset 恢复出厂与 register_write_hook 注入持久化后端.
 */
#ifndef CONFIG_STORE_H
#define CONFIG_STORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" 
{
#endif

/*
 * 绑定 JSON 配置缓冲区基地址.
 * 必须在 config_store_init() 之前调用.
 * 移植到新平台时, 传入 embedded 的 system_config.json 地址及其大小.
 */
void config_store_bind_source(const char* json_buffer, size_t size);

bool config_store_init(void);

bool config_store_get_bool(const char* key, bool default_value);
int  config_store_get_int(const char* key, int default_value);
float config_store_get_float(const char* key, float default_value);
const char* config_store_get_string(const char* key, const char* default_value);

bool config_store_set_bool(const char* key, bool value);
bool config_store_set_int(const char* key, int value);
bool config_store_set_float(const char* key, float value);
bool config_store_set_string(const char* key, const char* value);

bool config_store_commit(void);

bool config_store_factory_reset(void);

int config_store_health(void);

/* ── 持久化后端回调桥接器 ──
 * 用户工程通过此函数注入底层存储的读写能力。
 * 若不注册，config_store_commit() 将使用默认的 hal_storage 路径。
 *
 * 用法:
 *   static bool my_write(const uint8_t* data, size_t len)
 {
 *       return my_flash_write(0x1000, data, len);
 *   }
 *   config_store_register_write_hook(my_write);
 */
typedef bool (*config_store_write_hook_t)(const uint8_t* data, size_t len);
void config_store_register_write_hook(config_store_write_hook_t hook);

#ifdef __cplusplus
}
#endif

#endif
