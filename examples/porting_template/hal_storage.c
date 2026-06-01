/*
 * hal_storage.c — 持久化存储移植模板
 *
 * 双槽 A/B 存储，用于配置数据块。
 * 由 config_store.c 使用，实现安全的原子更新。
 */

#include "hal_storage.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

bool hal_storage_init(void)
{
    /*
     * TODO: 初始化存储介质。
     *
     * ESP-IDF (NVS):
     *   esp_err_t e = nvs_flash_init();
     *   return e == ESP_OK || e == ESP_ERR_NVS_NEW_VERSION_FOUND;
     *
     * ARM (EEPROM):
     *   HAL_EEPROM_Init();
     *   return true;
     */
    return false;
}

bool hal_storage_read_flag(uint8_t* flag)
{
    /*
     * TODO: 读取指示活动槽位的单字节元数据标志。
     */
    (void)flag;
    return false;
}

bool hal_storage_write_flag(uint8_t flag)
{
    /*
     * TODO: 写入活动槽位标志字节。
     */
    (void)flag;
    return false;
}

bool hal_storage_read_blob(uint8_t slot, uint8_t* buf, size_t* len)
{
    /*
     * TODO: 从指定槽位（0 或 1）读取配置数据块。
     * 进入时，*len 为缓冲区大小；返回时，*len 为实际数据大小。
     */
    (void)slot;
    (void)buf;
    (void)len;
    return false;
}

bool hal_storage_write_blob(uint8_t slot, const uint8_t* buf, size_t len)
{
    /*
     * TODO: 将配置数据块写入指定槽位。
     */
    (void)slot;
    (void)buf;
    (void)len;
    return false;
}

bool hal_storage_erase_all(void)
{
    /*
     * TODO: 擦除所有存储（两个槽位和标志位）。
     */
    return false;
}
