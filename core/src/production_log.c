/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file production_log.c
 *@brief production log 实现
 *@author H-000-H
 *@details
 *   Production Log — 量产日志环形缓冲区实现
 *   启用 CONFIG_PRODUCTION_LOG 时以 blob 形式持久化到 hal_storage, 掉电可恢复
 *   ISR 中跳过持久化写 (存储可能阻塞), 仅更新内存环形缓冲
 */

#include "production_log.h"

#include "config.h"
#include "hal_storage.h"
#include "osal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "compiler_compat_poison.h"

/* -------------------------------------------------------------------------- */
/* CONFIG_PRODUCTION_LOG — 启用时通过 hal_storage 持久化 */
/* -------------------------------------------------------------------------- */
#ifdef CONFIG_PRODUCTION_LOG

/* 持久化快照: 将环形缓冲区 + 元数据打包为单个 blob */
#define PROD_LOG_STORAGE_SLOT 0

struct prod_log_persist

{
    uint16_t head; /**< 环形缓冲头指针 */
    uint32_t seq; /**< 全局序列号 */
    struct prod_log_entry ring[PROD_LOG_SLOT_COUNT]; /**< 日志环形缓冲 */
};

static struct prod_log_persist s_state;
static bool s_ready = false;

/**
 * @brief init 并从 storage 恢复
 * @return 0
 */
int production_log_init(void)
{
    COMPAT_IGNORE_RESULT(hal_storage_init());

    COMPAT_MEM_SET(&s_state, 0, sizeof(s_state));
    size_t len = sizeof(s_state);
    COMPAT_IGNORE_RESULT(hal_storage_read_blob(PROD_LOG_STORAGE_SLOT, (uint8_t*)&s_state, &len));

    s_ready = true;
    return 0;
}

/**
 * @brief 追加日志并持久化
 * @param[in] level 级别
 * @param[in] tag 标签
 * @param[in] msg 消息
 */
void production_log_push(prod_log_level_t level, const char* tag, const char* msg)
{
    if (!s_ready)
        return;

    struct prod_log_entry* entry = &s_state.ring[s_state.head];
    entry->seq = s_state.seq++;
    entry->timestamp = 0;
    entry->level = (uint8_t)level;

    strncpy(entry->tag, tag ? tag : "", PROD_LOG_TAG_LEN - 1);
    entry->tag[PROD_LOG_TAG_LEN - 1] = '\0';

    strncpy(entry->msg, msg ? msg : "", PROD_LOG_MSG_LEN - 1);
    entry->msg[PROD_LOG_MSG_LEN - 1] = '\0';

    s_state.head = (s_state.head + 1) % PROD_LOG_SLOT_COUNT;

    /* ISR 中跳过持久化 (存储操作可能阻塞) */
    if (osal_in_isr())
        return;

    COMPAT_IGNORE_RESULT(
        hal_storage_write_blob(PROD_LOG_STORAGE_SLOT, (const uint8_t*)&s_state, sizeof(s_state)));
}

/**
 * @brief 格式化追加
 * @param[in] level 级别
 * @param[in] tag 标签
 * @param[in] fmt 格式
 * @param ... 参数
 */
void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
{
    char msg[PROD_LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    production_log_push(level, tag, msg);
}

/**
 * @brief 有效条目数
 * @return 数量
 */
int production_log_count(void)
{
    for (int slot_index = 0; slot_index < PROD_LOG_SLOT_COUNT; slot_index++)
        if (s_state.ring[slot_index].seq == 0 && s_state.ring[slot_index].level == 0 &&
            s_state.ring[slot_index].msg[0] == '\0')
            return slot_index;
    return PROD_LOG_SLOT_COUNT;
}

/**
 * @brief 按索引读
 * @param[in] index 索引
 * @return 条目或 NULL
 */
const struct prod_log_entry* production_log_get(int index)
{
    if (index < 0 || index >= PROD_LOG_SLOT_COUNT)
        return NULL;
    return &s_state.ring[index];
}

/**
 * @brief dump 到 sink
 * @param[in] sink 行回调
 */
void production_log_dump(void (*sink)(const char* line))
{
    if (!sink)
        return;

    char buf[256];
    sink("=== PRODUCTION LOG DUMP ===");

    int oldest = s_state.head;
    for (int slot_index = 0; slot_index < PROD_LOG_SLOT_COUNT; slot_index++)
    {
        int idx = (oldest + slot_index) % PROD_LOG_SLOT_COUNT;
        const struct prod_log_entry* entry = &s_state.ring[idx];
        if (entry->seq == 0 && entry->msg[0] == '\0')
            continue;

        const char* lvl_str = "?";
        switch (entry->level)
        {
        case PROD_LOG_ERROR:
            lvl_str = "E";
            break;
        case PROD_LOG_WARN:
            lvl_str = "W";
            break;
        case PROD_LOG_INFO:
            lvl_str = "I";
            break;
        }

        snprintf(buf, sizeof(buf), "[%lu] %s %s: %s", (unsigned long)entry->seq, lvl_str,
                 entry->tag, entry->msg);
        sink(buf);
    }
    sink("=== END ===");
}

#else /* !CONFIG_PRODUCTION_LOG — 空实现 */

/**
 * @brief CONFIG 关闭时的空 init
 * @return 0
 */
int production_log_init(void) { return 0; }

/**
 * @brief stub: 忽略日志写入
 * @param[in] level 级别 (忽略)
 * @param[in] tag 标签 (忽略)
 * @param[in] msg 消息 (忽略)
 */
void production_log_push(prod_log_level_t level, const char* tag, const char* msg)
{
    COMPAT_UNUSED_PARAM(level);
    COMPAT_UNUSED_PARAM(tag);
    COMPAT_UNUSED_PARAM(msg);
}

/**
 * @brief stub: 忽略格式化日志
 * @param[in] level 级别 (忽略)
 * @param[in] tag 标签 (忽略)
 * @param[in] fmt 格式 (忽略)
 * @param ... 参数 (忽略)
 */
void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
{
    COMPAT_UNUSED_PARAM(level);
    COMPAT_UNUSED_PARAM(tag);
    COMPAT_UNUSED_PARAM(fmt);
}

/**
 * @brief stub: 无日志条目
 * @return 0
 */
int production_log_count(void) { return 0; }

/**
 * @brief stub: 无条目可读
 * @param[in] index 索引 (忽略)
 * @return NULL
 */
const struct prod_log_entry* production_log_get(int index)
{
    COMPAT_UNUSED_PARAM(index);
    return NULL;
}

/**
 * @brief stub: 输出占位行
 * @param[in] sink 行回调
 */
void production_log_dump(void (*sink)(const char* line))
{
    if (sink)
        sink("=== PRODUCTION LOG DUMP (stub) ===");
}

#endif /* CONFIG_PRODUCTION_LOG */
