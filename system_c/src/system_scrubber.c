/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file system_scrubber.c
 *@brief system scrubber 实现
 *@author H-000-H
 *@details
 *   system_scrubber (C 实现) — Flash bit-rot 巡检任务
 *   定期对"当前运行的镜像分区"做纯数据校验 (mini-ota 的 image_verify_area,
 *   不解析镜像头 / 尾部 meta), 与构建期基线比对; 失配即 enter_safe_state,
 *   防止固件位翻转静默运行。
 *   校验原语与 CRC 模型统一来自 mini-ota, 本模块不再自带 CRC 表。
 */

#include "system_scrubber.h"

#include "safe_state.h"
#include "system_cfg.h"
#include "system_scrubber_config.h"
#include "system_wdt.h"

#include "flash.h"  /* flash_area_id_t */
#include "start.h"  /* ota_current_partition_get */
#include "verify.h" /* image_verify_area */

#include "mini_time.h"

#include "compiler_compat_poison.h"

static const char*    k_tag = "Scrubber";
static const uint32_t k_scrubber_prio =
#if defined(CONFIG_OS_FREERTOS)
    1; /* FreeRTOS: 0=最低, 31=最高 — 后台巡检, 最低优先级 */
#else
    30; /* RT-Thread: 0=最高, 31=最低 — 后台巡检, 最低优先级 */
#endif
static const uint32_t k_scrubber_stack = 2048;

static mini_task_handle_t s_handle = NULL;
static volatile bool      s_running = false;

/**
 * @brief Flash 巡检后台任务: 定期对当前镜像分区做纯数据校验
 * @param[in] param 任务参数 (未使用)
 * @note  基线未配置 (CRC 或长度任一为 0) 时任务自行退出, 不空转。
 */
static void scrubber_task(void* param)
{
    (void)param;

    const uint32_t baseline_crc = SYSTEM_SCRUBBER_CRC_BASELINE;
    const uint32_t image_len = SYSTEM_SCRUBBER_IMAGE_LEN;

    if (baseline_crc == 0U || image_len == 0U)
    {
        MT_LOG_WARN(k_tag, "baseline not set (crc=0x%08X len=%u) — scrubber inactive, run post_build_crc.py", (unsigned)baseline_crc,
                    (unsigned)image_len);
        s_running = false;
        mini_task_self_delete();
        return;
    }

    /* 当前运行的镜像在 IMAGE_0 / IMAGE_1 之间, 由 OTA 持久状态决定 */
    const uint32_t area_id = (uint32_t)FLASH_AREA_ID_IMAGE_0 + (uint32_t)ota_current_partition_get();

    MT_LOG_INFO(k_tag, "scrubber started: area=%u len=%u bytes interval=%ums baseline=0x%08X", (unsigned)area_id, (unsigned)image_len,
                (unsigned)SYSTEM_SCRUBBER_INTERVAL_MS, (unsigned)baseline_crc);

    while (s_running)
    {
        system_wdt_feed_iwdg();

        int ret = image_verify_area(area_id, 0U, image_len, baseline_crc);

        if (ret == ERR_CRC_MISMATCH)
        {
            MT_LOG_ERROR(k_tag, "FLASH CORRUPTION: area=%u len=%u baseline=0x%08X", (unsigned)area_id, (unsigned)image_len, (unsigned)baseline_crc);
            enter_safe_state("Flash bit-rot detected — firmware corruption");
        }
        else if (ret == ERR_OK)
        {
            MT_LOG_INFO(k_tag, "scrub pass complete: area=%u OK", (unsigned)area_id);
        }
        else
        {
            /* 读失败 / 分区未注册等: 记账并等下一轮, 不误判成位翻转 */
            MT_LOG_WARN(k_tag, "scrub pass skipped: image_verify_area ret=%d", ret);
        }

        mini_delay_ms(SYSTEM_SCRUBBER_INTERVAL_MS);
    }

    MT_LOG_INFO(k_tag, "scrubber task exiting");
    mini_task_self_delete();
}

/**
 * @brief 启动 CRC 巡检任务
 * @return MINI_OK 成功; MINI_ERR_NOMEM 任务创建失败
 */
mt_err_t system_scrubber_start(void)
{
    if (s_running)
        return MINI_OK;

    s_running = true;
    int ret = mini_task_create_handle("scrubber", k_scrubber_stack, k_scrubber_prio, scrubber_task, NULL, 0, &s_handle);
    if (ret != 0)
    {
        MT_LOG_ERROR(k_tag, "failed to create scrubber task");
        s_running = false;
        return MINI_ERR_NOMEM;
    }

    MT_LOG_INFO(k_tag, "scrubber task created, prio=%u", (unsigned)k_scrubber_prio);
    return MINI_OK;
}

/**
 * @brief 是否运行
 * @return true
 */
bool system_scrubber_is_running(void) { return s_running; }
