@ SPDX-License-Identifier: Apache-2.0
@ file port.S
@ Cortex-M port: PendSV context switch, first-thread start, yield trigger, SVC handler
@ Uppercase .S: preprocessed by CPP (headers/macros may be included if needed).
@ The stack frame layout matches mini_os_thread_stack_init in thread.c:
@ low -> high: r4..r11, R0..R3, R12, LR, PC, xPSR.
#include "mini_config.h"
    @ one-gate check: unknown MINI_OS_ARCH values must not silently fall into
    @ any #else branch below (they are M3+/M7-shared); fail the build instead
#if (MINI_OS_ARCH != MINI_OS_ARCH_M0) && (MINI_OS_ARCH != MINI_OS_ARCH_M3) && \
    (MINI_OS_ARCH != MINI_OS_ARCH_M4) && (MINI_OS_ARCH != MINI_OS_ARCH_M7)
#error "mini-os port.S: unsupported MINI_OS_ARCH value (expected 0=M0, 1=M3, 2=M4, 3=M7)"
#endif
    .syntax unified
    .thumb

    .extern mini_os_current_thread
    .extern mini_os_schedule_switch
    .extern g_svc_cb
    .extern g_svc_arg
    .extern mini_os_svc_dispatch
    .extern mini_os_yield_trigger
    .equ    MINI_OS_EXC_RETURN_THREAD_PSP, 0xFFFFFFFD  @ EXC_RETURN: return to Thread mode, use PSP
    .equ    NVIC_ICSR,                      0xE000ED04  @ Interrupt Control and State Register
    .equ    NVIC_PENDSVSET,                 (1u<<28)  @ PendSV set-pending bit
    .equ    NVIC_PENDSVCLR,                 (1u<<27)  @ PendSV clear-pending bit
    .equ    NVIC_IPR0,                      0xE000E400  @ NVIC Interrupt Priority Register base (one byte per IRQ)
@ MINI_OS_NVIC_PRIO_BITS comes from mini_config.h (per MINI_OS_ARCH)
    .equ    SYSTICK_CTRL,                   0xE000E010  @ SysTick Control and Status Register
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    @ single-precision VFP encoding: M4 default, and it overrides the M7
    @ double-precision default so the s16-s31 encodings stay valid
    .fpu  fpv4-sp-d16
#endif
    .text

@-----------------------------------------------------------------------------
@ interrupt masking primitives (exported, called from C via redef.h prototypes)
@ PRIMASK path by default; BASEPRI path when MINI_OS_IRQ_MAX_SYSCALL_PRIORITY
@ is defined in mini_config.h (M3/M4/M7 only, M0 has no BASEPRI hardware).
@ MINI_OS_IRQ_BASEPRI_THRESHOLD (register-position value) comes from
@ mini_config.h as well.
@-----------------------------------------------------------------------------
    .global mini_os_irq_disable
    .thumb_func
    .type mini_os_irq_disable, %function
mini_os_irq_disable:
#if MINI_OS_IRQ_USE_BASEPRI
    ldr     r0, =MINI_OS_IRQ_BASEPRI_THRESHOLD
    msr     basepri, r0             @ mask interrupts with priority >= threshold
#else
    cpsid   i                       @ mask all interrupts via PRIMASK
#endif
    bx      lr

    .global mini_os_irq_enable
    .thumb_func
    .type mini_os_irq_enable, %function
mini_os_irq_enable:
#if MINI_OS_IRQ_USE_BASEPRI
    movs    r0, #0                  @ BASEPRI = 0 unmasks everything (PRIMASK untouched)
    msr     basepri, r0
#else
    cpsie   i
#endif
    bx      lr

    .global mini_os_irq_save
    .thumb_func
    .type mini_os_irq_save, %function
mini_os_irq_save:
#if MINI_OS_IRQ_USE_BASEPRI
    mrs     r0, basepri             @ save previous BASEPRI (nestable)
    ldr     r1, =MINI_OS_IRQ_BASEPRI_THRESHOLD
    msr     basepri, r1
#else
    mrs     r0, primask             @ save previous PRIMASK (nestable)
    cpsid   i
#endif
    bx      lr                      @ saved state returned in r0

    .global mini_os_irq_restore
    .thumb_func
    .type mini_os_irq_restore, %function
mini_os_irq_restore:
#if MINI_OS_IRQ_USE_BASEPRI
    msr     basepri, r0             @ restore previous BASEPRI
#else
    msr     primask, r0             @ restore previous PRIMASK
#endif
    bx      lr

@-----------------------------------------------------------------------------
@ pendsv_handler : context switch exception handler
@-----------------------------------------------------------------------------
    .global pendsv_handler
    .thumb_func
    .type pendsv_handler, %function
pendsv_handler:
    cpsid   i                       @ disable interrupts
    mrs     r0, psp                 @ r0 = current task PSP

    ldr     r2, =mini_os_current_thread
    ldr     r1, [r2]                @ r1 = current TCB pointer

    cmp     r1, #0
    beq     .Lskip_save             @ current thread is NULL, skip saving

#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    @ ARMv6-M: STM/LDM register lists are limited to r0-r7, so r8-r11 are
    @ staged through low registers; only the 16-bit STM/LDM encodings exist
    subs    r0, r0, #32             @ make room for r4-r11
    stmia   r0!, {r4-r7}            @ save r4-r7
    mov     r4, r8
    mov     r5, r9
    mov     r6, r10
    mov     r7, r11
    stmia   r0!, {r4-r7}            @ save r8-r11 (staged in r4-r7)
    subs    r0, r0, #32             @ point r0 back at the saved block
#else
    stmdb   r0!, {r4-r11}           @ save callee-saved registers r4-r11
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    @ FPU : save s16-s31 below r4-r11 when the hardware
    @ frame holds FP state (EXC_RETURN bit4 = 0), plus a flag word on top
    tst     lr, #0x10               @ EXC_RETURN bit4: 0 = FP context stacked
    it      eq
    vstmdbeq r0!, {s16-s31}
    mov     r3, #0                  @ flag: 0 = no FP context saved
    tst     lr, #0x10
    it      eq
    moveq   r3, #1                  @ flag: 1 = s16-s31 are on the stack
    stmdb   r0!, {r3}
#endif
#endif
    str     r0, [r1]                @ TCB->sp = r0

.Lskip_save:
#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    @ ARMv6-M: POP cannot restore LR, so stage EXC_RETURN through r3 and keep
    @ r2 (symbol address) on the MSP 
    mov     r3, lr
    push    {r2, r3}
    bl      mini_os_schedule_switch
    pop     {r2, r3}
    mov     lr, r3
#elif MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    push    {r2, r3, lr}            @ keep r2, the FP flag and EXC_RETURN across the C call
    bl      mini_os_schedule_switch
    pop     {r2, r3, lr}
#else
    push    {r2, lr}                @ keep r2 (symbol address) and LR for the C call
    bl      mini_os_schedule_switch
    pop     {r2, lr}
#endif

    ldr     r1, [r2]                @ r1 = new TCB pointer
    ldr     r0, [r1]                @ r0 = new TCB->sp

#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    ldmia   r0!, {r4-r7}            @ restore r8-r11 first (saved last)
    mov     r8, r4
    mov     r9, r5
    mov     r10, r6
    mov     r11, r7
    ldmia   r0!, {r4-r7}            @ then restore r4-r7
#else
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    ldmia   r0!, {r3}               @ r3 = FP flag (saved last, restored first)
#endif
    ldmia   r0!, {r4-r11}           @ restore callee-saved registers
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    cmp     r3, #0
    it      ne
    vldmiane r0!, {s16-s31}         @ restore s16-s31 when the flag is set
#endif
#endif
    msr     psp, r0
    isb

#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    @ r3 still holds the FP flag (pushed around the schedule call above)
    orr     lr, lr, #0x10           @ default: exception return without FP frame
    cmp     r3, #0
    it      ne
    bicne   lr, lr, #0x10           @ flag set: declare FP context on return
#endif
    cpsie   i
    bx      lr                      @ exception return: hardware pops r0-r3, r12, lr, pc, xpsr

@-----------------------------------------------------------------------------
@ mini_os_start_first_thread : switch from MSP to PSP, enter the first thread
@-----------------------------------------------------------------------------
    .global mini_os_start_first_thread
    .thumb_func
    .type mini_os_start_first_thread, %function
mini_os_start_first_thread:
    ldr     r2, =mini_os_current_thread
    ldr     r0, [r2]                @ r0 = TCB pointer
    ldr     r0, [r0]                @ r0 = TCB->sp

#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    ldmia   r0!, {r4-r7}            @ restore r8-r11 first (saved last)
    mov     r8, r4
    mov     r9, r5
    mov     r10, r6
    mov     r11, r7
    ldmia   r0!, {r4-r7}            @ then restore r4-r7
#else
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    ldmia   r0!, {r3}               @ r3 = FP flag (always 0 in an initial frame)
#endif
    ldmia   r0!, {r4-r11}           @ restore callee-saved registers
#if MINI_OS_ARCH_HAS_FPU && MINI_OS_USE_FPU
    cmp     r3, #0
    it      ne
    vldmiane r0!, {s16-s31}         @ never taken for an initial frame
#endif
#endif
    msr     psp, r0
    isb

#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    @ ARMv6-M: no literal-load into high registers; build 0xFFFFFFFD via
    @ r0 = 4, r0 = 0 - 4 
    movs    r0, #4  
    rsbs    r0, r0, #0
#else
    ldr     lr, =MINI_OS_EXC_RETURN_THREAD_PSP @ EXC_RETURN: return to thread mode, use PSP
#endif
    cpsie   i
#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    bx      r0
#else
    bx      lr
#endif

@-----------------------------------------------------------------------------
@ mini_os_yield_trigger : trigger a context switch from C (set PendSV pending)
@-----------------------------------------------------------------------------
    .global mini_os_yield_trigger
    .thumb_func
    .type mini_os_yield_trigger, %function
mini_os_yield_trigger:
    ldr     r0, =NVIC_ICSR           @ Interrupt Control and State Register
    ldr     r1, =NVIC_PENDSVSET      @ PendSV set-pending bit
    str     r1, [r0]
    dsb
    isb
    movs    r0, #0
    bx      lr

    .global mini_os_cancel_pendsv
    .thumb_func
    .type mini_os_cancel_pendsv, %function
mini_os_cancel_pendsv:
    ldr     r0, =NVIC_ICSR           @ Interrupt Control and State Register
    ldr     r1, =NVIC_PENDSVCLR      @ PendSV clear-pending bit
    str     r1, [r0]
    dsb
    isb
    movs    r0, #0
    bx      lr

@-----------------------------------------------------------------------------
@ mini_os_barrier : data + instruction synchronization barrier (dsb + isb)
@-----------------------------------------------------------------------------
    .global mini_os_barrier
    .thumb_func
    .type mini_os_barrier, %function
mini_os_barrier:
    dsb
    isb
    bx      lr

@-----------------------------------------------------------------------------
@ mini_os_wfi : enter sleep, wait for the next interrupt
@-----------------------------------------------------------------------------
    .global mini_os_wfi
    .thumb_func
    .type mini_os_wfi, %function
mini_os_wfi:
    wfi
    bx      lr

@-----------------------------------------------------------------------------
@ mini_os_pause : spin-loop wait hint (yield, NOP-equivalent on single core)
@-----------------------------------------------------------------------------
    .global mini_os_pause
    .thumb_func
    .type mini_os_pause, %function
mini_os_pause:
    yield
    bx      lr

@-----------------------------------------------------------------------------
@ Set the priority of an NVIC interrupt
@ mini_os_nvic_set_priority(irq_type, priority) return MINI_OS_OK
@-----------------------------------------------------------------------------
    .global mini_os_nvic_set_priority
    .thumb_func
    .type mini_os_nvic_set_priority, %function
mini_os_nvic_set_priority:
    @ r0 = IRQ number, r1 = priority (0 = highest)
    ldr     r2, =NVIC_IPR0           @ NVIC interrupt priority register base
    add     r2, r0                   @ one priority byte per IRQ
    lsls    r1, r1, #(8 - MINI_OS_NVIC_PRIO_BITS) @ priority lives in the top bits of the byte (s suffix: 16-bit encoding under unified syntax)
    strb    r1, [r2]                 @ write the priority byte
    dsb
    isb
    movs    r0, #0                   @ return MINI_OS_OK
    bx      lr

    .global mini_os_psp_set
    .thumb_func
    .type mini_os_psp_set, %function

@-----------------------------------------------------------------------------
@ mini_os_psp_set : set the PSP to the given value  return void
@-----------------------------------------------------------------------------
    .global mini_os_psp_set
    .thumb_func
    .type mini_os_psp_set, %function
mini_os_psp_set:
    msr     psp, r0

@-----------------------------------------------------------------------------
@ mini_os_set_control : set the control register to the given value  return void
@-----------------------------------------------------------------------------
    .global mini_os_set_control
    .thumb_func
    .type mini_os_set_control, %function
mini_os_set_control:
    msr     control, r0
    isb

@-----------------------------------------------------------------------------
@ svc_handler : pass the exception frame, callback and arg to the C dispatcher
@-----------------------------------------------------------------------------
    .global svc_handler
    .thumb_func
    .type svc_handler, %function
svc_handler:
#if MINI_OS_ARCH == MINI_OS_ARCH_M0
    @ ARMv6-M: no TST/AND (immediate) encoding exists; shift EXC_RETURN bit2
    @ into the carry flag (LSLS leaves C = the shifted-out bit) and branch
    mov     r0, lr
    lsls    r0, r0, #29             @ C = bit2 (1 = PSP, 0 = MSP)
    bcc     .Luse_msp
#else
    tst     lr, #4                  @ EXC_RETURN bit2: 1 = PSP, 0 = MSP
    beq     .Luse_msp
#endif
    mrs     r0, psp                 @ r0 = exception frame
    b       .Lsvc_done
.Luse_msp:
    mrs     r0, msp
.Lsvc_done:
    ldr     r1, =g_svc_cb
    ldr     r1, [r1]                @ r1 = callback
    ldr     r2, =g_svc_arg
    ldr     r2, [r2]                @ r2 = arg
    bl      mini_os_svc_dispatch
    bx      lr                      @ exception return

@-----------------------------------------------------------------------------
@ Cortex-M7 L1 cache maintenance (M7 only, 32-byte lines).
@ Enable/disable sequences follow CMSIS SCB_EnableICache/SCB_EnableDCache,
@ the by-address ops follow RT-Thread and zephyr cpu_cache.c semantics.Thanks to these two open-source projects 
@-----------------------------------------------------------------------------
#if MINI_OS_ARCH == MINI_OS_ARCH_M7
    .equ    SCB_CCR,              0xE000ED14  @ Cache Control Register
    .equ    SCB_CCSIDR,           0xE000ED80  @ Cache Size ID Register (read)
    .equ    SCB_CSSELR,           0xE000ED84  @ Cache Size Selection Register
    .equ    SCB_ICIALLU,          0xE000EF50  @ I-cache invalidate all to PoU
    .equ    SCB_ICIMVAU,          0xE000EF58  @ I-cache invalidate by MVA to PoU
    .equ    SCB_DCIMVAC,          0xE000EF5C  @ D-cache invalidate by MVA to PoC
    .equ    SCB_DCISW,            0xE000EF60  @ D-cache invalidate by set/way
    .equ    SCB_DCCMVAC,          0xE000EF68  @ D-cache clean by MVA to PoC
    .equ    SCB_DCCIMVAC,         0xE000EF70  @ D-cache clean + invalidate by MVA to PoC
    .equ    SCB_DCCISW,           0xE000EF74  @ D-cache clean + invalidate by set/way
    .equ    MINI_OS_CACHE_LINE_BYTES, 32      @ M7 L1 cache line size
    @ error-code literals for mini_os_dcache_ops; keep in sync with err.h
    @ (ABI is fixed 32-bit, so the fallback values apply here; err.h itself
    @ cannot be included: redef.h typedefs are not assembler-safe)
    .equ    MINI_OS_ASM_OK,        0
    .equ    MINI_OS_ASM_ERR_INVAL, -1

    .global mini_os_icache_enable
    .thumb_func
    .type mini_os_icache_enable, %function
mini_os_icache_enable:
    dsb
    isb
    ldr     r0, =SCB_ICIALLU
    movs    r1, #0
    str     r1, [r0]                @ invalidate the whole I-cache
    dsb
    isb
    ldr     r0, =SCB_CCR
    ldr     r1, [r0]
    movs    r2, #1
    lsl     r2, r2, #16             @ CCR.IC
    orr     r1, r1, r2
    str     r1, [r0]
    dsb
    isb
    bx      lr

    .global mini_os_icache_disable
    .thumb_func
    .type mini_os_icache_disable, %function
mini_os_icache_disable:
    ldr     r0, =SCB_CCR
    ldr     r1, [r0]
    movs    r2, #1
    lsl     r2, r2, #16             @ CCR.IC
    bics    r1, r1, r2
    str     r1, [r0]
    dsb
    isb
    bx      lr

    .global mini_os_icache_invalidate_by_addr
    .thumb_func
    .type mini_os_icache_invalidate_by_addr, %function
mini_os_icache_invalidate_by_addr:
    @ r0 = addr, r1 = size; the range is rounded out to cache lines
    bic     r2, r0, #(MINI_OS_CACHE_LINE_BYTES - 1)   @ line-aligned start
    subs    r3, r0, r2              @ offset inside the first line
    adds    r1, r1, r3              @ r1 = remaining bytes
    dsb
    ldr     r3, =SCB_ICIMVAU
.Licache_line:
    cmp     r1, #0
    ble     .Licache_done
    str     r2, [r3]                @ invalidate by MVA to PoU
    add     r2, r2, #MINI_OS_CACHE_LINE_BYTES
    sub     r1, r1, #MINI_OS_CACHE_LINE_BYTES
    b       .Licache_line
.Licache_done:
    dsb
    isb
    bx      lr

@ internal: maintain every set/way of the level-1 D-cache; r3 = DCISW/DCCISW
.Ldcache_maint_all:
    ldr     r0, =SCB_CSSELR
    movs    r1, #0
    str     r1, [r0]                @ select level-1 data cache
    dsb
    ldr     r0, =SCB_CCSIDR
    ldr     r0, [r0]                @ r0 = CCSIDR
    ubfx    r1, r0, #13, #15        @ r1 = sets (count - 1)
    ubfx    r2, r0, #3, #10         @ r2 = ways (count - 1)
.Lmaint_sets:
    mov     r4, r2
.Lmaint_ways:
    lsl     r5, r1, #5              @ set field (bits[18:5])
    lsl     r6, r4, #30             @ way field (bits[31:30])
    orr     r5, r5, r6
    str     r5, [r3]
    subs    r4, r4, #1
    bpl     .Lmaint_ways
    subs    r1, r1, #1
    bpl     .Lmaint_sets
    dsb
    bx      lr

    .global mini_os_dcache_enable
    .thumb_func
    .type mini_os_dcache_enable, %function
mini_os_dcache_enable:
    dsb
    isb
    ldr     r3, =SCB_DCISW
    bl      .Ldcache_maint_all      @ no dirty line may survive enabling
    ldr     r0, =SCB_CCR
    ldr     r1, [r0]
    movs    r2, #1
    lsl     r2, r2, #17             @ CCR.DC
    orr     r1, r1, r2
    str     r1, [r0]
    dsb
    isb
    bx      lr

    .global mini_os_dcache_disable
    .thumb_func
    .type mini_os_dcache_disable, %function
mini_os_dcache_disable:
    ldr     r0, =SCB_CCR
    ldr     r1, [r0]
    movs    r2, #1
    lsl     r2, r2, #17             @ CCR.DC
    bics    r1, r1, r2
    str     r1, [r0]
    dsb
    ldr     r3, =SCB_DCCISW
    bl      .Ldcache_maint_all      @ write back every dirty line
    isb
    bx      lr

    .global mini_os_dcache_ops
    .thumb_func
    .type mini_os_dcache_ops, %function
mini_os_dcache_ops:
    @ r0 = addr, r1 = size, r2 = ops (MINI_OS_CACHE_FLUSH/INVALIDATE bitmask)
    @ returns MINI_OS_OK, or MINI_OS_ERR_INVAL when no op is selected
    and     r3, r2, #(MINI_OS_CACHE_FLUSH | MINI_OS_CACHE_INVALIDATE)
    cmp     r3, #(MINI_OS_CACHE_FLUSH | MINI_OS_CACHE_INVALIDATE)
    bne     .Ldcache_ops_single
    ldr     r3, =SCB_DCCIMVAC       @ clean + invalidate by MVA
    b       .Ldcache_ops_run
.Ldcache_ops_single:
    tst     r2, #MINI_OS_CACHE_FLUSH
    bne     .Ldcache_ops_flush
    tst     r2, #MINI_OS_CACHE_INVALIDATE
    bne     .Ldcache_ops_inv
    ldr     r0, =MINI_OS_ASM_ERR_INVAL @ no op selected: report the error
    bx      lr
.Ldcache_ops_flush:
    ldr     r3, =SCB_DCCMVAC        @ clean by MVA
    b       .Ldcache_ops_run
.Ldcache_ops_inv:
    ldr     r3, =SCB_DCIMVAC        @ invalidate by MVA (see note in port.h)
.Ldcache_ops_run:
    bic     r2, r0, #(MINI_OS_CACHE_LINE_BYTES - 1)   @ line-aligned start
    subs    r0, r0, r2              @ offset inside the first line
    adds    r1, r1, r0              @ r1 = remaining bytes
    dsb
.Ldcache_ops_line:
    cmp     r1, #0
    ble     .Ldcache_ops_done
    str     r2, [r3]                @ maintenance by MVA to PoC
    add     r2, r2, #MINI_OS_CACHE_LINE_BYTES
    sub     r1, r1, #MINI_OS_CACHE_LINE_BYTES
    b       .Ldcache_ops_line
.Ldcache_ops_done:
    dsb
    isb
    movs    r0, #0                  @ MINI_OS_ASM_OK
    bx      lr
#endif /* MINI_OS_ARCH == MINI_OS_ARCH_M7 */
