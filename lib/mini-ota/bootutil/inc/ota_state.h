/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file ota_state.h
 * @brief OTA 持久化状态：按位定义的状态字 + 存储后端钩子。
 * @note  状态固定用"一个 32bit 状态字 + 一个校验字"（共 2 字）表达：一个状态字
 *        正好等于一个 RTC 备份寄存器，各芯片后端可直接一对一映射，不存在结构体
 *        跨字/对齐问题；核心逻辑只按位判断，不依赖字段偏移。
 *        介质（flash 状态扇区 / RTC 备份域 / RAM）由平台注册 ota_state_ops_t 提供。
 */
#ifndef BOOTUTIL_INC_OTA_STATE_H
#define BOOTUTIL_INC_OTA_STATE_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include <stdint.h>

/* ---------------- 状态字位定义（判位访问，不依赖结构体布局） ----------------
 * bit0      OTA 开关           0=关 1=开
 * bit1      保留             （双分区是编译期能力，见 boot_config.h 的 OTA_DUAL_PARTITION）
 * bit2      回滚开关           0=关 1=开
 * bit3      强制 OTA           仅 DEBUG 有效
 * bit4~5    失败码             OTA_FAIL_xxx（见 start.h）
 * bit6      当前分区           0=image_0 1=image_1
 * bit7      待确认 pending     1=新镜像已激活、尚未确认
 * bit8~15   魔数 0xA5          有效性判定 + 格式版本
 * bit16     试运行 trial       1=已带着 pending 跳入新分区一次，仍未被确认
 * bit17~31  保留               写 0
 *
 * 注意：bit0~bit6 与 start.h 的 ota_status 位布局一致，便于加载后直接回填；
 *       回填时请只取 bit0~bit6（pending 是 bit7，不属于 ota_status 的 8bit 定义）。
 *
 * pending + trial 构成"两次判定"（标准 A/B 试运行语义）：
 *   pending=0                 → 已确认或从未升级，正常启动
 *   pending=1 且 trial=0      → 刚激活，放行跳入新分区并把 trial 置 1
 *   pending=1 且 trial=1      → 已跳入过一次仍未 confirm → 新固件没起来, 回滚
 *   confirm 清掉 pending+trial → 试运行通过，不再回滚
 */
#define OTA_STATE_BIT_OPEN        0u
#define OTA_STATE_BIT_ROLLBACK    2u
#define OTA_STATE_BIT_FORCE       3u
#define OTA_STATE_FAIL_SHIFT      4u
#define OTA_STATE_FAIL_MASK       (0x3u << OTA_STATE_FAIL_SHIFT)
#define OTA_STATE_BIT_CURRENT     6u
#define OTA_STATE_BIT_PENDING     7u
#define OTA_STATE_MAGIC_SHIFT     8u
#define OTA_STATE_MAGIC_MASK      (0xFFu << OTA_STATE_MAGIC_SHIFT)
#define OTA_STATE_MAGIC_VALUE     0xA5u
#define OTA_STATE_BIT_TRIAL       16u

/* 单 bit 掩码：做"读-改-写"时写名字，避免代码里到处写 1u << bit */
#define OTA_STATE_MASK_OPEN       (1u << OTA_STATE_BIT_OPEN)
#define OTA_STATE_MASK_ROLLBACK   (1u << OTA_STATE_BIT_ROLLBACK)
#define OTA_STATE_MASK_FORCE      (1u << OTA_STATE_BIT_FORCE)
#define OTA_STATE_MASK_CURRENT    (1u << OTA_STATE_BIT_CURRENT)
#define OTA_STATE_MASK_PENDING    (1u << OTA_STATE_BIT_PENDING)
#define OTA_STATE_MASK_TRIAL      (1u << OTA_STATE_BIT_TRIAL)

/* 当前分区取值（bit6 解码后的值，不是掩码） */
#define OTA_STATE_PARTITION_IMAGE_0   0u
#define OTA_STATE_PARTITION_IMAGE_1   1u

/* 低 8 位 = 原 ota_status 字节（bit0~bit7） */
#define OTA_STATE_MASK_STATUS_BYTE    0xFFu

/** 持久化位掩码：只有这些位跨复位保留；开关位属运行期配置（app 每次启动自行设置） */
#define OTA_STATE_DURABLE_MASK    (OTA_STATE_FAIL_MASK | OTA_STATE_MASK_CURRENT | \
                                   OTA_STATE_MASK_PENDING | OTA_STATE_MASK_TRIAL)

#if !defined(__cplusplus) && !defined(_MSC_VER)
/* 位域不自相重叠：掩码写错在编译期就报，不留到烧进去才发现 */
_Static_assert((OTA_STATE_FAIL_MASK & OTA_STATE_MASK_CURRENT) == 0u,
               "ota_state: fail code field overlaps current-partition bit");
_Static_assert((OTA_STATE_MAGIC_MASK & (OTA_STATE_FAIL_MASK |
                                        OTA_STATE_MASK_CURRENT |
                                        OTA_STATE_MASK_PENDING)) == 0u,
               "ota_state: magic field overlaps other fields");
_Static_assert((OTA_STATE_DURABLE_MASK & OTA_STATE_MAGIC_MASK) == 0u,
               "ota_state: durable bits must not include magic");
#endif

/** 记录字长：状态字 + 校验字；校验字最后写，半写掉电时整条记录作废 */
#define OTA_STATE_RECORD_WORDS    2u

/** @brief 读某一位（返回 0 或 1） */
static inline uint32_t ota_state_bit_get(uint32_t word, uint32_t bit)
{
    return (word >> bit) & 0x1u;
}

/** @brief 写某一位（返回新状态字） */
static inline uint32_t ota_state_bit_put(uint32_t word, uint32_t bit, uint32_t value)
{
    return (value != 0u) ? (word | (0x1u << bit)) : (word & ~(0x1u << bit));
}

/** @brief 读失败码（0~3） */
static inline uint32_t ota_state_fail_get(uint32_t word)
{
    return (word & OTA_STATE_FAIL_MASK) >> OTA_STATE_FAIL_SHIFT;
}

/** @brief 把失败码编码进状态字对应位域（超出按低 2 位截断） */
static inline uint32_t ota_state_fail_encode(uint32_t code)
{
    return (code << OTA_STATE_FAIL_SHIFT) & OTA_STATE_FAIL_MASK;
}

/** @brief 写失败码（超出按低 2 位截断，返回新状态字） */
static inline uint32_t ota_state_fail_put(uint32_t word, uint32_t code)
{
    return (word & ~OTA_STATE_FAIL_MASK) | ota_state_fail_encode(code);
}

/** @brief 读当前分区（0=image_0 1=image_1） */
static inline uint32_t ota_state_partition_get(uint32_t word)
{
    return ota_state_bit_get(word, OTA_STATE_BIT_CURRENT);
}

/** @brief 读待确认标志（1=待 app 确认） */
static inline uint32_t ota_state_pending_get(uint32_t word)
{
    return ota_state_bit_get(word, OTA_STATE_BIT_PENDING);
}

/** @brief 读试运行标志（1=已跳入新分区一次但未确认；boot 据此决定放行还是回滚） */
static inline uint32_t ota_state_trial_get(uint32_t word)
{
    return ota_state_bit_get(word, OTA_STATE_BIT_TRIAL);
}

/* ---------------- 记录完整性（核心与各后端共用） ---------------- */
/** @brief 状态字校验值（校验字内容） */
uint32_t ota_state_crc32(uint32_t state_word);

/** @brief 由状态字组装一条记录（补魔数 + 算校验字） */
void ota_state_record_build(uint32_t record[OTA_STATE_RECORD_WORDS], uint32_t state_word);

/**
 * @brief 回滚：pending 置位时切到另一个分区、清 pending/trial、写入失败码
 * @param state     输入输出状态字（就地修改）
 * @param fail_code 回滚时记录的失败码（OTA_FAIL_xxx）
 * @return 1 = 已回滚；0 = 无需回滚或入参为空
 * @note  "是否该回滚"由调用方判定（boot 侧要求 pending=1 且 trial=1 才试运行超时），
 *        本函数只改状态字，不做试运行次数判断
 */
int ota_state_resolve_pending(uint32_t *state, uint32_t fail_code);

/** @brief 校验一条记录（魔数 + 校验字）；返回 1 有效 / 0 无效 */
int ota_state_record_valid(const uint32_t record[OTA_STATE_RECORD_WORDS]);

/* ---------------- 存储后端钩子 ----------------
 * 后端只负责两件事：把一条记录原子地持久化、把最近一条有效记录读回来。
 * "原子"= 任意时刻掉电后，load 拿到的必须是完整的新记录或完整的旧记录。
 * 具体怎么做到（flash 追加日志 / RTC 双缓冲）由后端按各自介质实现。
 */
typedef struct
{
    /** 读回最近一条有效记录；无有效记录返回 ERR_OTA_STATE */
    int (*load)(uint32_t record[OTA_STATE_RECORD_WORDS]);
    /** 持久化一条记录；失败返回负数（见 err.h） */
    int (*store)(const uint32_t record[OTA_STATE_RECORD_WORDS]);
} ota_state_ops_t;

/** @brief 注册状态存储后端（平台启动时调用一次；成员不可为 NULL） */
int ota_state_ops_register(const ota_state_ops_t *ops);

/* ---------------- 高层接口（魔数/校验/位语义在此收口） ---------------- */
/**
 * @brief 读回最近一次持久化状态字；无有效记录返回 ERR_OTA_STATE；未注册后端返回 ERR_NOT_SUPPORTED
 * @note  仅供 boot 播种用；返回的是介质里的持久位，不含运行期开关位（那些位不落盘），
 *        运行期读状态请走 start.h 的 getter，不要拿本函数当"读当前状态"
 */
int ota_state_load(uint32_t *state_word);

/** @brief 持久化状态字（内部补魔数与校验字） */
int ota_state_store(uint32_t state_word);

/**
 * @brief 读-改-写持久状态：只改 mask 指定的持久位，其余位从介质读回保留
 * @param mask  要修改的位（OTA_STATE_DURABLE_MASK 的子集）
 * @param value 这些位的新值
 * @return ERR_OK；未注册后端返回 ERR_NOT_SUPPORTED
 * @note  给 boot/app 各自镜像安全落盘用：调用方**不必先 ota_state_load()**，
 *        避免"未加载的 RAM 副本整字回写"把其它持久位（如当前分区）冲掉
 */
int ota_state_update(uint32_t mask, uint32_t value);

/* ---------------- 内置后端 ---------------- */
/** @brief 注册内置的 flash 状态扇区后端（追加日志，区域 id = FLASH_AREA_ID_STATE） */
int ota_state_flash_register(void);

#if defined(__cplusplus)
}
#endif
#endif /* BOOTUTIL_INC_OTA_STATE_H */
