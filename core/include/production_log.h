#ifndef PRODUCTION_LOG_H
#define PRODUCTION_LOG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "compiler_compat.h"

#ifdef __cplusplus
extern "C" 
{
#endif

#define PROD_LOG_SLOT_COUNT 32
#define PROD_LOG_TAG_LEN    8
#define PROD_LOG_MSG_LEN    112

typedef enum
{
    PROD_LOG_ERROR = 0,
    PROD_LOG_WARN  = 1,
    PROD_LOG_INFO  = 2,
} prod_log_level_t;

struct prod_log_entry

{
    uint32_t seq;
    uint32_t timestamp;
    uint8_t  level;
    char     tag[PROD_LOG_TAG_LEN];
    char     msg[PROD_LOG_MSG_LEN];
};

int  production_log_init(void);

void production_log_push(prod_log_level_t level, const char* tag, const char* msg);

void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
    COMPAT_FMT_PRINTF(3, 4);

int  production_log_count(void);

const struct prod_log_entry* production_log_get(int index);

void production_log_dump(void (*sink)(const char* line));

#ifdef __cplusplus
}
#endif

#endif
