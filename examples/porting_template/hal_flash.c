/*
 * hal_flash.c — Flash 读取移植模板
 *
 * 提供对包含固件镜像的 Flash 分区的读取访问。
 * 由位腐烂检测器 (scrubber) 使用，定期对运行中的镜像进行 CRC 校验。
 */

#include "hal_flash.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

bool hal_flash_read(uint32_t addr, uint8_t* buf, size_t len)
{
    /*
     * TODO: 从给定绝对地址读取 flash。
     *
     * ARM + 外部 flash:
     *   memcpy(buf, (void*)addr, len);
     *   return true;
     *
     * ESP-IDF:
     *   return esp_flash_read(NULL, buf, addr, len) == ESP_OK;
     *
     * POSIX (模拟):
     *   FILE* f = fopen("flash.bin", "rb");
     *   fseek(f, addr, SEEK_SET);
     *   size_t r = fread(buf, 1, len, f);
     *   fclose(f);
     *   return r == len;
     */
    (void)addr;
    (void)buf;
    (void)len;
    return false;
}

uint32_t hal_flash_get_app_addr(void)
{
    /*
     * TODO: 返回固件分区在 flash 中的起始地址。
     *
     * ARM IAP:
     *   return FLASH_BASE;  // 或特定扇区
     *
     * ESP-IDF:
     *   const esp_partition_t* p = esp_ota_get_running_partition();
     *   return p->address;
     *
     * POSIX:
     *   return 0;
     */
    return 0;
}

uint32_t hal_flash_get_app_size(void)
{
    /*
     * TODO: 返回固件分区的总大小（字节）。
     */
    return 0;
}
