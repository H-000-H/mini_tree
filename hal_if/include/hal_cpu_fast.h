#ifndef HAL_CPU_FAST_H
#define HAL_CPU_FAST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── ISR 安全红线 ──
 *
 * 中断服务函数中禁止调用任何可能持有互斥锁的 VFS/OSAL 函数:
 *   device_write / device_read / device_ioctl / device_open / device_close
 *   osal_mutex_take / osal_mutex_give
 *
 * 中断上下文无法等待锁, 调用即死锁 (HardFault 或优先级反转)。
 * 高频中断 (<10μs 周期) 应直接操作硬件寄存器, 绕过框架抽象层。
 *
 * 参考 "红线区 / 蓝线区" 架构原则:
 *   红线区 (硬实时 5%) — 允许暴力开后门, 直接写寄存器
 *   蓝线区 (非时间关键 95%) — 强制使用 VFS/OSAL
 */

/* ── 中断上下文检测 ──
 *
 * 返回非零表示当前在异常处理上下文中 (ISR / HardFault / NMI)。
 * 平台实现:
 *   ARM Cortex-M: MRS IPSR (Interrupt Program Status Register)
 *   RISC-V:       CSR MCAUSE (Machine Cause Register)
 *   其他:         返回 0 (保守假设不在中断)
 */
static inline int hal_is_in_isr(void)
{
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)
    int ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr;
#elif defined(__riscv)
    int mcause;
    __asm__ volatile("csrr %0, mcause" : "=r"(mcause));
    return mcause;
#else
    (void)0;
    return 0;
#endif
}

/* ── ISR 安全断言 (DEBUG 时检测非法 VFS 调用) ──
 *
 * 在任何 VFS 入口处插入 HAL_ASSERT_NOT_ISR():
 *   int device_write(device_t* dev, ...) {
 *       HAL_ASSERT_NOT_ISR();
 *       ...
 *   }
 *
 * Release 模式编译为空, 零开销。
 */
#ifndef DEBUG
#define HAL_ASSERT_NOT_ISR()  ((void)0)
#else
#include <stdio.h>
#define HAL_ASSERT_NOT_ISR()                                             \
    do {                                                                 \
        if (hal_is_in_isr()) {                                           \
            printf("[FATAL] VFS call from ISR context! "                 \
                   "Remove VFS calls from ISR or bypass VFS.\n");        \
            while (1) { /* trap */ }                                      \
        }                                                                \
    } while (0)
#endif

/* ── NVIC 寄存器直写 (ARM Cortex-M) ──
 *
 * 不依赖 CMSIS, 直接操作内存映射寄存器。
 *   ISER[irq_num/32] 置位 → 使能
 *   ICER[irq_num/32] 置位 → 禁能
 *   IPR[irq_num]     写入 → 优先级
 *
 * irq_num: 外部中断号。Cortex-M 前 16 号为系统异常,
 *           外部中断从 16 开始, 但 ISER/ICER 以 0 为基址。
 *           例如 TIM3 的 IRQn=29, hal_irq_enable(29) 即可。
 */
#define HAL_NVIC_ISER_BASE   0xE000E100UL
#define HAL_NVIC_ICER_BASE   0xE000E180UL
#define HAL_NVIC_IPR_BASE    0xE000E400UL

static inline void hal_irq_enable(int irq_num)
{
    uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
    uint32_t bit = 1UL << (irq_num & 0x1F);
    *(volatile uint32_t*)(HAL_NVIC_ISER_BASE + reg) = bit;
}

static inline void hal_irq_disable(int irq_num)
{
    uint32_t reg = (uint32_t)(irq_num >> 5) << 2;
    uint32_t bit = 1UL << (irq_num & 0x1F);
    *(volatile uint32_t*)(HAL_NVIC_ICER_BASE + reg) = bit;
}

static inline void hal_irq_set_priority(int irq_num, int priority)
{
    *(volatile uint8_t*)(HAL_NVIC_IPR_BASE + (uint32_t)irq_num) = (uint8_t)(priority & 0xFF);
}

static inline int hal_irq_get_priority(int irq_num)
{
    return *(volatile uint8_t*)(HAL_NVIC_IPR_BASE + (uint32_t)irq_num);
}

/* ── 全局中断开关 (CPU 层面) ──
 *
 * 仅用于临界段, 不应长期关闭中断。
 * hal_irq_disable_all() 返回当前 PRIMASK 状态, 供 hal_irq_restore() 恢复。
 */
#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_6M__) || defined(__ARM_ARCH_8M_BASE__) || \
    defined(__ARM_ARCH_8M_MAIN__)

static inline uint32_t hal_irq_disable_all(void)
{
    uint32_t mask;
    __asm__ volatile("mrs %0, primask\n\t"
                     "cpsid i"
                     : "=r"(mask));
    return mask;
}

static inline void hal_irq_restore(uint32_t mask)
{
    __asm__ volatile("msr primask, %0" : : "r"(mask));
}

#else

static inline uint32_t hal_irq_disable_all(void) { uint32_t m; __asm__ volatile("" : "=r"(m)); return m; }
static inline void hal_irq_restore(uint32_t mask) { (void)mask; }

#endif

#ifdef __cplusplus
}
#endif

#endif /* HAL_CPU_FAST_H */
