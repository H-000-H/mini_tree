/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: read.h
 * @brief: OTA 镜像读取：布局解析 + 按模式完成「完整性校验 + 取明文」，
 *         与 tools/main.py 的打包格式一一对应
 *         纯 C 实现：无动态内存、无文件 IO、无标准库输出，可直接在 bootloader 中调用
 *         解析部分（image_parse）只做「按布局切字段」：算偏移、切字段、读 CRC，
 *         不做任何校验、解密、比较；加密原语依赖 algorithm.h（SHA/HMAC/AES）
 * 镜像自描述：末尾固定 4B meta 记录 magic/模式/元数据位置/变长字段长度，
 * 调用方无需再传入 mode/version_len/tag_len/is_front，cfg 只剩密钥
 * 镜像布局(is_front=1): crc32(4B) | version | tag | aux | payload | meta(4B)
 * 镜像布局(is_front=0): payload | aux | version | tag | crc32(4B) | meta(4B)
 * meta 布局: magic(0xA5) | mode|0x80(is_front 置位) | version_len | tag_len
 * aux 布局: CRC=空  SHA=sha256(32B)  GCM=nonce(12B)+tag(16B)  CBC=iv(16B)  CBC_SHA=iv(16B)+hmac(32B)
 * 接口约定: 统一返回 err.h 中的错误码，结果一律通过 [out] 指针参数返回
 * 各模式处理内容（与 tools/main.py 打包时一一对应）:
 *   CRC     : 对 payload 算 CRC32，与镜像记录的 crc 比较；payload 即明文
 *             CRC 模型由 boot_config.h 的 CRC_MODEL_* 决定（可用 CONFIG_CRC_* 覆盖），
 *             必须与 tools/main.py 的打包参数一致
 *   SHA     : 对 payload 算 SHA-256，与 aux(32B) 比较；payload 即明文
 *   GCM     : 用 aux 前 12B 作 nonce、后 16B 作 tag 做 AES-GCM 解密并认证
 *   CBC     : 用 aux(16B) 作 iv 做 AES-CBC 解密，再做 PKCS#7 去填充
 *   CBC_SHA : 先用 HMAC-SHA-256 校验 IV||密文（覆盖范围须与打包端 tools/main.py 一致），
 *             通过后才做 AES-CBC 解密与 PKCS#7 去填充（verify-then-decrypt，认证不过绝不解密）
 * 编译开关: boot_config.h 的 IMAGE_CRYPTO_ENABLE(默认 1) 决定是否编入 SHA/HMAC/AES 分支
 *          （需链接 mbedcrypto）；置 0 时这些模式直接返回 ERR_UNSUPPORTED，便于纯 CRC 场景裁剪体积
 */
#ifndef READ_H
#define READ_H

#include <stddef.h>
#include <stdint.h>

#include "err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================ 镜像布局解析 ============================ */

/** 镜像中 CRC32 字段长度（小端） */
#define IMAGE_CRC_LEN 4u

/** 末尾自描述 meta 长度 */
#define IMAGE_META_LEN 4u

/** meta 魔数 */
#define IMAGE_META_MAGIC 0xA5u

/** meta 中记录的打包模式，决定 aux 长度 */
typedef enum
{
    IMAGE_CHECK_CRC = 0, /**< aux 0B  */
    IMAGE_CHECK_SHA,     /**< aux 32B */
    IMAGE_CHECK_GCM,     /**< aux 28B */
    IMAGE_CHECK_CBC,     /**< aux 16B */
    IMAGE_CHECK_CBC_SHA  /**< aux 48B */
} image_check_t;

/** 解析结果：全部为指向输入缓冲区的指针，不拷贝数据 */
typedef struct
{
    image_check_t mode;     /**< meta 中记录的打包模式 */
    int is_front;           /**< 非 0 表示元数据（crc/version/tag/aux）在头部 */
    const uint8_t *payload; /**< 固件本体（加密模式下为密文） */
    size_t payload_len;
    const uint8_t *aux;     /**< nonce/iv/tag/sha256 等附加数据 */
    size_t aux_len;
    const uint8_t *version; /**< 版本字符串，非 '\0' 结尾 */
    size_t version_len;
    const uint8_t *tag;     /**< 标签字符串，非 '\0' 结尾 */
    size_t tag_len;
    uint32_t crc_stored;    /**< 镜像中记录的 CRC32（小端），是否一致由校验分支判定 */
} image_view_t;

/**
 * @brief 查询某模式的 aux 长度
 * @param mode    [in]  校验/加密模式
 * @param out_len [out] aux 字节数，不可为 NULL
 * @return ERR_OK / ERR_ARG
 */
int image_aux_len(image_check_t mode, size_t *out_len);

/**
 * @brief 查询镜像除 payload 外的固定开销
 * @param mode          [in]  校验/加密模式
 * @param version_len   [in]  version 字符串长度
 * @param tag_len       [in]  tag 字符串长度
 * @param out_overhead  [out] crc + meta + version + tag + aux 总长度，不可为 NULL
 * @return ERR_OK / ERR_ARG（模式不支持、out_overhead 为 NULL、长度和溢出）
 */
int image_overhead(image_check_t mode, size_t version_len, size_t tag_len,
                   size_t *out_overhead);

/**
 * @brief 读末尾 meta 后按布局把镜像切分为 payload/aux/version/tag，并读出记录的 CRC32
 * @param buf      [in]  镜像数据
 * @param len      [in]  镜像长度
 * @param out_view [out] 解析结果，不可为 NULL
 * @return ERR_OK / ERR_ARG（meta 非法）/ ERR_TOO_SMALL
 */
int image_parse(const uint8_t *buf, size_t len, image_view_t *out_view);

/* ============================ 读取与校验 ============================ */

/** AES-GCM/CBC 的 nonce/iv 长度与 GCM 标签长度 */
#define IMAGE_GCM_NONCE_LEN 12u
#define IMAGE_GCM_TAG_LEN   16u
#define IMAGE_CBC_IV_LEN    16u
/** SHA-256 / HMAC-SHA-256 摘要长度 */
#define IMAGE_HASH_LEN      32u
/** AES-CBC 分组长度，密文长度必须是它的整数倍 */
#define IMAGE_CBC_BLOCK_LEN 16u

/** 读取配置，全部为入参；模式与变长字段长度均来自镜像 meta，此处只需提供密钥 */
typedef struct
{
    const uint8_t *key;     /**< [in] 加密密钥，GCM/CBC/CBC_SHA 必填，其余模式可为 NULL */
    size_t key_len;         /**< [in] 加密密钥长度，16/24/32 字节 */
    const uint8_t *mac_key; /**< [in] MAC 密钥，仅 CBC_SHA 使用；为 NULL 时回退用 key
                                 （生产环境应与加密密钥独立，见 algorithm.h） */
    size_t mac_key_len;     /**< [in] mac_key 长度；mac_key 为 NULL 时忽略本字段 */
} image_read_cfg_t;

/**
 * @brief 镜像数据读取回调：从存储介质读取 offset 处 len 字节到 buf
 * @param ctx    [in]  回调上下文（调用 image_verify_stream 时原样传回）
 * @param offset [in]  镜像内的相对偏移
 * @param buf    [out] 输出缓冲区
 * @param len    [in]  读取长度
 * @return ERR_OK 成功，负数为失败原因（见 err.h）
 * @note  适配任意来源：flash 区读、内存直读、外部 SPI NOR、流式接收均可
 */
typedef int (*image_read_fn)(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len);

/**
 * @brief 流式校验镜像：分块读取并增量计算 CRC/SHA/HMAC/GCM，镜像无需整包驻留内存
 *        模式与变长字段长度从镜像末尾 meta 自描述获得
 * @param read_fn      [in]  读取回调，不可为 NULL
 * @param read_ctx     [in]  回调上下文，可为 NULL
 * @param img_size     [in]  镜像总长度
 * @param cfg          [in]  读取配置（key/mac_key），不可为 NULL
 * @param scratch      [in]  分块读取用临时缓冲（调用方提供），不可为 NULL
 * @param scratch_size [in]  缓冲大小（>=16，建议 512~4096，越大越快）
 * @param out_crc      [out] 实算 CRC32（仅 CRC 模式有效），可为 NULL
 * @return ERR_OK 校验通过；ERR_ARG / ERR_NOT_SUPPORTED(GCM/CBC/CBC_SHA 且未开加密) /
 *         ERR_CRC_MISMATCH / ERR_HASH_MISMATCH / ERR_AUTH_FAILED / 回调返回的错误码
 * @note  各模式流式语义：
 *          CRC      : 增量 CRC32 累加后比对
 *          SHA      : 增量 SHA-256 后与 aux 摘要比对
 *          CBC      : 无认证属性，布局检查通过即返回 ERR_OK（与一次性接口一致）
 *          GCM      : 流式解密（输出即弃），finish 时比对 tag
 *          CBC_SHA  : 流式 HMAC 验证 IV||密文（verify-then-decrypt 的验证段）
 */
int image_verify_stream(image_read_fn read_fn, void *read_ctx, uint32_t img_size,
                        const image_read_cfg_t *cfg,
                        uint8_t *scratch, uint32_t scratch_size, uint32_t *out_crc);

/**
 * @brief 解析 + 校验 + 取明文 payload（加密模式会解密后写出）
 *        CRC/SHA 模式：out_payload 可为 NULL，表示只校验；也可直接拷出明文
 *        GCM/CBC/CBC_SHA 模式：out_payload 必须提供，容量需 >= payload_len
 * @param buf            [in]  镜像数据
 * @param len            [in]  镜像长度
 * @param cfg            [in]  读取配置（key/mac_key），不可为 NULL
 * @param out_payload    [out] 明文输出缓冲区，CRC/SHA 模式可为 NULL
 * @param payload_cap    [in]  out_payload 容量（字节）
 * @param out_view       [out] 解析结果，可为 NULL
 * @param out_plain_len  [out] 明文实际长度（去填充后），可为 NULL
 * @param out_crc_calc   [out] 实算 CRC32，可为 NULL（仅 CRC 模式有效）
 * @return ERR_OK 成功；否则见 err.h
 */
int image_read_payload(const uint8_t *buf, size_t len, const image_read_cfg_t *cfg,
                       uint8_t *out_payload, size_t payload_cap,
                       image_view_t *out_view, size_t *out_plain_len,
                       uint32_t *out_crc_calc);

#ifdef __cplusplus
}
#endif

#endif /* READ_H */
