#include "log.h"
#include "log_config.h"
#include "buffer.h"
#if MINI_LOG_USE_FLASH
#include "crc.h"
#endif
#include "string.h"
#include <stdint.h>

/** @brief 主日志环的底层存储 (静态分配, 容量为 2 的幂) */
static uint8_t s_ring_buf[MINI_LOG_RING_SIZE];

#if MINI_LOG_USE_FLASH
/** @brief flash 环的底层存储 (建议与 flash 分区或某个 page 同大小, 2 的幂) */
static uint8_t s_ring_flash_buf[MINI_LOG_FLASH_RING_SIZE];

/** @brief 组帧/读回专用暂存 (独立于 s_flash_ring 的底层缓冲, 避免互相踩踏) */
static uint8_t s_flash_frame_buf[MINI_LOG_FLASH_RING_SIZE];

/**
 * @brief flash 记录帧头 (文件内私有)
 */
typedef struct 
{
    uint16_t magic;             /**< 固定魔数 (MINI_LOG_MAGIC) */
    uint16_t len;               /**< 实际 payload 字符长度 */
    uint32_t tick;              /**< 写入时间戳 */
    uint16_t crc;               /**< 帧校验 CRC-16, 覆盖 [magic..tick] + payload (见 MINI_LOG_FRAME_CRC_*) */
} __attribute__((packed)) mini_log_frame_handle_check_t;

/**
 * @brief flash 运行上下文 (文件内私有)
 */
typedef struct 
{
    mini_log_flash_ops_t  ops;  /**< 底层操作集合 */
    mini_log_flash_info_t info; /**< 分区信息 */
    size_t write_offset;        /**< 当前追加写偏移 (相对于 base_addr) */
    int is_initialized;         /**< 注册成功且字段合法时置 1 */
} mini_log_flash_ctx_t;

/** @brief flash 运行上下文实例 */
static mini_log_flash_ctx_t s_flash_ctx = {0};
#endif /* MINI_LOG_USE_FLASH */

/** @brief 控制台日志环句柄 (SPSC: 单生产者单消费者) */
static struct fifo_uni_spsc s_ring = 
{
    .buf       = s_ring_buf,
    .size      = MINI_LOG_RING_SIZE,
    .item_size = 1,                    /**< 1 = 字节流, 内存利用率最高 */
    .mask      = MINI_LOG_RING_SIZE - 1,
    .w_ptr     = 0,
    .r_ptr     = 0,
};

#if MINI_LOG_USE_FLASH
/** @brief flash 暂存环句柄 (SPSC: 生产 mini_log_flash_output / 消费 mini_log_flash_flush) */
static struct fifo_uni_spsc s_flash_ring=
{
    .buf       = s_ring_flash_buf,
    .size      = MINI_LOG_FLASH_RING_SIZE,
    .item_size = 1,                    /**< 1 = 字节流, 内存利用率最高 */
    .mask      = MINI_LOG_FLASH_RING_SIZE - 1,
    .w_ptr     = 0,
    .r_ptr     = 0,
};

/** @brief 整行装配缓冲: 跨多次 flush 累积字节, 保证一帧 = 一条日志 */
static char s_flash_line_buf[MINI_LOG_MAX_LEN];
/** @brief 当前已装配的字节数 */
static size_t s_flash_line_len = 0;
/**
 * @brief 落盘故障后的 «重同步» 标志
 * @note 置位后 flush 丢弃输入直到下一个 '\n', 把记录边界重新对齐到行边界; 否则底层写失败
 *       留下的半截行尾会被当成一条完整日志落盘 (帧 CRC 只覆盖自身 payload, 事后无法识别)。
 */
static int s_flash_resync = 0;
#endif

/** @brief 终端输出回调; 为 NULL 时走默认 fwrite 到 stdout */
static mini_log_output_fn s_mini_log_output = NULL;

/** @brief 时间戳回调; 为 NULL 时表示未接入 (时间字段显示 not support check time) */
static mini_log_tick_fn s_mini_log_tick = NULL;

void mini_log_register_tick(mini_log_tick_fn fn)
{
    s_mini_log_tick = fn;
}

int mini_log_get_tick(void)
{
    return (s_mini_log_tick != NULL) ? s_mini_log_tick() : -1;
}

void mini_log_set_output(mini_log_output_fn fn)
{
    s_mini_log_output = fn;
}

void mini_log_flush(void)
{
    uint8_t chunk[128];
    uint16_t n;

    for (;;)
    {
        uint16_t got = 0;
        (void)fifo_uni_read_block(&s_ring, chunk, (uint16_t)sizeof(chunk), &got);
        n = got;
        if (n == 0)
            break;

        if (s_mini_log_output)
        {
            s_mini_log_output((const char *)chunk, n);
        }
        else
        {
            fwrite(chunk, 1, n, stdout);
            fflush(stdout);
        }
    }
}

void mini_log_default_output(const char *str, ...)
{
    if (!str)
        return;

    char fmt[MINI_LOG_MAX_LEN];

    va_list args;
    va_start(args, str);
    int n = vsnprintf(fmt, sizeof(fmt), str, args);
    va_end(args);

    if (n <= 0)
        return;

    uint16_t len = (n < (int)sizeof(fmt)) ? (uint16_t)n : (uint16_t)(sizeof(fmt) - 1);

    uint16_t used = 0;
    (void)fifo_uni_get_count(&s_ring, &used);
    if ((uint16_t)(MINI_LOG_RING_SIZE - used) < len)
        return;

    uint16_t wrote = 0;
    (void)fifo_uni_write_block(&s_ring, fmt, len, &wrote);

#if MINI_LOG_AUTO_FLUSH
    mini_log_flush();
#endif
}

#if MINI_LOG_USE_FLASH
void mini_log_flash_output(const char *str, ...)
{
    if (!str)
        return;

    char fmt[MINI_LOG_MAX_LEN];

    va_list args;
    va_start(args, str);
    int n = vsnprintf(fmt, sizeof(fmt), str, args);
    va_end(args);

    if (n <= 0)
        return;

    uint16_t len = (n < (int)sizeof(fmt)) ? (uint16_t)n : (uint16_t)(sizeof(fmt) - 1);

    /*
     * flash 侧按行组帧, 必须保证入环的块以 '\n' 收尾。
     */
    if (fmt[len - 1] != '\n')
        fmt[len - 1] = '\n';

    uint16_t used = 0;
    (void)fifo_uni_get_count(&s_flash_ring, &used);
    if ((uint16_t)(MINI_LOG_FLASH_RING_SIZE - used) < len)
        return;

    uint16_t wrote = 0;
    (void)fifo_uni_write_block(&s_flash_ring, fmt, len, &wrote);

#if MINI_LOG_FLASH_AUTO_FLUSH
    (void)mini_log_flash_flush();
#endif
}

int mini_log_flash_register_cxt(const mini_log_flash_ops_t *ops, const mini_log_flash_info_t *info)
{
    s_flash_ctx.is_initialized = 0;
    s_flash_resync = 0;

    if (ops == NULL || info == NULL)
        return MINI_LOG_ERR_PARAM;
    if (ops->open == NULL || ops->close == NULL || ops->erase == NULL ||
        ops->write == NULL || ops->read == NULL)
        return MINI_LOG_ERR_PARAM;

    if (info->total_size == 0 || info->sector_size == 0)
        return MINI_LOG_ERR_PARAM;
    if ((info->total_size % info->sector_size) != 0)
        return MINI_LOG_ERR_PARAM;
    if (info->write_gran != 0 && (info->write_gran & (info->write_gran - 1)) != 0)
        return MINI_LOG_ERR_PARAM;

    s_flash_ctx.ops = *ops;
    s_flash_ctx.info = *info;
    s_flash_ctx.write_offset = 0;
    s_flash_ctx.is_initialized = 1;

    return MINI_LOG_OK;
}

int mini_log_flash_clean_all(void)
{
    if (!s_flash_ctx.is_initialized)
        return MINI_LOG_ERR_NOT_INIT;

    if (s_flash_ctx.ops.open(s_flash_ctx.info.base_addr,
                             s_flash_ctx.info.sector_size,
                             (uint16_t)(s_flash_ctx.info.total_size / s_flash_ctx.info.sector_size)) != 0)
        return MINI_LOG_ERR_FLASH_OPEN;

    int ret = s_flash_ctx.ops.erase(0, s_flash_ctx.info.total_size);

    s_flash_ctx.ops.close();

    if (ret != 0)
        return MINI_LOG_ERR_FLASH_ERASE;

    s_flash_ctx.write_offset = 0;
    s_flash_resync = 0;                 /* 整片已擦除, 记录边界重新对齐 */

    return MINI_LOG_OK;
}

int mini_log_flash_write_record(const char *data, size_t len)
{
    if (!s_flash_ctx.is_initialized)
        return MINI_LOG_ERR_NOT_INIT;
    if (data == NULL || len == 0)
        return MINI_LOG_ERR_PARAM;

    size_t total_len = sizeof(mini_log_frame_handle_check_t) + len;
    size_t aligned_len = s_flash_ctx.info.write_gran > 0 ? s_flash_ctx.info.write_gran : MINI_LOG_DEFAULT_ALIGIN;
    size_t write_len = (total_len + (aligned_len - 1)) & ~(aligned_len - 1);

    if (write_len > MINI_LOG_FLASH_RING_SIZE ||
        write_len > s_flash_ctx.info.total_size)
        return MINI_LOG_ERR_TOO_LONG;

    if (s_flash_ctx.write_offset + write_len > s_flash_ctx.info.total_size)
        s_flash_ctx.write_offset = 0;

    mini_log_frame_handle_check_t handle =
    {
        .len   = (uint16_t)len,
        .magic = MINI_LOG_MAGIC,
        .tick  = MINI_LOG_GET_TICK(),
        .crc   = 0
    };

    crc_stream_t cs;
    crc_stream_start(&cs, MINI_LOG_FRAME_CRC_INIT, MINI_LOG_FRAME_CRC_REFIN,
                     MINI_LOG_FRAME_CRC_REFOUT, MINI_LOG_FRAME_CRC_XOR_OUT,
                     MINI_LOG_FRAME_CRC_POLY, MINI_LOG_FRAME_CRC_WIDTH);
    crc_stream_feed(&cs, (const uint8_t *)&handle,
                    offsetof(mini_log_frame_handle_check_t, crc));
    crc_stream_feed(&cs, (const uint8_t *)data, len);
    handle.crc = (uint16_t)crc_stream_finish(&cs);

    memset(s_flash_frame_buf, 0xFF, MINI_LOG_FLASH_RING_SIZE);
    memcpy(s_flash_frame_buf, &handle, sizeof(handle));
    memcpy(s_flash_frame_buf + sizeof(handle), data, len);

    size_t sector_size = s_flash_ctx.info.sector_size;
    size_t frame_end = s_flash_ctx.write_offset + write_len;
    size_t erase_start = ((s_flash_ctx.write_offset % sector_size) == 0)
                             ? s_flash_ctx.write_offset
                             : ((s_flash_ctx.write_offset / sector_size) + 1) * sector_size;
    size_t erase_end = ((frame_end + sector_size - 1) / sector_size) * sector_size;

    if (s_flash_ctx.ops.open(s_flash_ctx.info.base_addr, sector_size,
                             (uint16_t)(s_flash_ctx.info.total_size / sector_size)) != 0)
        return MINI_LOG_ERR_FLASH_OPEN;

    if (erase_end > erase_start &&
        s_flash_ctx.ops.erase(erase_start, erase_end - erase_start) != 0)
    {
        s_flash_ctx.ops.close();
        return MINI_LOG_ERR_FLASH_ERASE;
    }

    if (s_flash_ctx.ops.write(s_flash_ctx.write_offset, s_flash_frame_buf, write_len) != 0)
    {
        s_flash_ctx.ops.close();
        return MINI_LOG_ERR_FLASH_WRITE;
    }

    s_flash_ctx.write_offset += write_len;
    if (s_flash_ctx.write_offset < s_flash_ctx.info.total_size &&
        (s_flash_ctx.write_offset % sector_size) == 0)
        s_flash_ctx.ops.erase(s_flash_ctx.write_offset, sector_size);

    s_flash_ctx.ops.close();

    return MINI_LOG_OK;
}

int mini_log_flash_flush(void)
{
    uint8_t chunk[128];
    uint16_t n;
    int ret = MINI_LOG_OK;

    if (!s_flash_ctx.is_initialized)
        return MINI_LOG_ERR_NOT_INIT;

    for (;;)
    {
        uint16_t got = 0;
        (void)fifo_uni_read_block(&s_flash_ring, chunk, (uint16_t)sizeof(chunk), &got);
        n = got;
        if (n == 0)
            break;

        for (uint16_t i = 0; i < n; i++)
        {
            if (s_flash_resync)
            {
                /* 丢弃残留半截数据, 直到下一个行尾为止, 记录边界重新对齐 */
                if (chunk[i] == '\n')
                    s_flash_resync = 0;
                continue;
            }

            s_flash_line_buf[s_flash_line_len++] = (char)chunk[i];

            if (chunk[i] == '\n' || s_flash_line_len == sizeof(s_flash_line_buf))
            {
                ret = mini_log_flash_write_record(s_flash_line_buf, s_flash_line_len);
                s_flash_line_len = 0;

                if (ret != MINI_LOG_OK)
                {
                    /* 只有底层故障才需要重同步; 参数/尺寸类失败说明该条本就不该 */
                    if (ret == MINI_LOG_ERR_FLASH_OPEN ||
                        ret == MINI_LOG_ERR_FLASH_ERASE ||
                        ret == MINI_LOG_ERR_FLASH_WRITE)
                        s_flash_resync = 1;
                    return ret;
                }
            }
        }
    }

    return ret;
}

int mini_log_read_from_flash(size_t offset, size_t len)
{
    if (!s_flash_ctx.is_initialized)
        return MINI_LOG_ERR_NOT_INIT;
    if (len == 0 || offset >= s_flash_ctx.info.total_size)
        return MINI_LOG_ERR_PARAM;

    if (len > s_flash_ctx.info.total_size - offset)
        len = s_flash_ctx.info.total_size - offset;
    if (len > MINI_LOG_FLASH_RING_SIZE)
        len = MINI_LOG_FLASH_RING_SIZE;

    size_t sector_size = s_flash_ctx.info.sector_size;
    uint16_t seg = (uint16_t)((offset + len + sector_size - 1) / sector_size);

    if (s_flash_ctx.ops.open(s_flash_ctx.info.base_addr, sector_size, seg) != 0)
        return MINI_LOG_ERR_FLASH_OPEN;

    int ret = s_flash_ctx.ops.read(offset, s_flash_frame_buf, len);

    s_flash_ctx.ops.close();

    return (ret == 0) ? MINI_LOG_OK : MINI_LOG_ERR_FLASH_READ;
}

/**
 * @brief 校验已落在 s_flash_frame_buf 中的一帧 (帧头 + payload) 的 CRC
 * @param payload_len payload 长度
 * @return 1 通过, 0 失败
 */
static int mini_log_frame_crc_ok(size_t payload_len)
{
    mini_log_frame_handle_check_t h;
    crc_stream_t cs;

    memcpy(&h, s_flash_frame_buf, sizeof(h));

    crc_stream_start(&cs, MINI_LOG_FRAME_CRC_INIT, MINI_LOG_FRAME_CRC_REFIN,
                     MINI_LOG_FRAME_CRC_REFOUT, MINI_LOG_FRAME_CRC_XOR_OUT,
                     MINI_LOG_FRAME_CRC_POLY, MINI_LOG_FRAME_CRC_WIDTH);
    crc_stream_feed(&cs, s_flash_frame_buf, offsetof(mini_log_frame_handle_check_t, crc));
    crc_stream_feed(&cs, s_flash_frame_buf + sizeof(h), payload_len);

    return ((uint16_t)crc_stream_finish(&cs) == h.crc) ? 1 : 0;
}

int mini_log_flash_recover(uint32_t *out_frames)
{
    size_t total_size;
    size_t sector_size;
    size_t aligned_len;
    size_t offset = 0;
    uint32_t frames = 0;
    int stopped_dirty = 0;   /**< 停下处是「脏帧」(残留字节, 非 0xFF ) */

    int err = MINI_LOG_OK;

    if (!s_flash_ctx.is_initialized)
        return MINI_LOG_ERR_NOT_INIT;

    total_size = s_flash_ctx.info.total_size;
    sector_size = s_flash_ctx.info.sector_size;
    aligned_len = s_flash_ctx.info.write_gran > 0 ? s_flash_ctx.info.write_gran : MINI_LOG_DEFAULT_ALIGIN;

    if (s_flash_ctx.ops.open(s_flash_ctx.info.base_addr, sector_size,
                             (uint16_t)(total_size / sector_size)) != 0)
        return MINI_LOG_ERR_FLASH_OPEN;

    while (offset + sizeof(mini_log_frame_handle_check_t) <= total_size)
    {
        mini_log_frame_handle_check_t h;
        size_t total;

        if (s_flash_ctx.ops.read(offset, s_flash_frame_buf,
                                 sizeof(mini_log_frame_handle_check_t)) != 0)
        {
            err = MINI_LOG_ERR_FLASH_READ;
            break;
        }

        memcpy(&h, s_flash_frame_buf, sizeof(h));

        if (h.magic != MINI_LOG_MAGIC)
        {
            /* 帧头全 0xFF = 可续写; 否则是残留脏数据 */
            size_t i;
            for (i = 0; i < sizeof(h); i++)
                if (s_flash_frame_buf[i] != 0xFF) 
                { 
                    stopped_dirty = 1; 
                    break; 
                }
            break;
        }

        if (h.len == 0)
        {
            stopped_dirty = 1;
            break;
        }

        total = sizeof(h) + h.len;
        if (total > MINI_LOG_FLASH_RING_SIZE || offset + total > total_size)
        {
            stopped_dirty = 1;
            break;
        }

        if (s_flash_ctx.ops.read(offset + sizeof(h), s_flash_frame_buf + sizeof(h),
                                 h.len) != 0)
        {
            err = MINI_LOG_ERR_FLASH_READ;
            break;
        }

        if (!mini_log_frame_crc_ok(h.len))
        {
            stopped_dirty = 1;
            break;
        }

        offset += (total + (aligned_len - 1)) & ~(aligned_len - 1);
        frames++;
    }

    s_flash_ctx.ops.close();

    if (err == MINI_LOG_OK)
    {
        /*
         * 停在脏帧处时, 那里的旧字节没被擦除, 直接续写会违反 flash 的 1->0 约束;
         * 把游标对齐到下一个扇区边界 (续写时整扇区被擦除), 废弃本扇区剩余空间。
         * 停在 0xFF 空洞则保持原位, 本扇区剩余空间仍可用。
         */
        if (stopped_dirty)
            offset = ((offset + sector_size - 1) / sector_size) * sector_size;

        s_flash_ctx.write_offset = (offset > total_size) ? 0 : offset;
    }

    if (out_frames != NULL)
        *out_frames = frames;

    return err;
}
#endif /* MINI_LOG_USE_FLASH */
