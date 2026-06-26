#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

#include <stddef.h>
#include <stdint.h>

/* 统一类型获取宏 */
#ifdef __cplusplus
#define TYPEOF(expr) decltype(expr)
#else
#define TYPEOF(expr) typeof(expr)
#endif

/* ── 编译器兼容抽象层 ──
 *
 * 统一 GCC / Clang 的 __attribute__ 与内置函数差异。
 * Kconfig 选项见 Compiler Compatibility 菜单 (tools/genconfig.py)。
 */

                                                            /*Kconfig 配置回退*/
/*===========================================================================================================================================================*/
#ifndef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 0
#if defined(__has_include)
#if __has_include("config.h")
#include "config.h"
#undef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 1
#endif
#endif
#endif
/*===========================================================================================================================================================*/

                                                            /*功能开关宏*/
/*===========================================================================================================================================================*/
#define COMPAT_CFG_ENABLED(sym) \
    ((!COMPAT_HAVE_KCONFIG) || defined(CONFIG_##sym))

/* COMPAT_GNU_EXT_OK / COMPAT_WUR_ATTR_OK 通过嵌套 #if 计算为字面量 0/1,
 * 避免 defined 出现在宏展开中触发 -Wexpansion-to-defined 警告。
 * (COMPAT_CFG_ENABLED 仍保留给少量 C 源文件在 #if 中直接使用) */
#if (defined(__GNUC__) || defined(__clang__))
#  if COMPAT_HAVE_KCONFIG
#    ifdef CONFIG_COMPILER_GNU_EXTENSIONS
#      define COMPAT_GNU_EXT_OK 1
#    else
#      define COMPAT_GNU_EXT_OK 0
#    endif
#  else
#    define COMPAT_GNU_EXT_OK 1
#  endif
#else
#  define COMPAT_GNU_EXT_OK 0
#endif

#if COMPAT_GNU_EXT_OK
#  if COMPAT_HAVE_KCONFIG
#    ifdef CONFIG_COMPILER_WARN_UNUSED_RESULT
#      define COMPAT_WUR_ATTR_OK 1
#    else
#      define COMPAT_WUR_ATTR_OK 0
#    endif
#  else
#    define COMPAT_WUR_ATTR_OK 1
#  endif
#else
#  define COMPAT_WUR_ATTR_OK 0
#endif
/*===========================================================================================================================================================*/

                                                            /*属性与内置宏*/
/*===========================================================================================================================================================*/
#define COMPAT_ALIGNED(n) __attribute__((aligned(n)))
/* 静态内存池、DMA buffer、OSAL backing storage 统一使用 COMPAT_ALIGNED(4)，见 static-pool-alignment.mdc */
#define COMPAT_WEAK __attribute__((weak))
#define COMPAT_TRAP()     __builtin_trap()
#define COMPAT_CTZ(x)     __builtin_ctz(x)
#define COMPAT_PACKED     __attribute__((packed))
/*===========================================================================================================================================================*/

                                                            /*函数内联控制*/
/*===========================================================================================================================================================*/
/* 强制内联: 用于高频热路径 (ISR/fast path), 绕过 -O0 调试限制 */
#define COMPAT_ALWAYS_INLINE __attribute__((always_inline)) inline
/* 禁止内联: 防止调试函数或冷路径膨胀代码段 */
#define COMPAT_NOINLINE      __attribute__((noinline))
/* 展开调用链所有内联: 用于性能关键入口函数 */
#define COMPAT_FLATTEN       __attribute__((flatten))
/*===========================================================================================================================================================*/

                                                            /*函数副作用标注*/
/*===========================================================================================================================================================*/
/* pure: 仅读全局变量/指针参数, 无写入 → 编译器可 CSE 消除冗余调用 */
#define COMPAT_PURE       __attribute__((pure))
/* const: 不读不写任何全局状态, 仅依赖参数 → 可被 CSE + 循环不变量外提 */
#define COMPAT_CONST_FUNC __attribute__((const))
/* noreturn: 标记 panic/assert 终止路径, 消除调用者 fallthrough 假设 */
#define COMPAT_NORETURN   __attribute__((noreturn))
/* hot: 热路径函数, 编译器优先内联并放置在代码段前部 (icache 友好) */
#define COMPAT_HOT        __attribute__((hot))
/* cold: 冷路径 (错误处理), 编译器减小内联并放置在代码段尾部 */
#define COMPAT_COLD       __attribute__((cold))
/*===========================================================================================================================================================*/

                                                            /*符号可见性保留*/
/*===========================================================================================================================================================*/
/* used: 防止 LTO/GC 删除看似未引用的静态符号 (如 linker table entry) */
#define COMPAT_USED   __attribute__((used))
/* unused: 抑制未使用参数/变量的 -Wunused 警告 */
#define COMPAT_UNUSED __attribute__((unused))
/* may_alias: 允许通过不同类型指针访问同一内存 (type-punning 安全) */
#define COMPAT_MAY_ALIAS __attribute__((may_alias))
/*===========================================================================================================================================================*/

                                                            /*编译期诊断与控制流*/
/*===========================================================================================================================================================*/
/* fallthrough: 显式标注 switch case 贯穿 (C++17 [[fallthrough]] 等价, 消除 -Wimplicit-fallthrough) */
#if defined(__cplusplus) && __cplusplus >= 201703L
#define COMPAT_FALLTHROUGH [[fallthrough]]
#elif COMPAT_GNU_EXT_OK
#define COMPAT_FALLTHROUGH __attribute__((fallthrough))
#else
#define COMPAT_FALLTHROUGH ((void)0)
#endif

/* deprecated: 标记废弃 API, 编译期警告 + 消息提示 */
#if COMPAT_GNU_EXT_OK
#define COMPAT_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define COMPAT_DEPRECATED(msg)
#endif

/* static_assert: C11 _Static_assert 兼容, 编译期布局校验 */
#if defined(__cplusplus) && __cplusplus >= 201103L
#define COMPAT_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define COMPAT_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define COMPAT_STATIC_ASSERT(cond, msg) typedef char COMPAT_SA_##__LINE__[(cond) ? 1 : -1]
#endif

/* compile_error: 在特定配置路径触发编译期错误 (替代 #error, 可嵌套宏) */
#if COMPAT_GNU_EXT_OK
#define COMPAT_COMPILE_ERROR(msg) __attribute__((error(msg)))
#endif
/*===========================================================================================================================================================*/

                                                            /*位操作内置函数*/
/*===========================================================================================================================================================*/
#define COMPAT_CLZ(x)      __builtin_clz(x)      /* 前导零计数 (需 x≠0) */
#define COMPAT_POPCOUNT(x) __builtin_popcount(x) /* 置位计数 */
#define COMPAT_FFS(x)      __builtin_ffs(x)      /* 最低置位位号 (1-based, 0=全零) */
/*===========================================================================================================================================================*/

                                                            /*预取与对齐假设*/
/*===========================================================================================================================================================*/
/* prefetch: 预取数据到 L1 cache, rw=0 读预取, locality=3 高时间局部性 */
#define COMPAT_PREFETCH(addr, rw, locality) __builtin_prefetch((addr), (rw), (locality))
/* assume_aligned: 告知编译器指针 N 字节对齐, 生成向量化 load/store */
#if COMPAT_GNU_EXT_OK
#define COMPAT_ASSUME_ALIGNED(ptr, n) __builtin_assume_aligned((ptr), (n))
#else
#define COMPAT_ASSUME_ALIGNED(ptr, n) (ptr)
#endif
/* constant_p: 编译期判断表达式是否为常量 (用于优化分支选择) */
#define COMPAT_CONSTANT_P(expr) __builtin_constant_p(expr)
/*===========================================================================================================================================================*/

                                                            /*VFS Magic 命名空间*/
/*===========================================================================================================================================================*/
/* VFS ioctl 总线 namespace: 每条总线占 0x100, 新增总线只在表里加一行 */
#define COMPAT_MAGIC_SLOT_STRIDE 0x100u
#define COMPAT_MAGIC_TABLE(X) \
    X(SPI,   0x00) \
    X(UART,  0x01) \
    X(I2C,   0x02) \
    X(I2S,   0x03) \
    X(USB,   0x04) \
    X(CAN,   0x05) \
    X(ETH,   0x06) \
    X(GPIO,  0x07) \
    X(SDIO,  0x08) \
    X(W25Q64,0X09) 
#define COMPAT_MAGIC_ENUM(name, slot) \
    COMPAT_MAGIC_##name = (uint32_t)((slot) * COMPAT_MAGIC_SLOT_STRIDE),

enum 
{
    COMPAT_MAGIC_TABLE(COMPAT_MAGIC_ENUM)
};

#undef COMPAT_MAGIC_ENUM

#define COMPAT_MAGIC(x) COMPAT_MAGIC_##x
/*===========================================================================================================================================================*/

                                                            /*warn_unused_result / nodiscard*/
/*===========================================================================================================================================================*/
#if COMPAT_WUR_ATTR_OK
#define COMPAT_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define COMPAT_WARN_UNUSED_RESULT
#endif

#ifdef __cplusplus
#if COMPAT_WUR_ATTR_OK
#define COMPAT_NODISCARD [[nodiscard]]
#else
#define COMPAT_NODISCARD
#endif
#else
#define COMPAT_NODISCARD COMPAT_WARN_UNUSED_RESULT
#endif
/*===========================================================================================================================================================*/

                                                            /*返回值显式丢弃*/
/*===========================================================================================================================================================*/
/* 显式丢弃 warn_unused_result 标注函数的返回值 (GCC 14+ 下 (void)expr 无效) */
#if COMPAT_WUR_ATTR_OK
#define COMPAT_IGNORE_RESULT(expr) \
    do { \
        TYPEOF(expr) _compat_ign_ __attribute__((unused)) = (expr); \
    } while (0)
#else
#define COMPAT_IGNORE_RESULT(expr) ((void)(expr))
#endif
/*===========================================================================================================================================================*/

                                                            /*printf format 属性*/
/*===========================================================================================================================================================*/
/* format 属性用 __printf__，避免 poison printf 后属性里的 printf 标识符报错 */
#if defined(__GNUC__)
#define COMPAT_FMT_PRINTF(fmt_arg, first_var) \
    __attribute__((format(__printf__, (fmt_arg), (first_var))))
#else
#define COMPAT_FMT_PRINTF(fmt_arg, first_var)
#endif
/*===========================================================================================================================================================*/

                                                            /*container_of*/
/*===========================================================================================================================================================*/
/* Linux 风格 container_of */
#if COMPAT_GNU_EXT_OK
#undef container_of
#define container_of(ptr, type, member) ({                         \
    const TYPEOF(((type *)0)->member) *__mptr = (ptr);             \
    (type *)((char *)__mptr - __builtin_offsetof(type, member));             \
})
#else
#ifndef container_of
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
#endif
/*===========================================================================================================================================================*/

                                                            /*likely / unlikely / unreachable*/
/*===========================================================================================================================================================*/
#if COMPAT_GNU_EXT_OK
#undef unlikely
#undef likely
#undef unreachable
#define unlikely(x) __builtin_expect(!!(x),0)
#define likely(x)   __builtin_expect(!!(x),1)
#define unreachable()  __builtin_unreachable()
/* 高级用法: 静态池在 main 之前自动执行 — pre_execution(150) → constructor(250) */
#define pre_execution(x) __attribute__((constructor(x+100)))
#ifdef AUTO_FREE_PTR
#include <stdlib.h>
static inline void auto_free_ptr(void *ptr)
{
    void **real_ptr = (void **)ptr;
    if (*real_ptr != NULL)
    {
        free(*real_ptr);
        *real_ptr = NULL;
    }
}
#define AUTO_FREE __attribute__((cleanup(auto_free_ptr)))
#endif
#endif
/*===========================================================================================================================================================*/

                                                            /*RAM 执行段*/
/*===========================================================================================================================================================*/
/* ── RAM_EXEC: 将函数置于 RAM 执行 ──
 *
 * 将高频中断或控制环函数放入 .ram_code 段, 在启动时由用户 linker script
 * 搬运到 TCM 或 SRAM, 消除 Flash Cache Miss 导致的延迟抖动。
 *
 * 用法:
 *   RAM_EXEC void motor_foc_isr(void) { ... }
 *
 * 确认芯片有足够的 RAM/ITCM, 并在 .ld 中添加:
 *
 *   .ram_code : {
 *       *(.ram_code*)
 *   } > ITCM AT> FLASH
 *
 *   _sram_code = ADDR(.ram_code);
 *   _eram_code = ADDR(.ram_code) + SIZEOF(.ram_code);
 *   _ram_code_flash = LOADADDR(.ram_code);
 *
 *   startup 中: memcpy(&_sram_code, &_ram_code_flash, _eram_code - _sram_code);
 */
#define RAM_EXEC  __attribute__((section(".ram_code")))
/*===========================================================================================================================================================*/

                                                            /*伪随机数生成器*/
/*===========================================================================================================================================================*/
static uint32_t xorshift_state = 2463532242UL;
#include <stdlib.h>

static inline uint32_t COMPAT_RAND(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    a ^= xorshift_state;

    // ChaCha20
    a += b; d ^= a; d = (d << 16) | (d >> 16);
    c += d; b ^= c; b = (b << 12) | (b >> 20);
    a += b; d ^= a; d = (d << 8)  | (d >> 24);
    c += d; b ^= c; b = (b << 7)  | (b >> 25);

    // ChaCha20 交替给 Xorshift
    uint32_t x = a ^ b ^ c ^ d;

    // Xorshift 核心变换
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    //  Xorshift 的结果再与 ChaCha20 的非线性项异或后输
    xorshift_state = x ^ (c + d);

    return xorshift_state;
}
/*===========================================================================================================================================================*/

                                                            /*字节序枚举*/
/*===========================================================================================================================================================*/
typedef enum 
{
    MSB = 0,
    LSB ,
}Endianness;
/*===========================================================================================================================================================*/

#endif /* COMPILER_COMPAT_H */
