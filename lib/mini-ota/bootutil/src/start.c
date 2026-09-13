/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file start.c
 * @brief ota的启动实现文件。
 * @author H-000-H
 * @note  运行时状态字 s_ota_state 为本文件内 static：符号不导出，外部无法 extern 触碰，
 *        只能通过 start.h 声明的函数读写（信息隐藏 + 全局唯一状态）；
 *        其中需要跨复位保留的位由 ota_state 状态区承载（位定义见 ota_state.h）
 */
#include "start.h"
#include "boot_redef.h"
#include "flash.h"
#include "err.h"
#include <stdint.h>
#include <stddef.h>
#include "boot_config.h"
#include "read.h"
#include "boot_keys.h"
#include "ota_state.h"
#include "sys/types.h"
/* OTA 运行时状态字（位定义见 ota_state.h） */
static volatile uint32_t s_ota_state = 0u;
static uint8_t load_buffer[MINI_BOOT_LOAD_MAX];/*默认走固定静态数组的如果内存想要优化可以放进栈里面但是栈小容易出问题固此处不放栈*/
static uint8_t verify_scratch[MINI_BOOT_LOAD_MAX]; /* 校验分块读取临时缓冲 */
static uint32_t s_downloaded_size = 0U; /* 最近一次成功下载的镜像总长度，0 表示尚无有效下载 */

/* 落盘：只改 mask 指定的持久位，其余位由 ota_state_update 从介质读回保留。
 * boot 与 app 是两份镜像、各有一份 s_ota_state：app 那份没加载过（为 0），
 * 若整字回写会把 current/fail_code 冲掉，所以一律走"读-改-写 + 掩码限定"。
 * 后端未注册 / 无有效记录时静默失败（早期启动阶段）。 */
static int state_update(uint32_t mask, uint32_t value)
{
    s_ota_state = (s_ota_state & ~mask) | (value & mask);
    return ota_state_update(mask, value);
}

/* 单 bit 落盘：value 非 0 置位、否则清位（mask 只含一个 bit） */
static int state_update_bit(uint32_t mask, uint32_t value)
{
    return state_update(mask, (value != 0u) ? mask : 0u);
}
/* ---------------- 设置 ---------------- */
/* 开关位属运行期配置（app 每次启动自行设置），只改 RAM，不落盘 */
void ota_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_OPEN, 1u);
}

void ota_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_OPEN, 0u);
}

void ota_rollback_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_ROLLBACK, 1u);
}

void ota_rollback_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_ROLLBACK, 0u);
}

/* 分区属持久位：boot 复位后要据此跳转，所以直接触发写state扇区 */
int ota_set_partition_image_0(void)
{
    return state_update_bit(OTA_STATE_MASK_CURRENT, OTA_STATE_PARTITION_IMAGE_0);
}

int ota_set_partition_image_1(void)
{
    return state_update_bit(OTA_STATE_MASK_CURRENT, OTA_STATE_PARTITION_IMAGE_1);
}
#if defined (DEBUG)
void ota_force_open(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_FORCE, 1u);
}

void ota_force_close(void)
{
    s_ota_state = ota_state_bit_put(s_ota_state, OTA_STATE_BIT_FORCE, 0u);
}
#endif

/* 失败码属持久位：要能跨复位被上层读到  */
int ota_fail_set(uint8_t code)
{
    return state_update(OTA_STATE_FAIL_MASK, ota_state_fail_encode((uint32_t)code));
}

/* ---------------- 读取 ---------------- */
uint8_t mini_boot_get_ota_status(void)
{
    return (uint8_t)(s_ota_state & OTA_STATE_MASK_STATUS_BYTE);
}

uint8_t ota_is_open(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_OPEN);
}

uint8_t ota_is_double(void)
{
    /* 只读：双分区是编译期就锁死的能力 */
    return (uint8_t)OTA_DUAL_PARTITION;
}

uint8_t ota_is_rollback(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_ROLLBACK);
}

#if defined (DEBUG)
uint8_t ota_is_force(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_FORCE);
}
#endif

uint8_t ota_fail_get(void)
{
    return (uint8_t)ota_state_fail_get(s_ota_state);
}

uint8_t ota_current_partition_get(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_CURRENT);
}

uint8_t ota_is_pending(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_PENDING);
}

uint8_t ota_is_trial(void)
{
    return (uint8_t)ota_state_bit_get(s_ota_state, OTA_STATE_BIT_TRIAL);
}

/* 从介质把持久位刷到 RAM（不做回滚判定）。
 * 返回 ERR_OK（含"无记录"，此时清持久位走默认）或后端错误（如 ERR_NOT_SUPPORTED）。 */
static int state_reload(void)
{
    uint32_t loaded = 0u;
    int rc = ota_state_load(&loaded);

    if (rc == ERR_OK)
    {
        /* 只恢复持久位；开关位是运行期配置，不被持久值覆盖 */
        s_ota_state = (s_ota_state & ~OTA_STATE_DURABLE_MASK) |
                      (loaded & OTA_STATE_DURABLE_MASK);
        return ERR_OK;
    }
    if (rc == ERR_OTA_STATE)
    {
        s_ota_state &= ~OTA_STATE_DURABLE_MASK; /* 首次上电/无有效记录：走默认，正常 */
        return ERR_OK;
    }
    return rc; /* 后端未注册（ERR_NOT_SUPPORTED）等 */
}

/* ---------------- 启动恢复 ----------------
 * boot 选区前调用一次：恢复持久位；若上次激活的新镜像未被确认（pending），
 * 第一次放行跳入新分区试运行（置 trial），第二次仍未确认才回滚到旧分区并记失败码。
 * 首次上电/无有效记录时走默认值。
 */
int mini_boot_state_load(void)
{
    int rc = state_reload();

    if (rc != ERR_OK)
    {
        return rc;
    }

    if (ota_is_pending() != 0u)
    {
        if (ota_is_trial() == 0u)
        {
            /* 刚激活后的首次复位：放行跳到 current 指向的新分区试运行，
             * 并把 trial 置 1 —— 新固件跑起来后 confirm 会清掉 pending+trial */
            (void)state_update_bit(OTA_STATE_MASK_TRIAL, 1u);
        }
        else
        {
            /* 已试运行过一次仍未被确认：新固件没起来（跑挂/掉电）→ 回滚到旧分区
             * （翻转 current + 清 pending/trial + 记失败码），一次落盘 */
            uint32_t resolved = s_ota_state;
            if (ota_state_resolve_pending(&resolved, OTA_FAIL_VERIFY) != 0)
            {
                uint32_t mask = OTA_STATE_MASK_CURRENT | OTA_STATE_MASK_PENDING |
                                OTA_STATE_MASK_TRIAL | OTA_STATE_FAIL_MASK;
                (void)state_update(mask, resolved & mask);
            }
        }
    }
    return ERR_OK;
}

/* app 侧读状态前调用：只刷新持久位到 RAM，不做回滚判定（回滚是 boot 的事） */
int mini_boot_state_refresh(void)
{
    return state_reload();
}

/* ---------------- flash 区域选择 ----------------
 * 当前分区   = bit6 指向的镜像区（boot 时跳转前校验用）
 * 非当前分区 = 另一片镜像区（下载目标 / 校验刚下载的镜像用）
 * 单分区模式下下载/校验目标固定为 image_0
 */
static uint32_t flash_active_area_id(void)
{
    return (ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1)
               ? (uint32_t)FLASH_AREA_ID_IMAGE_1
               : (uint32_t)FLASH_AREA_ID_IMAGE_0;
}

static uint32_t flash_inactive_area_id(void)
{
    if (!ota_is_double())
    {
        return (uint32_t)FLASH_AREA_ID_IMAGE_0;
    }
    return (ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1)
               ? (uint32_t)FLASH_AREA_ID_IMAGE_0
               : (uint32_t)FLASH_AREA_ID_IMAGE_1;
}

/* ---------------- OTA 主流程 ---------------- */
/* 镜像读取回调上下文：area 只在发起校验前解析/打开一次 */
typedef struct
{
    const flash_area_t *area; /* 镜像所在区域；镜像须从 area 偏移 0 开始，offset 移到数据区 */
} flash_read_ctx_t;

static int flash_image_read_fn(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const flash_read_ctx_t *c = (const flash_read_ctx_t *)ctx;
    if ((c == NULL) || (c->area == NULL))
    {
        return ERR_ARG;
    }
    return flash_area_read_operation(c->area, offset, buf, len);
}

int mini_boot_source_download_stream(down_load_hook hook, void *param, uint32_t total_len)
{
    if (!ota_is_open())
    {
#if defined(DEBUG)
        /* TODO: 调试钩子 */
#endif
        return ERR_OTA_OPEN;
    }
    if ((hook == NULL) || (total_len == 0U))
    {
        return ERR_ARG;
    }

    /* 下载目标：非当前运行分区（单分区即 image_0） */
    const flash_area_t *area = NULL;
    int result = flash_area_open(flash_inactive_area_id(), &area);
    if (result != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return result;
    }

    /*统一擦除函数都是从最开始就开擦除了所以直接一开始擦除足够内存 */
    result = flash_area_erase_operation(area, 0U, total_len);
    if (result != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return result;
    }

    uint32_t received = 0U;
    while (received < total_len)
    {
        uint32_t want = total_len - received;
        if (want > MINI_BOOT_LOAD_MAX)
        {
            want = MINI_BOOT_LOAD_MAX;
        }

        int raw = 0;
        result = hook(param, load_buffer, want, &raw);
        if ((result != ERR_OK) || (raw <= 0) || ((uint32_t)raw > want))
        {
            ota_fail_set(OTA_FAIL_READ);
            return ERR_TRANSMIT;
        }

        result = flash_area_write_operation(area, received, load_buffer, (uint32_t)raw);
        if (result != ERR_OK)
        {
            ota_fail_set(OTA_FAIL_WRITE);
            return result;
        }
        received += (uint32_t)raw;
    }

    s_downloaded_size = total_len;
    return ERR_OK;
}

int mini_boot_start_ota(void)
{
    if (!ota_is_open())
    {
        return ERR_OTA_OPEN;
    }
    if (s_downloaded_size == 0U)
    {
        return ERR_ARG; /* 尚未成功下载过镜像 */
    }

    /* 校验对象 = 刚下载的新镜像（非当前分区）；meta 在标记末尾无论是否front，长度用下载记录值 */
    flash_read_ctx_t rctx = {NULL};
    int rc = flash_area_open(flash_inactive_area_id(), &rctx.area);
    if (rc != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_WRITE);
        return rc;
    }

    image_read_cfg_t cfg = {0}; /* CRC/SHA 模式无需密钥；加密模式需先 boot_key_set() 装入 */
#if IMAGE_CRYPTO_ENABLE
    cfg.key = boot_key_get(&cfg.key_len);
#endif
    uint32_t crc = 0U;
    rc = image_verify_stream(flash_image_read_fn, &rctx, s_downloaded_size, &cfg,
                             verify_scratch, sizeof(verify_scratch), &crc);
#if IMAGE_CRYPTO_ENABLE
    boot_key_wipe(); /*无论成败都会清密钥避免上层拿密钥 */
#endif
    if (rc != ERR_OK)
    {
        ota_fail_set(OTA_FAIL_VERIFY);
        return rc;
    }

    /* 校验通过：激活新分区（bit6 切到刚下载的分区）+ 按需置 pending；
     * 同时清掉上次的失败码（它描述的是上一次 OTA 的结果）与 trial
     * （新的一轮试运行，trial 必须归零，否则 boot 会直接判回滚） */
    uint32_t mask = OTA_STATE_MASK_CURRENT | OTA_STATE_MASK_PENDING |
                    OTA_STATE_MASK_TRIAL | OTA_STATE_FAIL_MASK;
    uint32_t value = 0u;
    if (flash_inactive_area_id() == (uint32_t)FLASH_AREA_ID_IMAGE_1)
    {
        value |= OTA_STATE_MASK_CURRENT;
    }
    /* 双分区 + 回滚开启时才需要待确认：单分区没有可回退的旧镜像 */
    if (ota_is_double() && ota_is_rollback())
    {
        value |= OTA_STATE_MASK_PENDING;
    }
    int rc_state = state_update(mask, value);
    if (ota_is_double() && (rc_state != ERR_OK))
    {
        return rc_state;
    }
    return ERR_OK;
}

int mini_boot_backup(mini_boot_backup_param_t *param)
{
    flash_read_ctx_t rctx = {NULL};
    image_read_cfg_t cfg = {0};
    int rc;

    if ((param == NULL) || (param->size < (IMAGE_CRC_LEN + IMAGE_META_LEN)))
    {
        return ERR_ARG;
    }

    rc = flash_area_open((uint32_t)param->partition, &rctx.area);
    if (rc != ERR_OK)
    {
        return rc;
    }
    if ((rctx.area == NULL) || (param->size > rctx.area->fa_size))
    {
        return ERR_ARG;
    }

    cfg.key = param->key;
    cfg.key_len = param->key_len;
    cfg.mac_key = param->mac_key;
    cfg.mac_key_len = param->mac_key_len;

    /* 复用 read.c：模式/aux/摘要全由镜像自描述 meta 决定，不在核心层重造校验 */
    return image_verify_stream(flash_image_read_fn, &rctx, param->size,
                               &cfg, verify_scratch, (uint32_t)sizeof(verify_scratch), NULL);
}

int mini_boot_confirm_ota(void)
{
    /* app 运行正常：清 pending + trial（试运行通过，此后不再回滚）。
     * 后端未注册时返回 ERR_NOT_SUPPORTED，让"confirm 其实没落盘"当场暴露。 */
    return state_update(OTA_STATE_MASK_PENDING | OTA_STATE_MASK_TRIAL, 0u);
}
