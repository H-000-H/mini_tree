/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot.c
 * @author: H-000-H
 * @brief: bootloader 主流程实现（平台无关）
 */

#include "flash.h"
#include "err.h"
#include "read.h"
#include "boot.h"
#include "boot_redef.h"
#include "boot_config.h"
#include "start.h" /* mini_boot_backup：可选的跳转前整包校验 */
#if IMAGE_CRYPTO_ENABLE
#include "boot_keys.h" /* 加密镜像整包校验用的密钥槽 */
#endif
#include <stdint.h>
#define APP_ENTRY_ADDR (BOOT_APP_BASE + 4)
static inline int mini_boot_set_systick(uint32_t ctrl, uint32_t val)
{
    MINI_BOOT_SYSTICK_BASE->CTRL = ctrl;
    MINI_BOOT_SYSTICK_BASE->VAL = val;
    return ERR_OK;
}

static inline int mini_boot_clear_nvic(void)
{
    for (int i = 0; i < 16; i++) 
    {
        MINI_BOOT_NVIC_BASE->ICER[i] = 0xFFFFFFFF;
        MINI_BOOT_NVIC_BASE->ICPR[i] = 0xFFFFFFFF;
    }
    return ERR_OK;
}

int boot_jump_switch_app(mini_boot_app_area_t app_area)
{
    const mini_boot_vector_t *vt = MINI_BOOT_VECTOR_TABLE(app_area.initial_addr);

     /* VTOR 要求 128 字节对齐 */
    if ((app_area.initial_addr == 0U) || ((app_area.initial_addr & 0x7FU) != 0U))
    {
        return ERR_ARG;                      
    }
    uint32_t app_sp = vt->initial_sp;         /* 初始 MSP（表内第 0 个字） */
    uint32_t app_pc = vt->reset_handler;      /* Reset_Handler（表内第 1 个字） */

    if ((app_sp < SRAM_START_ADDR) || (app_sp > SRAM_END_ADDR) || ((app_sp & 0x3U) != 0U))
    {
        return ERR_ARG;
    }

    /* 复位入口必须落在本 app 分区内，且为 Thumb 状态(bit0=1) */
    if ((app_pc < app_area.initial_addr) ||
        (app_pc >= (app_area.initial_addr + app_area.size)) ||
        ((app_pc & 0x1U) == 0U))
    {
        return ERR_ARG;
    }

    /* 向量表合法后、真正跳转前：可选的整包校验（image_len != 0 时启用）。
     * 镜像曾被下载时校验/确认过，这里额外挡一下位翻转/误擦写，避免跳进坏镜像。 */
    if (app_area.image_len != 0U)
    {
        mini_boot_backup_param_t vp = {0};
        int vrc;

        vp.partition = (int)app_area.fa_id;
        vp.size = app_area.image_len;
#if IMAGE_CRYPTO_ENABLE
        {
            size_t klen = 0U;
            vp.key = boot_key_get(&klen);
            vp.key_len = (uint32_t)klen;
        }
#endif
        vrc = mini_boot_backup(&vp);
        if (vrc != ERR_OK)
        {
            return vrc;
        }
    }

    (void)mini_boot_irq_disable();
    mini_boot_set_systick(0, 0);
    mini_boot_clear_nvic();

    mini_boot_set_vtor(app_area.initial_addr);
    mini_boot_psp_to_msp();
    mini_boot_set_psp(0);
    mini_boot_dsb();
    mini_boot_isb();

    /* 切栈 + 跳转交给 arch 层汇编（noreturn）：不在内联汇编里动 SP，
       避免 GCC "asm 前后 SP 不变" 的假设被破坏，也不必用无效的 "sp" clobber */
    mini_boot_jump_to_app(app_sp, app_pc);

    /* noreturn，执行不到这里；保留死循环兜底 */
    for(;;);
}


