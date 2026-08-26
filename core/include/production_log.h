/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file production_log.h
 *@brief production log 头文件
 *@author H-000-H
 *@details
 *   Production Log — 量产日志环形缓冲区接口
 *   固定 32 槽位, 每条含 level/tag/msg, 用于产线故障现场追溯
 *   启用 CONFIG_PRODUCTION_LOG 时经 hal_storage 持久化, 否则为空实现
 */

#ifndef PRODUCTION_LOG_H
#define PRODUCTION_LOG_H

#include "compiler_compat.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef PROD_LOG_SLOT_COUNT
#ifdef CONFIG_PRODUCTION_LOG_SLOT_COUNT
#define PROD_LOG_SLOT_COUNT CONFIG_PRODUCTION_LOG_SLOT_COUNT
#else
#define PROD_LOG_SLOT_COUNT 32
#endif
#endif
#define PROD_LOG_TAG_LEN 8
#define PROD_LOG_MSG_LEN 112

    typedef enum
    {
        PROD_LOG_ERROR = 0,
        PROD_LOG_WARN = 1,
        PROD_LOG_INFO = 2,
    } prod_log_level_t;

    struct prod_log_entry

    {
        uint32_t seq; /**< 序列号 (单调递增) */
        uint32_t timestamp; /**< 时间戳 (ms) */
        uint8_t level; /**< 日志级别 (PROD_LOG_*) */
        char tag[PROD_LOG_TAG_LEN]; /**< 标签 */
        char msg[PROD_LOG_MSG_LEN]; /**< 消息内容 */
    };

    /**
     * @brief 初始化量产日志环形缓冲 (含持久化后端时恢复历史)
     * @return 成功返回 MINI_OK, 初始化失败返回 VFS_ERR_*
     */
    int production_log_init(void);

    /**
     * @brief 追加一条量产日志
     * @param[in] level 日志级别 (PROD_LOG_*)
     * @param[in] tag 标签 (截断至 PROD_LOG_TAG_LEN)
     * @param[in] msg 消息内容 (截断至 PROD_LOG_MSG_LEN)
     */
    void production_log_push(prod_log_level_t level, const char* tag, const char* msg);

    /**
     * @brief 追加一条格式化量产日志
     * @param[in] level 日志级别 (PROD_LOG_*)
     * @param[in] tag 标签
     * @param[in] fmt printf 格式串
     * @param[in] ... 格式化参数
     */
    void production_log_push_fmt(prod_log_level_t level, const char* tag, const char* fmt, ...)
        MINI_FMT_PRINTF(3, 4);

    /**
     * @brief 查询当前累计日志条数
     * @return 日志条数
     */
    int production_log_count(void);

    /**
     * @brief 按索引读取一条日志 (环形缓冲, 索引取模)
     * @param[in] index 日志索引 (0 起)
     * @return 日志条目指针; 索引越界返回 NULL
     */
    const struct prod_log_entry* production_log_get(int index);

    /**
     * @brief 导出全部日志到回调 (产线现场打印用)
     * @param[in] sink 逐行输出回调
     */
    void production_log_dump(void (*sink)(const char* line));

#ifdef __cplusplus
}
#endif

#endif
