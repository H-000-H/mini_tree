@ SPDX-License-Identifier: Apache-2.0
@ file: cortex_m.S
@ brief: Cortex-M 体系结构层实现（M0/M3/M4/M7 统一一份文件，用宏切换核差异）
@        声明见 bootutil/inc/boot_redef.h；参数传递遵循 AAPCS（r0=第1参数，r1=第2参数）
@ note:  文件名必须为大写 .S —— 这样才会经过预处理器，#if defined(MINI_BOOT_Mx) 才生效
@
@ 核差异汇总：
@   - VTOR：仅 Cortex-M0 没有（M0+ 起才有），故 MINI_BOOT_M0 下 mini_boot_set_vtor 返回 ERR_NOT_SUPPORTED；
@           M0+ 用独立宏 MINI_BOOT_M0P 选择（与 M0 区分），走写 VTOR 分支
@   - CONTROL：各核均支持读写，这里只清 SPSEL(bit1) 切回 MSP

@ 目标核由构建方提供：必须定义 MINI_BOOT_M0/M0P/M3/M4/M7 之一，并配套 -mcpu
#if !defined(MINI_BOOT_M0) && !defined(MINI_BOOT_M0P) && !defined(MINI_BOOT_M3) && \
    !defined(MINI_BOOT_M4) && !defined(MINI_BOOT_M7)
#error "未定义目标核：请定义 MINI_BOOT_M0/M0P/M3/M4/M7 之一（与 -mcpu 保持一致）"
#endif

    .syntax unified
    .thumb

@-------------------------------------------------------------------------------------------------------
@ int mini_boot_irq_disable(void)：返回关闭前的 PRIMASK，并关闭可屏蔽中断
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_irq_disable,"ax",%progbits
    .global mini_boot_irq_disable
    .type   mini_boot_irq_disable, %function
    .thumb_func
mini_boot_irq_disable:
    mrs   r0, PRIMASK
    cpsid i
    bx    lr

@-------------------------------------------------------------------------------------------------------
@ void mini_boot_irq_enable(uint32_t state)：恢复 PRIMASK
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_irq_enable,"ax",%progbits
    .global mini_boot_irq_enable
    .type   mini_boot_irq_enable, %function
    .thumb_func
mini_boot_irq_enable:
    msr   PRIMASK, r0
    bx    lr

@-------------------------------------------------------------------------------------------------------
@ 屏障：void mini_boot_isb(void) / void mini_boot_dsb(void)
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_isb,"ax",%progbits
    .global mini_boot_isb
    .type   mini_boot_isb, %function
    .thumb_func
mini_boot_isb:
    isb
    bx    lr

    .section .text.mini_boot_dsb,"ax",%progbits
    .global mini_boot_dsb
    .type   mini_boot_dsb, %function
    .thumb_func
mini_boot_dsb:
    dsb
    bx    lr

@-------------------------------------------------------------------------------------------------------
@ 栈指针：void mini_boot_set_msp(uint32_t) / void mini_boot_set_psp(uint32_t)
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_set_msp,"ax",%progbits
    .global mini_boot_set_msp
    .type   mini_boot_set_msp, %function
    .thumb_func
mini_boot_set_msp:
    msr   MSP, r0
    bx    lr

    .section .text.mini_boot_set_psp,"ax",%progbits
    .global mini_boot_set_psp
    .type   mini_boot_set_psp, %function
    .thumb_func
mini_boot_set_psp:
    msr   PSP, r0
    bx    lr

@-------------------------------------------------------------------------------------------------------
@ void mini_boot_psp_to_msp(void)：清 CONTROL.SPSEL，切回 MSP
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_psp_to_msp,"ax",%progbits
    .global mini_boot_psp_to_msp
    .type   mini_boot_psp_to_msp, %function
    .thumb_func
mini_boot_psp_to_msp:
    mrs   r0, CONTROL
    movs  r2, #2
    bics  r0, r2              @ SPSEL(bit1)=0 -> 使用 MSP
    msr   CONTROL, r0
    isb                       @ 改 CONTROL 后必须 ISB
    bx    lr

@-------------------------------------------------------------------------------------------------------
@ int mini_boot_set_vtor(uint32_t addr)：写 SCB->VTOR
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_set_vtor,"ax",%progbits
    .global mini_boot_set_vtor
    .type   mini_boot_set_vtor, %function
    .thumb_func
mini_boot_set_vtor:
#if defined(MINI_BOOT_M0)
    @ 仅 Cortex-M0 无 VTOR 寄存器（注意：M0+ 有，走下面 #else 分支），返回 ERR_NOT_SUPPORTED(-131)
    movs  r0, #131
    rsbs  r0, r0, #0          @ r0 = -131（不用 negs，兼容 ARMv6-M）
    bx    lr
#else
    @ M0+ / M3 / M4 / M7：写 SCB->VTOR
    ldr   r1, =0xE000ED08     @ SCB->VTOR（SCS 0xE000E000 + 0xD00 + 0x08）
    str   r0, [r1]
    movs  r0, #0              @ ERR_OK
    bx    lr
#endif

@-------------------------------------------------------------------------------------------------------
@ void mini_boot_jump_to_app(uint32_t msp, uint32_t entry)：切栈并跳转，不返回
@ r0 = app 初始栈顶, r1 = app 复位入口（Thumb 位为 1）
@ note: .Ljump_hang 为兜底死循环（实际执行不到：bx 后不会返回，真出问题由 HardFault 兜底）
@-------------------------------------------------------------------------------------------------------
    .section .text.mini_boot_jump_to_app,"ax",%progbits
    .global mini_boot_jump_to_app
    .type   mini_boot_jump_to_app, %function
    .thumb_func
mini_boot_jump_to_app:
    movs  r3, #0
    msr   CONTROL, r3         @ 复位态：特权级 + 使用 MSP
    isb
    msr   MSP, r0             @ 切到 app 的栈
    dsb
    isb
    bx    r1                  @ 跳到 app 复位入口，不再返回

.Ljump_hang:                  @ 局部标签(不污染符号表)：bx 不返回，此处仅为兜底陷阱
    b     .Ljump_hang

    .ltorg

    .equ AIRCR, (0xE000E000 + 0x0D00 + 0x00C) @ Application Interrupt and Reset Control Register
@ 软复位直接参考cmsis的软复位写的(可修改为直跳bootloar版本)
    .section .text.mini_boot_system_reset,"ax",%progbits
    .global mini_boot_system_reset
    .type   mini_boot_system_reset, %function
    .thumb_func
mini_boot_system_reset:
    ldr   r0, =AIRCR         
    ldr   r1, =0x05FA0004     @ VECTKEY(0x5FA<<16) | SYSRESETREQ(bit2)
    dsb                       
    str   r1, [r0]           
    dsb                      
.Lreset_hang:
    b     .Lreset_hang        @ 等待复位生效（等价于 cmsis 的 for(;;){__NOP()}）
    .ltorg                   

