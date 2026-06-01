#include "production_log.h"

#include "config.h"
#include "osal.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ═══════════════════════════════════════════════════════════════════
 * CONFIG_PRODUCTION_LOG — 启用时使用 ESP-IDF NVS 实现
 * ═══════════════════════════════════════════════════════════════════ */
#ifdef CONFIG_PRODUCTION_LOG

#include "nvs_flash.h"
#include "nvs.h"

#define NVS_NS          "prod_log"
#define NVS_KEY_RING    "ring"
#define NVS_KEY_HEAD    "head"
#define NVS_KEY_SEQ     "seq"
#define RING_BLOB_SIZE  (PROD_LOG_SLOT_COUNT * sizeof(prod_log_entry_t))

static prod_log_entry_t s_ring[PROD_LOG_SLOT_COUNT];
static uint16_t         s_head = 0;
static uint32_t         s_seq  = 0;
static bool             s_ready = false;

int production_log_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) return -1;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return -2;

    size_t len = RING_BLOB_SIZE;
    err = nvs_get_blob(h, NVS_KEY_RING, s_ring, &len);
    if (err != ESP_OK)
    {
        memset(s_ring, 0, sizeof(s_ring));
    }

    uint16_t head = 0;
    if (nvs_get_u16(h, NVS_KEY_HEAD, &head) == ESP_OK)
        s_head = head;

    uint32_t seq = 0;
    if (nvs_get_u32(h, NVS_KEY_SEQ, &seq) == ESP_OK)
        s_seq = seq;

    nvs_close(h);
    s_ready = true;
    return 0;
}

void production_log_push(prod_log_level_t level, const char* tag, const char* msg)
{
    if (!s_ready) return;

    /* 仅写入环形缓冲区 (ISR 安全), NVS 持久化在 ISR 中跳过 */
    prod_log_entry_t* e = &s_ring[s_head];
    e->seq       = s_seq++;
    e->timestamp = 0;
    e->level     = (uint8_t)level;

    strncpy(e->tag, tag ? tag : "", PROD_LOG_TAG_LEN - 1);
    e->tag[PROD_LOG_TAG_LEN - 1] = '\0';

    strncpy(e->msg, msg ? msg : "", PROD_LOG_MSG_LEN - 1);
    e->msg[PROD_LOG_MSG_LEN - 1] = '\0';

    uint16_t next = (s_head + 1) % PROD_LOG_SLOT_COUNT;
    s_head = next;

    /* NVS 操作非 ISR 安全, ISR 中跳过持久化 */
    if (osal_in_isr()) return;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;

    nvs_set_blob(h, NVS_KEY_RING, s_ring, RING_BLOB_SIZE);
    nvs_set_u16(h, NVS_KEY_HEAD, s_head);
    nvs_set_u32(h, NVS_KEY_SEQ, s_seq);
    nvs_commit(h);
    nvs_close(h);
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
        if (s_ring[i].seq == 0 && s_ring[i].level == 0 && s_ring[i].msg[0] == '\0')
            return i;
    }
    return PROD_LOG_SLOT_COUNT;
}

const prod_log_entry_t* production_log_get(int index)
{
    if (index < 0 || index >= PROD_LOG_SLOT_COUNT) return NULL;
    return &s_ring[index];
}

void production_log_dump(void (*sink)(const char* line))
{
    if (!sink) return;

    char buf[256];
    sink("=== PRODUCTION LOG DUMP ===");

    int oldest = s_head;
    for (int i = 0; i < PROD_LOG_SLOT_COUNT; i++)
    {
        int idx = (oldest + i) % PROD_LOG_SLOT_COUNT;
        const prod_log_entry_t* e = &s_ring[idx];
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

#else /* !CONFIG_ESP_NVS — 空实现 (移植到新平台需提供 backend) */

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