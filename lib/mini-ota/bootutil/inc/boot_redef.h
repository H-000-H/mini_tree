/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file: boot_redef.h
 * @author: H-000-H
 * @brief: 平台/体系结构适应接口
 *         核相关原语（开关中断、屏障、栈指针、VTOR、跳转）由 arch/arm/cortex-m/cortex_m.S 实现，
 *         M0/M3/M4/M7 在同一份汇编里用宏切换；本文件只做声明。
 *         NVIC/SysTick/SCB 等核外设寄存器地址在此以结构体定义（ARM 架构规定地址，与芯片厂商无关）。
 */
#ifndef BOOTUTIL_INC_BOOT_REDEF_H
#define BOOTUTIL_INC_BOOT_REDEF_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stddef.h> /* offsetof：用于向量表布局静态断言 */
#include "boot_config.h"

#if defined(__GNUC__) || defined(__clang__)
#define MINI_BOOT_NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define MINI_BOOT_NORETURN __declspec(noreturn)
#else
#define MINI_BOOT_NORETURN
#endif

/*-------------------------------------------------------------------------------------------------------*/
/*------------------------- 体系结构层接口（实现：arch/arm/cortex-m/cortex_m.S）-------------------------*/
/*-------------------------------------------------------------------------------------------------------*/
/**
 * @brief: 关闭可屏蔽中断，并返回关闭前的 PRIMASK 状态
 * @return: 关闭前的 PRIMASK（0=原本中断开启，1=原本已关闭）
 */
int mini_boot_irq_disable(void);

/**
 * @brief: 恢复中断状态
 * @param state: mini_boot_irq_disable() 的返回值
 */
void mini_boot_irq_enable(uint32_t state);

/** @brief: 指令同步屏障 */
void mini_boot_isb(void);

/** @brief: 数据同步屏障 */
void mini_boot_dsb(void);

/** @brief: 设置主栈指针 MSP */
void mini_boot_set_msp(uint32_t m_msp);

/** @brief: 设置进程栈指针 PSP */
void mini_boot_set_psp(uint32_t psp);

/** @brief: 切回 MSP（清 CONTROL.SPSEL），跳转前确保处于复位态 */
void mini_boot_psp_to_msp(void);

/**
 * @brief: 设置中断向量表地址
 * @param vtor_addr: 向量表基址（需 128 字节对齐）
 * @return: ERR_OK 成功；Cortex-M0 无 VTOR 寄存器，返回 ERR_NOT_SUPPORTED
 */
int mini_boot_set_vtor(uint32_t vtor_addr);

/**
 * @brief: 切换栈并跳转到 app（不返回）
 * @param msp:   app 的初始栈顶（向量表第 0 个字）
 * @param entry: app 的复位入口（向量表第 1 个字，Thumb 位为 1）
 */
MINI_BOOT_NORETURN void mini_boot_jump_to_app(uint32_t msp, uint32_t entry);

/*-------------------------------------------------------------------------------------------------------*/
/*-------------------------------------------------寄存器定义区-------------------------------------------*/
/*-------------------------------------------------------------------------------------------------------*/
#define MINI_BOOT_SCB_VTOR  (*(volatile uint32_t *)0xE000ED08)
#define MINI_BOOT_SYSTICK_BASE  ((struct mini_boot_systick *)(0xE000E000UL+0x010UL))
#define MINI_BOOT_NVIC_BASE  ((struct mini_boot_nvic *)(0xE000E000UL+0x0100UL))

struct mini_boot_systick
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
};

struct mini_boot_nvic
{
  volatile uint32_t ISER[16U];              /*!< Offset: 0x000 (R/W)  Interrupt Set Enable Register */
        uint32_t RESERVED0[16U];
  volatile uint32_t ICER[16U];              /*!< Offset: 0x080 (R/W)  Interrupt Clear Enable Register */
        uint32_t RSERVED1[16U];
  volatile uint32_t ISPR[16U];              /*!< Offset: 0x100 (R/W)  Interrupt Set Pending Register */
        uint32_t RESERVED2[16U];
  volatile uint32_t ICPR[16U];              /*!< Offset: 0x180 (R/W)  Interrupt Clear Pending Register */
        uint32_t RESERVED3[16U];
  volatile uint32_t IABR[16U];              /*!< Offset: 0x200 (R/W)  Interrupt Active bit Register */
        uint32_t RESERVED4[16U];
  volatile uint32_t ITNS[16U];              /*!< Offset: 0x280 (R/W)  Interrupt Non-Secure State Register */
        uint32_t RESERVED5[16U];
  volatile uint32_t IPR[124U];              /*!< Offset: 0x300 (R/W)  Interrupt Priority Register */
};

/*-------------------------------------------------------------------------------------------------------*/
/*-------------------------------Cortex-M 向量表（ARM 架构规定布局）-------------------------------------*/
/*-------------------------------------------------------------------------------------------------------*/
/**
 * @brief: 向量表头部结构体（ARM 架构硬性规定：+0 初始 MSP，+4 复位入口）
 */
typedef struct
{
    volatile uint32_t initial_sp;      /* +0x00: 初始主栈顶 MSP（app 链接脚本的 _estack） */
    volatile uint32_t reset_handler;   /* +0x04: 复位入口（末位 Thumb 位须为 1） */
} mini_boot_vector_t;

/** @brief: 取指定地址处的向量表；调用方需保证地址 128 字节对齐且镜像已校验 */
#define MINI_BOOT_VECTOR_TABLE(addr) ((const mini_boot_vector_t *)(uintptr_t)(addr))

#if !defined(__cplusplus) && !defined(_MSC_VER)
_Static_assert(offsetof(mini_boot_vector_t, reset_handler) == 4U,
               "mini_boot_vector_t 布局必须匹配 ARM 向量表：+0 MSP,+4 复位入口");
#endif

/**
 * @brief 系统复位（平台实现）
 * @return 无直接bootloar了也不需要返回值
 * @note  
    - 平台实现需保证调用后系统软复位(此处可和jump一样硬跳但是我感觉没必要直接软复位就行)，
    - 返回值仅表示是否成功触发复位
 */
void mini_boot_system_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BOOTUTIL_INC_BOOT_REDEF_H */
