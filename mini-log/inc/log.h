/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @author H-000-H
 * @file log.h
 * @brief Logging utilities for the project.
 * @details 两条互不干扰的链路:
 *  - 控制台: MINI_LOG_x       -> mini_log_default_output -> s_ring       -> mini_log_flush       (回调/stdout)
 *  - flash : MINI_LOG_FLASH_x -> mini_log_flash_output   -> s_flash_ring -> mini_log_flash_flush (落盘)
 * @note The flash memory must support sector-based erase operations.
 *  - some flash like stm32f407zgt6 sector not same if using this chip use same sector to make record
 */
#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "log_config.h"
#include "log_err.h"

/*----------------------------------------------------------------------*/
/* Flash 接口抽象                                                        */
/*----------------------------------------------------------------------*/
#if MINI_LOG_USE_FLASH
/**
 * @brief 打开 flash 分区窗口 (其后 erase/write/read 的 offset 均相对该窗口)
 * @param[in] sector_start 分区起始物理地址
 * @param[in] sector_size  单个扇区大小 (字节)
 * @param[in] sector_count 扇区数量
 * @return 0 成功, 非 0 失败
 */
typedef int (*mini_log_flash_open_fn)(uint32_t sector_start, size_t sector_size, uint16_t sector_count);

/**
 * @brief 关闭 flash 分区窗口
 * @return 无
 */
typedef void (*mini_log_flash_close_fn)(void);

/**
 * @brief 按扇区擦除 (必须能按扇区擦除)
 * @param[in] offset 相对分区基址的偏移
 * @param[in] len    擦除长度
 * @return 0 成功, 非 0 失败
 */
typedef int (*mini_log_flash_erase_fn)(size_t offset, size_t len);

/**
 * @brief 写入数据
 * @param[in] offset 相对分区基址的偏移
 * @param[in] data   源数据
 * @param[in] len    写入长度
 * @return 0 成功, 非 0 失败
 */
typedef int (*mini_log_flash_write_fn)(size_t offset, const uint8_t *data, size_t len);

/**
 * @brief 读取数据
 * @param[in]  offset 相对分区基址的偏移
 * @param[out] buf    目标缓冲区
 * @param[in]  len    读取长度
 * @return 0 成功, 非 0 失败
 */
typedef int (*mini_log_flash_read_fn)(size_t offset, uint8_t *buf, size_t len);

/**
 * @brief flash 底层操作集合 (五项缺一不可)
 */
typedef struct {
    mini_log_flash_open_fn  open;  /**< 打开分区 */
    mini_log_flash_close_fn close; /**< 关闭分区 */
    mini_log_flash_erase_fn erase; /**< 扇区擦除 */
    mini_log_flash_write_fn write; /**< 写入 */
    mini_log_flash_read_fn  read;  /**< 读取 */
} mini_log_flash_ops_t;

/**
 * @brief flash 分区信息
 */
typedef struct {
    uint32_t base_addr;   /**< 分区物理基地址 */
    size_t   total_size;  /**< 分区总大小 (须为 sector_size 的整数倍) */
    size_t   sector_size; /**< 扇区大小 (如 4096) */
    size_t   write_gran;  /**< 最小写入粒度 (如 1/4/8, 为 0 取默认) */
} mini_log_flash_info_t;

/**
 * @brief 擦除整个 flash 分区
 * @return MINI_LOG_OK; MINI_LOG_ERR_NOT_INIT 未注册; MINI_LOG_ERR_FLASH_OPEN/ERASE 底层失败
 */
int mini_log_flash_clean_all(void);

/**
 * @brief 将 flash 暂存环中的数据逐条落盘到 flash
 * @note flash 落盘链路 (mini_log_flash_output 生产 / mini_log_flash_flush 消费) 共用 flash 暂存环。
 *       本库为独立无锁库, 自身不加锁; 若用于 OS / 多任务环境且存在多个上下文并发访问这条链路,
 *       必须由调用方在外部对 flash 暂存环的读写自行加锁。
 * @note 按 «整行» 组帧: 以 `\n` 为界装配, 一条日志正好一条记录 (帧内 `len` 即整行长度);
 *       被 MINI_LOG_MAX_LEN 截断的超长行按缓冲上限独立成帧。
 * @note 落盘失败时**原样透出**写记录的错误码 (便于区分参数类失败与底层故障)。
 *       若是底层故障 (OPEN/ERASE/WRITE), 内部进入 «重同步» 态: 丢弃暂存环里残留的半截数据
 *       直到下一个 `\n` 为止, 使后续记录重新从行边界开始 —— 否则半截行尾会被当成一条完整
 *       日志落盘 (帧 CRC 只覆盖自身 payload, 事后无法识别)。
 *       重同步态由下一次 flush 自动消解, 也可用 mini_log_flash_clean_all 立即清除。
 * @return MINI_LOG_OK; MINI_LOG_ERR_NOT_INIT 未注册; 否则为
 *         mini_log_flash_write_record 的错误码 (剩余数据留在暂存环)
 */
int mini_log_flash_flush(void);

/**
 * @brief 将一段数据封装成记录帧 (帧头 + payload + 对齐填充) 追加写入 flash
 * @note 帧头为 {magic, len, tick, crc}, 其中 crc 为**两字节 CRC-16** (模型见 MINI_LOG_FRAME_CRC_*),
 *       覆盖 [magic..tick] + payload (不含 crc 自身与对齐填充), 供帧有效性与记录边界校验。
 * @param[in] data 记录 payload
 * @param[in] len  payload 长度
 * @return MINI_LOG_OK; MINI_LOG_ERR_NOT_INIT 未注册; MINI_LOG_ERR_PARAM data 为空或 len==0;
 *         MINI_LOG_ERR_TOO_LONG 单帧放不下; MINI_LOG_ERR_FLASH_OPEN/ERASE/WRITE 底层失败
 */
int mini_log_flash_write_record(const char *data, size_t len);

/**
 * @brief 从 flash 读回指定区间的数据
 * @param[in] offset 起始偏移
 * @param[in] len    读取长度 (自动截断到分区末尾与内部暂存上限)
 * @return MINI_LOG_OK; MINI_LOG_ERR_NOT_INIT 未注册; MINI_LOG_ERR_PARAM len==0 或 offset 越界;
 *         MINI_LOG_ERR_FLASH_OPEN/READ 底层失败; 读回内容落在内部全局暂存
 */
int mini_log_read_from_flash(size_t offset, size_t len);

/**
 * @brief 上电扫描 flash 分区: 逐帧校验 magic + CRC, 恢复写偏移
 * @note 从偏移 0 顺序扫描, 遇到第一个非法帧 (magic 不符 / CRC 失败 / 越界) 即停, 并把
 *       write_offset 设为该位置 (下一条记录的写入处)。
 *       依据: `mini_log_flash_write_record` 进入扇区首地址时会先擦除该扇区, 擦除区 (全 0xFF)
 *       总是紧跟在游标之后, 扫描到它即停 —— 分区回绕后同样成立。
 * @note 停下处若是 «残留脏帧» (非 0xFF 空洞), 那里的旧字节没被擦除, 直接续写会违反 flash 的
 *       1->0 约束, 故把游标对齐到**下一个扇区边界** (续写时整扇区被擦除), 废弃本扇区剩余空间。
 * @param[out] out_frames 回传有效帧数量, 可传 NULL
 * @return MINI_LOG_OK; MINI_LOG_ERR_NOT_INIT 未注册; MINI_LOG_ERR_FLASH_OPEN/READ 底层失败
 *         (读失败时不动 write_offset)
 */
int mini_log_flash_recover(uint32_t *out_frames);

/**
 * @brief 注册 flash 底层操作与分区信息 (初始化入口)
 * @param[in] ops  flash 操作集合, 五项须全部非空
 * @param[in] info 分区信息, total_size/sector_size 合法且 write_gran 为 0 或 2 的幂
 * @return MINI_LOG_OK; MINI_LOG_ERR_PARAM 有任何一项不合法
 *         (注册失败时内部保持未初始化态, 各 flash 接口随后均返回 MINI_LOG_ERR_NOT_INIT)
 */
int mini_log_flash_register_cxt(const mini_log_flash_ops_t *ops, const mini_log_flash_info_t *info);
#endif /* MINI_LOG_USE_FLASH */

/*----------------------------------------------------------------------*/
/* 终端输出接口                                                          */
/*----------------------------------------------------------------------*/
/**
 * @brief 终端输出回调
 * @param[in] str 数据首地址
 * @param[in] len 数据长度
 * @return 无
 */
typedef void (*mini_log_output_fn)(const char *str, size_t len);

/**
 * @brief 设置终端输出回调
 * @param[in] fn 输出回调; 传 NULL 恢复默认 (fwrite 到 stdout)
 * @return 无
 */
void mini_log_set_output(mini_log_output_fn fn);

/**
 * @brief 将控制台日志环的数据输出 (走回调, 无回调则默认 fwrite 到 stdout)
 * @note 控制台链路 (mini_log_default_output 生产 / mini_log_flush 消费) 共用控制台日志环。
 *       本库为独立无锁库, 自身不加锁; 若用于 OS / 多任务环境且存在多个上下文并发访问这条链路,
 *       必须由调用方在外部对控制台日志环的读写自行加锁。
 * @return 无
 */
void mini_log_flush(void);

#if MINI_LOG_USE_FLASH
/**
 * @brief printf 风格日志入口: 格式化后投递到 flash 暂存环 (不做 I/O)
 * @note flash 落盘链路 (mini_log_flash_output 生产 / mini_log_flash_flush 消费) 共用 flash 暂存环。
 *       本库为独立无锁库, 自身不加锁; 若用于 OS / 多任务环境且存在多个上下文并发访问这条链路,
 *       必须由调用方在外部对 flash 暂存环的读写自行加锁。
 * @param[in] str 格式串
 * @param[in] ... 可变参数
 * @return 无
 */
void mini_log_flash_output(const char *str, ...) __attribute__((format(printf, 1, 2)));
#endif /* MINI_LOG_USE_FLASH */

/**
 * @brief printf 风格日志入口: 格式化后投递到控制台日志环
 * @note 控制台链路 (mini_log_default_output 生产 / mini_log_flush 消费) 共用控制台日志环。
 *       本库为独立无锁库, 自身不加锁; 若用于 OS / 多任务环境且存在多个上下文并发访问这条链路,
 *       必须由调用方在外部对控制台日志环的读写自行加锁。
 * @param[in] str 格式串
 * @param[in] ... 可变参数
 * @return 无
 */
void mini_log_default_output(const char *str, ...) __attribute__((format(printf, 1, 2)));

/*----------------------------------------------------------------------*/
/* 日志级别与颜色                                                        */
/*----------------------------------------------------------------------*/
/**
 * @brief 日志级别 (数值越大越详细, 用于编译期过滤)
 */
typedef enum {
    MINI_LOG_LEVEL_NONE    = 0, /**< 关闭 */
    MINI_LOG_LEVEL_ERROR   = 1, /**< 错误 */
    MINI_LOG_LEVEL_WARNING = 2, /**< 警告 */
    MINI_LOG_LEVEL_INFO    = 3, /**< 信息 */
    MINI_LOG_LEVEL_DEBUG   = 4, /**< 调试 */
} MiniLogLevel;

/**
 * @brief 终端 ANSI 颜色码 (MINI_LOG_COLOR_ENABLE == 0 时全部展开为空串)
 */
#if (defined(MINI_LOG_COLOR_ENABLE) && (MINI_LOG_COLOR_ENABLE == 1))
    #define LOG_COLOR_RED       "\033[0;31m"
    #define LOG_COLOR_YELLOW    "\033[0;33m"
    #define LOG_COLOR_GREEN     "\033[0;32m"
    #define LOG_COLOR_CYAN      "\033[0;36m"
    #define LOG_COLOR_GOLD      "\033[1;33m"
    #define LOG_COLOR_RESET     "\033[0m"
#else
    #define LOG_COLOR_RED
    #define LOG_COLOR_YELLOW
    #define LOG_COLOR_GREEN
    #define LOG_COLOR_CYAN
    #define LOG_COLOR_GOLD
    #define LOG_COLOR_RESET
#endif

/**
 * @brief 本模块编译期日志级别 (未定义时默认 MINI_LOG_LEVEL_DEBUG)
 */
#ifndef MINI_LOG_LOCAL_LEVEL
#define MINI_LOG_LOCAL_LEVEL MINI_LOG_LEVEL_DEBUG
#endif

/**
 * @brief 时间戳回调: 接入调用方自己的时基
 * @return 自系统启动的 tick; 返回负值表示本次取不到时间
 * @note 本库不感知任何 tick 源 (调度器 / RTOS / bsp), 由调用方注册;
 *       回调可能在任意日志上下文 (含 ISR) 被调用, 实现必须无阻塞、无锁
 */
typedef int (*mini_log_tick_fn)(void);

/**
 * @brief 注册时间戳回调
 * @param[in] fn 时间戳回调; 传 NULL 恢复 "未接入" 态
 * @return 无
 */
void mini_log_register_tick(mini_log_tick_fn fn);

/**
 * @brief 取当前时间戳
 * @return 已注册回调时为其返回值 (可能为负); 未注册回调时返回 -1
 */
int mini_log_get_tick(void);

/**
 * @brief 时间戳取值: 钩子返回负值 (未接入) 时统一按 0 输出
 * @return 非负时间戳
 */
static inline uint32_t mini_log_tick_value(void)
{
    int t = mini_log_get_tick();
    return (t < 0) ? 0u : (uint32_t)t;
}

/**
 * @brief 时间戳获取宏 (可在包含本头文件前用 -D 覆盖)
 */
#ifndef MINI_LOG_GET_TICK
#define MINI_LOG_GET_TICK() (mini_log_tick_value())
#endif

/**< 时间字段字符串缓冲长度 */
#define MINI_LOG_TIME_LEN 24

/**
 * @brief 渲染时间字段: 已接入 -> "t=<tick>"; 未接入 -> "not support check time"
 * @param[out] buf 输出缓冲 (仅已接入时写入)
 * @param[in]  n   缓冲大小
 * @return 时间字段字符串; 未接入时返回常量字符串 "not support check time"
 */
static inline const char *mini_log_tick_str(char *buf, size_t n)
{
    int t = mini_log_get_tick();
    if (t < 0)
        return "not support check time";
    (void)snprintf(buf, n, "t=%d", t);
    return buf;
}

/*----------------------------------------------------------------------*/
/* 日志打印宏 (带编译期过滤与自动行号)                                    */
/*----------------------------------------------------------------------*/
/**
 * @brief 内部: 统一发射一条日志 (先渲染时间字段再投递; __VA_ARGS__ 只求值一次)
 * @note 时间字段: 已接入 tick -> "t=<tick>"; 未接入 -> "not support check time"
 */
#define MINI_LOG_EMIT_(sink, color, tag, fmt, ...)                                  \
    do {                                                                            \
        char _mini_log_ts[MINI_LOG_TIME_LEN];                                        \
        (sink)(color tag " (%u) %s " fmt LOG_COLOR_RESET "\r\n",                    \
               (unsigned int)__LINE__,                                              \
               mini_log_tick_str(_mini_log_ts, sizeof(_mini_log_ts)),               \
               ##__VA_ARGS__);                                                      \
    } while (0)

/**
 * @brief 控制台日志宏 (E/W/I/D): 格式化后投递到控制台日志环
 * @note 输出前缀为 "<color><tag> (<行号>) <时间> "; 时间未接入 tick 时显示 "not support check time"
 */
#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_ERROR)
#define MINI_LOG_E(fmt, ...) MINI_LOG_EMIT_(mini_log_default_output, LOG_COLOR_RED, "[ERROR]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_E(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_WARNING)
#define MINI_LOG_W(fmt, ...) MINI_LOG_EMIT_(mini_log_default_output, LOG_COLOR_YELLOW, "[WARN]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_W(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_INFO)
#define MINI_LOG_I(fmt, ...) MINI_LOG_EMIT_(mini_log_default_output, LOG_COLOR_GREEN, "[INFO]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_I(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_DEBUG)
#define MINI_LOG_D(fmt, ...) MINI_LOG_EMIT_(mini_log_default_output, LOG_COLOR_CYAN, "[DEBUG]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_D(fmt, ...) ((void)0)
#endif

/*----------------------------------------------------------------------*/
/* flash 落盘宏 (投递到暂存环, 不碰控制台环)                              */
/*----------------------------------------------------------------------*/
#if MINI_LOG_USE_FLASH
/**
 * @brief flash 落盘宏 (E/W/I/D): 格式化后投递到 flash 暂存环
 * @note 输出前缀同控制台宏; 仅入队, 需配合 mini_log_flash_flush 落盘
 */
#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_ERROR)
#define MINI_LOG_FLASH_E(fmt, ...) MINI_LOG_EMIT_(mini_log_flash_output, LOG_COLOR_RED, "[ERROR]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_FLASH_E(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_WARNING)
#define MINI_LOG_FLASH_W(fmt, ...) MINI_LOG_EMIT_(mini_log_flash_output, LOG_COLOR_YELLOW, "[WARN]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_FLASH_W(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_INFO)
#define MINI_LOG_FLASH_I(fmt, ...) MINI_LOG_EMIT_(mini_log_flash_output, LOG_COLOR_GREEN, "[INFO]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_FLASH_I(fmt, ...) ((void)0)
#endif

#if (MINI_LOG_LOCAL_LEVEL >= MINI_LOG_LEVEL_DEBUG)
#define MINI_LOG_FLASH_D(fmt, ...) MINI_LOG_EMIT_(mini_log_flash_output, LOG_COLOR_CYAN, "[DEBUG]", fmt, ##__VA_ARGS__)
#else
#define MINI_LOG_FLASH_D(fmt, ...) ((void)0)
#endif
#else /* MINI_LOG_USE_FLASH == 0: 整条 flash 链路关闭 */
#define MINI_LOG_FLASH_E(fmt, ...) ((void)0)
#define MINI_LOG_FLASH_W(fmt, ...) ((void)0)
#define MINI_LOG_FLASH_I(fmt, ...) ((void)0)
#define MINI_LOG_FLASH_D(fmt, ...) ((void)0)
#endif /* MINI_LOG_USE_FLASH */

/**
 * @brief 手动触发控制台日志环输出 (MINI_LOG_AUTO_FLUSH == 0 时使用)
 * @return 无
 */
#if defined(MINI_LOG_AUTO_FLUSH) && (MINI_LOG_AUTO_FLUSH == 1)
#define MINI_LOG_FLUSH() mini_log_flush()
#else
#define MINI_LOG_FLUSH() ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif // LOG_H
