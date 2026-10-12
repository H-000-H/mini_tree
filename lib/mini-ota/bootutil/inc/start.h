/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 * @file start.h
 * @brief ota的启动头文件。ota使用只需调用该头文件即可。
 * @author H-000-H
 * @note  状态变量本体是 start.c 内的 static（对外彻底隐藏），
 *        外部只能通过本文件声明的函数读写，不要尝试 extern 它。
 *  boot:   flash ──load()──► s_ota_state（只回填持久位，开关位留给 app 设置）
 *  运行:   s_ota_state ──state_update()──► flash（只改指定的持久位，其余位读回保留）
 */
#ifndef BOOTUTIL_INC_START_H
#define BOOTUTIL_INC_START_H
#if defined(__cplusplus)
extern "C"
{
#endif
#include <stdint.h>

#include "read.h"

/* ---------------- OTA 状态位定义 ----------------
 * bit0   OTA 开关              0=关 1=开
 * bit1   保留                  （双分区是编译期能力，宏 OTA_DUAL_PARTITION）
 * bit2   回滚开关              0=关 1=开
 * bit3   强制 OTA              仅 DEBUG 编译有效
 * bit4~5 失败码                0=成功 1=读bin失败 2=写flash失败 3=校验失败
 * bit 6  目前分区              0=image_0 1=image_1
 * bit 7  pending               1=新镜像已激活但未确认
 * bit 16 trial                 1=已跳入新分区试运行一次仍未被确认 → 下次复位回滚
 */

/* ---- 失败码（ota_fail_set 入参 / ota_fail_get 返回值）---- */
#define OTA_FAIL_NONE   0U /* 成功，无失败 */
#define OTA_FAIL_READ   1U /* 读 bin 失败 */
#define OTA_FAIL_WRITE  2U /* 写 flash 失败 */
#define OTA_FAIL_VERIFY 3U /* 校验失败 */

/* ---- 设置 ---- */
/* 开关位（open/rollback/force）只改 RAM，不落盘；app 每次启动自行设置 */
void ota_open(void);             /* bit0 置 1：开启 OTA */
void ota_close(void);            /* bit0 清 0：关闭 OTA */
void ota_rollback_open(void);    /* bit2 置 1：开启回滚 */
void ota_rollback_close(void);   /* bit2 清 0：关闭回滚 */
#if defined (DEBUG)
void ota_force_open(void);       /* bit3 置 1：强制 OTA（仅 DEBUG 编译有效） */
void ota_force_close(void);      /* bit3 清 0 */
#endif

/* 以下均属持久位：改动即落盘（读-改-写，只动自己那几位）；
 * 返回 ERR_NOT_SUPPORTED 表示状态后端未注册 —— 即“没写下去” */
int ota_set_partition_image_0(void); /* bit6 写当前分区=image_0 */
int ota_set_partition_image_1(void); /* bit6 写当前分区=image_1 */
int ota_fail_set(uint8_t code);      /* bit4~5 写失败码（OTA_FAIL_xxx，超出按低 2 位截断） */

/* ---- 读取 ---- */
uint8_t mini_boot_get_ota_status(void); /* 整个状态字节 */
uint8_t ota_is_open(void);              /* 读 bit0 */
uint8_t ota_is_double(void);            /* 只读：双分区能力（编译期常量 OTA_DUAL_PARTITION） */
uint8_t ota_is_rollback(void);          /* 读 bit2 */
#if defined (DEBUG)
uint8_t ota_is_force(void);             /* 读 bit3 */
#endif
uint8_t ota_fail_get(void);             /* 读 bit4~5 解码后的失败码（OTA_FAIL_xxx） */
uint8_t ota_is_pending(void);           /* 读 bit7 待确认标志 */
uint8_t ota_is_trial(void);             /* 读 bit16 试运行标志（已跳入新分区一次但未确认） */
uint8_t ota_current_partition_get(void); /* 读 bit6 当前分区（0=image_0 1=image_1） */

/* ---- OTA 主流程 ---- */
/**
 * @brief 下载钩子（平台实现：从网络/串口等来源取数填充 buf）
 * @param[in] param 用户透传参数
 * @param[out] buf 本次数据缓冲区（最多写 want 字节，不得越界）
 * @param[in] want 本次请求的字节数（不超过 MINI_BOOT_LOAD_MAX，末段为剩余字节数）
 * @param[out] out_len 实际取到的字节数（0 < out_len <= want，允许小于 want）
 * @return ERR_OK 成功；其他值按传输失败处理
 */
typedef int (*down_load_hook)(void *param, uint8_t *buf, uint32_t want, int *out_len);

/**
 * @brief 流式下载固件到目标分区（内部先 open 目标 area 并一次性擦除后分段写入）
 * @param[in] hook 自己的数据传输钩子
 * @param[in] param 透传给钩子的用户参数
 * @param[in] total_len 固件总长度（字节）
 * @return ERR_OK 成功，负数为失败原因（见 err.h）
 */
int mini_boot_source_download_stream(down_load_hook hook,void* param,uint32_t total_len);

/**
 * @brief 校验刚下载的镜像（非当前分区），通过后激活新分区；双分区+回滚开启时置 pending
 * @return ERR_OK 校验通过并已激活新分区；负数为失败原因（见 err.h）
 * @note  激活后需由 app 调用 mini_boot_confirm_ota() 确认；未确认则下次复位由
 *        mini_boot_state_load() 回滚到旧分区
 * @note  加密镜像（GCM/CBC/CBC_SHA）需先经 boot_keys.h 的 boot_key_set() 装入密钥，
 *        校验结束后无论成败内部自动 wipe；CRC/SHA 模式无需密钥
 */
int mini_boot_start_ota(void);

/**
 * @brief 启动时恢复持久状态（boot 选区前调用一次）
 * @note  若上次激活的新镜像未被 app 确认（pending），内部回滚到另一分区并记录失败码
 * @return ERR_OK 已恢复（首次上电无记录也返回 ERR_OK，走默认值）；
 *         后端未注册时返回 ERR_NOT_SUPPORTED（说明状态区没接上，需平台先注册后端）
 */
int mini_boot_state_load(void);

/**
 * @brief app 侧读状态前调用：只把持久位从介质刷进 RAM，**不做回滚判定**
 * @return ERR_OK（含首次上电无记录）；后端未注册返回 ERR_NOT_SUPPORTED
 * @note  回滚判定只在 mini_boot_state_load() 里做；app 不要拿它当"读当前状态"的替代
 */
int mini_boot_state_refresh(void);

/**
 * @brief 新镜像运行确认（app 侧运行正常后调用）：清 pending，不再回滚
 * @return ERR_OK 已落盘；ERR_NOT_SUPPORTED 后端未注册（什么都没写，需平台先注册后端）
 * @note  只改 pending（读-改-写），不会动当前分区/失败码
 */
int mini_boot_confirm_ota(void);

/* ---------------- 分区摘要校验（回滚前查备用分区是否完好） ---------------- */

/** 分区镜像校验参数（镜像自带 meta，模式/摘要/aux 无需调用方传） */
typedef struct
{
    int partition;          /**< flash_area_id_t：待校验分区 */
    uint32_t size;          /**< 镜像总长度（字节，含末尾自描述 meta） */
    const uint8_t *key;     /**< 密钥：GCM/CBC/CBC_SHA 必填（16/24/32B）；CRC/SHA 可置 NULL */
    uint32_t key_len;       /**< 密钥长度（字节） */
    const uint8_t *mac_key; /**< MAC 密钥：仅 CBC_SHA；为 NULL 时回退用 key */
    uint32_t mac_key_len;   /**< mac_key 长度；mac_key 为 NULL 时忽略 */
} mini_boot_backup_param_t;

/**
 * @brief 校验分区内的 OTA 镜像是否完好（直接复用 read.c 的 image_verify_stream）
 *        模式（CRC/SHA/GCM/CBC/CBC_SHA）与 aux（摘要/nonce/iv/tag/hmac）均由镜像
 *        自描述 meta 决定，调用方只需给出分区、镜像长度与密钥
 * @param param [in] 校验参数；不可为 NULL，size 为镜像总长度（不含分区尾部空白）
 * @return ERR_OK 校验/认证通过；ERR_CRC_MISMATCH / ERR_HASH_MISMATCH / ERR_AUTH_FAILED；
 *         ERR_ARG 入参非法或 meta 非法；ERR_TOO_SMALL 长度不足；ERR_NOT_SUPPORTED 未编入加密；
 *         其余见 err.h（如 flash 读失败）
 * @note  典型用途：回滚前/跳转前确认备用分区镜像完好（回滚判定本身不再内置该检查）
 */
int mini_boot_backup(mini_boot_backup_param_t *param);

#if defined(__cplusplus)
}
#endif

#endif /* BOOTUTIL_INC_START_H */
