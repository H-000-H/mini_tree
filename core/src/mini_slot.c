/**
 * @file mini_slot.c
 * @author H-000-H
 * @brief 槽位池实现 (线程/中断安全的定长索引分配器)
 * @note 临界区策略按后端分发:
 * @note - ESP_PLATFORM        : 池内嵌 portMUX_TYPE (mini_slot_t.mux_storage)
 * @note - CONFIG_OS_FREERTOS: taskENTER_CRITICAL / taskEXIT_CRITICAL (非 ESP)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */

#include "mini_slot.h"

#include "mini_critical.h"

#if defined(ESP_PLATFORM)
#include "hal_amp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#elif defined(CONFIG_OS_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

/* -------------------------------------------------------------------------- */
/* 临界区 (按后端分发)                                                         */
/* -------------------------------------------------------------------------- */
#if defined(ESP_PLATFORM)
/**
 * @brief ESP 平台: 池内嵌 portMUX_TYPE, 任务与 ISR 分别走对应入口
 */
_Static_assert(sizeof(portMUX_TYPE) <= MINI_SLOT_MUX_STORAGE_SIZE, "MINI_SLOT_MUX_STORAGE_SIZE too small for portMUX_TYPE");

typedef uint32_t mini_slot_irq_t;

MINI_STATIC_INLINE portMUX_TYPE* mini_slot_mux(mini_slot_t* pool) { return (portMUX_TYPE*)pool->mux_storage; }

MINI_STATIC_INLINE mini_slot_irq_t mini_slot_enter(mini_slot_t* pool)
{
    portMUX_TYPE* mux = mini_slot_mux(pool);
    if (hal_is_in_isr())
        portENTER_CRITICAL_ISR(mux);
    else
        taskENTER_CRITICAL(mux);
    return 0U;
}

MINI_STATIC_INLINE void mini_slot_exit(mini_slot_t* pool, mini_slot_irq_t irq)
{
    MINI_UNUSED_PARAM(irq);
    portMUX_TYPE* mux = mini_slot_mux(pool);
    if (hal_is_in_isr())
        portEXIT_CRITICAL_ISR(mux);
    else
        taskEXIT_CRITICAL(mux);
}

MINI_STATIC_INLINE void mini_slot_mux_init(mini_slot_t* pool) { portMUX_INITIALIZE(mini_slot_mux(pool)); }

#elif defined(CONFIG_OS_FREERTOS)
/**
 * @brief 非 ESP FreeRTOS: 内核嵌套临界区 (port 内部自带嵌套计数, 无需存储)
 */
typedef uint32_t mini_slot_irq_t;

MINI_STATIC_INLINE mini_slot_irq_t mini_slot_enter(mini_slot_t* pool)
{
    MINI_UNUSED_PARAM(pool);
    taskENTER_CRITICAL();
    return 0U;
}

MINI_STATIC_INLINE void mini_slot_exit(mini_slot_t* pool, mini_slot_irq_t irq)
{
    MINI_UNUSED_PARAM(pool);
    MINI_UNUSED_PARAM(irq);
    taskEXIT_CRITICAL();
}

MINI_STATIC_INLINE void mini_slot_mux_init(mini_slot_t* pool) { MINI_UNUSED_PARAM(pool); }

#else
/**
 * @brief 裸机 / mini-os / RT-Thread: 可嵌套关中断
 */
typedef mini_irq_state_t mini_slot_irq_t;

MINI_STATIC_INLINE mini_slot_irq_t mini_slot_enter(mini_slot_t* pool)
{
    MINI_UNUSED_PARAM(pool);
    return mini_critical_enter();
}

MINI_STATIC_INLINE void mini_slot_exit(mini_slot_t* pool, mini_slot_irq_t irq)
{
    MINI_UNUSED_PARAM(pool);
    mini_critical_exit(irq);
}

MINI_STATIC_INLINE void mini_slot_mux_init(mini_slot_t* pool) { MINI_UNUSED_PARAM(pool); }

#endif /* 临界区分发 */

/* -------------------------------------------------------------------------- */
/* 槽位池 API                                                                  */
/* -------------------------------------------------------------------------- */

mt_err_t mini_slot_init(mini_slot_t* pool, volatile uint8_t* used_slots, size_t slot_count)
{
    if (!pool || !used_slots || slot_count == 0)
        return MINI_ERR_INVAL;

    pool->used_slots = used_slots;
    pool->slot_count = slot_count;

    for (size_t iter_index = 0; iter_index < slot_count; iter_index++)
        used_slots[iter_index] = 0;

    mini_slot_mux_init(pool);

    return MINI_OK;
}

int mini_slot_claim(mini_slot_t* pool)
{
    if (!pool || !pool->used_slots || pool->slot_count == 0)
        return MINI_ERR_INVAL;

    mini_slot_irq_t irq = mini_slot_enter(pool);

    int claimed_index = -1;
    for (size_t iter_index = 0; iter_index < pool->slot_count; iter_index++)
    {
        if (!pool->used_slots[iter_index])
        {
            pool->used_slots[iter_index] = 1;
            claimed_index = (int)iter_index;
            break;
        }
    }

    mini_slot_exit(pool, irq);
    return claimed_index;
}

mt_err_t mini_slot_release(mini_slot_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return MINI_ERR_INVAL;

    mini_slot_irq_t irq = mini_slot_enter(pool);
    pool->used_slots[slot_index] = 0;
    mini_slot_exit(pool, irq);
    return MINI_OK;
}

bool mini_slot_is_used(mini_slot_t* pool, int slot_index)
{
    if (!pool || !pool->used_slots || slot_index < 0 || (size_t)slot_index >= pool->slot_count)
        return false;
    return pool->used_slots[slot_index] != 0U;
}
