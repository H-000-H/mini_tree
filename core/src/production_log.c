#include "production_log.h"

#include "config.h"
#include "osal.h"
#include "hal_storage.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════════════════
 * CONFIG_PRODUCTION_LOG — 启用时通过 hal_storage 持久化
 * ═══════════════════════════════════════════════════════════════════ */
#ifdef CONFIG_PRODUCTION_LOG

/* 持久化快照: 将环形缓冲区 + 元数据打包为单个 blob */
#define PROD_LOG_STORAGE_SLOT  0

typedef struct
{
    uint16_t head;
    uint32_t seq;
    prod_log_entry_t ring[PROD_LOG_SLOT_COUNT];
} prod_log_persist_t;

static prod_log_persist_t s_state;
static bool s_ready = false;

int production_log_init(void)
{
    hal_storage_init();

    memset(&s_state, 0, sizeof(s_state));
    size_t len = sizeof(s_state);
    hal_storage_read_blob(PROD_LOG_STORAGE_SLOT, (uint8_t*)&s_state, &len);

    s_ready = true;
    return 0;
}

void production_log_push(prod_log_level_t level, const char* tag, const char* msg)
{
    if (!s_ready) return;

    prod_log_entry_t* e = &s_state.ring[s_state.head];
    e->seq       = s_state.seq++;
    e->timestamp = 0;
    e->level     = (uint8_t)level;

    strncpy(e->tag, tag ? tag : "", PROD_LOG_TAG_LEN - 1);
    e->tag[PROD_LOG_TAG_LEN - 1] = '\0';

    strncpy(e->msg, msg ? msg : "", PROD_LOG_MSG_LEN - 1);
    e->msg[PROD_LOG_MSG_LEN - 1] = '\0';

    s_state.head = (s_state.head + 1) % PROD_LOG_SLOT_COUNT;

    /* ISR 中跳过持久化 (存储操作可能阻塞) */
    if (osal_in_isr()) return;

    hal_storage_write_blob(PROD_LOG_STORAGE_SLOT, (const uint8_t*)&s_state, sizeof(s_state));
}

void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
{
    char msg[PROD_LOG_MSG_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    production_log_push(level, tag, msg);
}

int production_log_count(void)
{
    for (int i = 0; i < PROD_LOG_SLOT_COUNT; i++)
    {
        if (s_state.ring[i].seq == 0 && s_state.ring[i].level == 0 && s_state.ring[i].msg[0] == '\0')
            return i;
    }
    return PROD_LOG_SLOT_COUNT;
}

const prod_log_entry_t* production_log_get(int index)
{
    if (index < 0 || index >= PROD_LOG_SLOT_COUNT) return NULL;
    return &s_state.ring[index];
}

void production_log_dump(void (*sink)(const char* line))
{
    if (!sink) return;

    char buf[256];
    sink("=== PRODUCTION LOG DUMP ===");

    int oldest = s_state.head;
    for (int i = 0; i < PROD_LOG_SLOT_COUNT; i++)
    {
        int idx = (oldest + i) % PROD_LOG_SLOT_COUNT;
        const prod_log_entry_t* e = &s_state.ring[idx];
        if (e->seq == 0 && e->msg[0] == '\0') continue;

        const char* lvl_str = "?";
        switch (e->level)
        {
        case PROD_LOG_ERROR: lvl_str = "E"; break;
        case PROD_LOG_WARN:  lvl_str = "W"; break;
        case PROD_LOG_INFO:  lvl_str = "I"; break;
        }

        snprintf(buf, sizeof(buf), "[%lu] %s %s: %s",
                 (unsigned long)e->seq, lvl_str, e->tag, e->msg);
        sink(buf);
    }
    sink("=== END ===");
}

#else /* !CONFIG_PRODUCTION_LOG — 空实现 */

int production_log_init(void)
{
    return 0;
}

void production_log_push(prod_log_level_t level, const char* tag, const char* msg)
{
    (void)level; (void)tag; (void)msg;
}

void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
{
    (void)level; (void)tag; (void)fmt;
}

int production_log_count(void)
{
    return 0;
}

const prod_log_entry_t* production_log_get(int index)
{
    (void)index;
    return NULL;
}

void production_log_dump(void (*sink)(const char* line))
{
    if (sink) sink("=== PRODUCTION LOG DUMP (stub) ===");
}

#endif /* CONFIG_PRODUCTION_LOG */
