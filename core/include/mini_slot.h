/**
 * @file mini_slot.h
 * @author H-000-H
 * @brief 槽位池 (线程/中断安全的定长索引分配器)
 * @note mini_slot — 定长索引分配器 (槽位位图)。注意它**不是内存分配器**:
 * @note 只负责在调用方提供的 used_slots[] 位图里 claim / release 一个下标,
 * @note 不分配任何字节。要分配字节请用统一接口的堆接口 (mini_malloc)。
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#ifndef MINI_SLOT_H
#define MINI_SLOT_H

#include "compiler_compat.h"
#include "status.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef MINI_SLOT_MUX_STORAGE_SIZE
#define MINI_SLOT_MUX_STORAGE_SIZE 16 /**< ESP 平台内嵌 portMUX_TYPE 的存储字节数 */
#endif

/**
 * @brief 槽位池描述符
 */
typedef struct mini_slot
{
    volatile uint8_t* used_slots;                              /**< 已占用槽位位图 (调用方提供, 每槽 1 字节) */
    size_t            slot_count;                              /**< 槽位数量 */
    uint8_t           mux_storage[MINI_SLOT_MUX_STORAGE_SIZE]; /**< ESP 平台临界区锁存储; 其它后端预留不使用 */
} mini_slot_t;

/**
 * @brief 初始化槽位池 (清零位图, 使之可在任务/中断中安全 claim/release)
 * @param[in] pool 槽位池指针
 * @param[in] used_slots 已占用位图指针 (由调用方提供, 长度须 >= slot_count)
 * @param[in] slot_count 槽位数量
 * @return 成功返回 MINI_OK; 参数非法返回 MINI_ERR_INVAL
 */
mt_err_t mini_slot_init(mini_slot_t* pool, volatile uint8_t* used_slots, size_t slot_count) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 申请一个空闲槽位
 * @param[in] pool 槽位池指针
 * @return 成功返回槽位下标 (>= 0); 参数非法或池满返回负错误码
 */
int mini_slot_claim(mini_slot_t* pool) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 释放指定槽位
 * @param[in] pool 槽位池指针
 * @param[in] slot_index 槽位下标
 * @return 成功返回 MINI_OK; 下标越界返回 MINI_ERR_INVAL
 */
mt_err_t mini_slot_release(mini_slot_t* pool, int slot_index) MINI_WARN_UNUSED_RESULT;

/**
 * @brief 查询槽位是否被占用
 * @param[in] pool 槽位池指针
 * @param[in] slot_index 槽位下标
 * @return 已占用返回 true; 参数非法或未占用返回 false
 */
bool mini_slot_is_used(mini_slot_t* pool, int slot_index);

#ifdef __cplusplus
}
#endif

#endif /* MINI_SLOT_H */
