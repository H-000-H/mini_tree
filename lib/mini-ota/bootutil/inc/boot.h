/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot.h
 * @author: H-000-H
 */

#ifndef BOOTUTIL_INC_BOOT_H
#define BOOTUTIL_INC_BOOT_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include <stdint.h>
typedef struct mini_boot_app_area mini_boot_app_area_t ;
struct mini_boot_app_area
{
    uint32_t initial_addr;
    uint32_t size;
    uint32_t fa_id;     /**< 目标分区 flash_area_id_t；仅 image_len != 0 时用于整包校验 */
    uint32_t image_len; /**< 镜像实际长度（含末尾 meta）；0 = 跳过整包校验，只查向量表 */
};

/**
 * @brief 校验 app 向量表并跳转过去（正常不返回）
 * @param app_area 目标分区的起始地址与大小；image_len 非 0 时额外做整包校验
 * @return 0 成功（正常不返回）；负数为失败原因（见 err.h）
 * @note  跳哪个分区由调用方决定（按 ota_current_partition_get() 自行组装 app_area），
 *        因此不需要单独的"强制跳转"入口；向量表非法/整包校验不过时直接返回，不会硬跳
 * @note  整包校验可选：填 image_len（及 fa_id）即启用，内部走 mini_boot_backup()
 *        （加密镜像需先经 boot_keys.h 的 boot_key_set() 装入密钥）；不填则退化为旧行为
 */
int boot_jump_switch_app(mini_boot_app_area_t app_area);
#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_BOOT_H */
