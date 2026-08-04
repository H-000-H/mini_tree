/* SPDX-License-Identifier: Apache-2.0 */
/*
 * compiler_compat — 编译器兼容性抽象层
 *
 * 统一 GCC/Clang 的 __attribute__ 与内置函数差异, 功能受 Kconfig 开关控制
 * 提供 warn_unused_result、format、container_of、likely/unlikely、RAM_EXEC 等通用宏
 */
#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

#include "compiler_inline.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ── TYPEOF ─────────────────────────────────────────────────────────────── */

/**
 * @brief 获取表达式的类型
 * @param expr 表达式
 */
#ifdef __cplusplus
#define TYPEOF(expr) decltype(expr)
#else
#define TYPEOF(expr) typeof(expr)
#endif

/* ── Kconfig 配置回退 ───────────────────────────────────────────────────── */

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

/* ── 功能开关 ───────────────────────────────────────────────────────────── */

/**
 * @brief 功能开关宏
 * @param sym 功能开关名称 (不含 CONFIG_ 前缀)
 */
#define COMPAT_CFG_ENABLED(sym) ((!COMPAT_HAVE_KCONFIG) || defined(CONFIG_##sym))

/**
 * @brief GNU 扩展是否可用
 * @details 通过嵌套 #if 计算为字面量 0/1; 受 CONFIG_COMPILER_GNU_EXTENSIONS 控制
 */
#if (defined(__GNUC__) || defined(__clang__))
#if COMPAT_HAVE_KCONFIG
#ifdef CONFIG_COMPILER_GNU_EXTENSIONS
#define COMPAT_GNU_EXT_OK 1
#else
#define COMPAT_GNU_EXT_OK 0
#endif
#else
#define COMPAT_GNU_EXT_OK 1
#endif
#else
#define COMPAT_GNU_EXT_OK 0
#endif

/**
 * @brief warn_unused_result 属性是否可用
 * @details 通过嵌套 #if 计算为字面量 0/1; 受 CONFIG_COMPILER_WARN_UNUSED_RESULT 控制
 */
#if COMPAT_GNU_EXT_OK
#if COMPAT_HAVE_KCONFIG
#ifdef CONFIG_COMPILER_WARN_UNUSED_RESULT
#define COMPAT_WUR_ATTR_OK 1
#else
#define COMPAT_WUR_ATTR_OK 0
#endif
#else
#define COMPAT_WUR_ATTR_OK 1
#endif
#else
#define COMPAT_WUR_ATTR_OK 0
#endif

/* ── 基础属性 ───────────────────────────────────────────────────────────── */

/** @brief 对齐属性 @param n 对齐字节数 */
#define COMPAT_ALIGNED(n) __attribute__((aligned(n)))
/** @brief 弱符号属性 */
#define COMPAT_WEAK __attribute__((weak))
/** @brief 不返回函数属性 */
#define COMPAT_NORETURN __attribute__((noreturn))
/** @brief 打包属性 */
#define COMPAT_PACKED __attribute__((packed))
/** @brief 强制内联属性 */
#define COMPAT_ALWAYS_INLINE __attribute__((always_inline)) inline
/** @brief 禁止内联属性 */
#define COMPAT_NOINLINE __attribute__((noinline))
/** @brief 展开调用链所有内联属性 */
#define COMPAT_FLATTEN __attribute__((flatten))
/** @brief 纯函数属性 */
#define COMPAT_PURE __attribute__((pure))
/** @brief 常量函数属性 */
#define COMPAT_CONST_FUNC __attribute__((const))
/** @brief 热路径函数属性 */
#define COMPAT_HOT __attribute__((hot))
/** @brief 冷路径函数属性 */
#define COMPAT_COLD __attribute__((cold))
/** @brief 使用属性 (防止链接器剔除) */
#define COMPAT_USED __attribute__((used))
/** @brief 未使用属性 (抑制警告) */
#define COMPAT_UNUSED __attribute__((unused))
/** @brief 可能别名属性 */
#define COMPAT_MAY_ALIAS __attribute__((may_alias))

/* ── 扩展属性 ───────────────────────────────────────────────────────────── */

/**
 * @brief 贯穿属性 (switch fallthrough)
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#define COMPAT_FALLTHROUGH [[fallthrough]]
#elif COMPAT_GNU_EXT_OK
#define COMPAT_FALLTHROUGH __attribute__((fallthrough))
#else
#define COMPAT_FALLTHROUGH ((void)0)
#endif

/**
 * @brief 静态断言
 * @param cond 断言条件
 * @param msg  断言消息
 */
#if defined(__cplusplus) && __cplusplus >= 201103L
#define COMPAT_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define COMPAT_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define COMPAT_STATIC_ASSERT(cond, msg) typedef char COMPAT_SA_##__LINE__[(cond) ? 1 : -1]
#endif

/**
 * @brief 编译期错误属性 (触发时直接编译失败)
 * @param msg 编译期错误消息
 */
#if COMPAT_GNU_EXT_OK
#define COMPAT_COMPILE_ERROR(msg) __attribute__((error(msg)))
#else
#define COMPAT_COMPILE_ERROR(msg)
#endif

/* ── 编译器内建函数 ─────────────────────────────────────────────────────── */

#include "status.h"

/**
 * @brief 触发 CPU 陷阱, 用于不可恢复错误
 */
COMPAT_STATIC_INLINE COMPAT_NORETURN void COMPAT_TRAP(void) { __builtin_trap(); }

/**
 * @brief 计算 x 的末尾零位 (count trailing zeros)
 * @param x 32 位无符号整数
 * @return 末尾零位个数; x 为 0 时返回 32
 */
COMPAT_STATIC_INLINE uint32_t COMPAT_CTZ(uint32_t x)
{
    if (x == 0U)
        return 32U;
    return (uint32_t)__builtin_ctz(x);
}

/**
 * @brief 计算 x 的前导零位 (count leading zeros)
 * @param x 32 位无符号整数
 * @return 前导零位个数; x 为 0 时返回 32
 */
COMPAT_STATIC_INLINE COMPAT_CONST_FUNC uint32_t COMPAT_CLZ(uint32_t x)
{
    if (x == 0U)
        return 32U;
    return (uint32_t)__builtin_clz(x);
}

/**
 * @brief 计算 x 的置位个数 (population count)
 * @param x 32 位无符号整数
 * @return 置位个数
 */
COMPAT_STATIC_INLINE COMPAT_CONST_FUNC uint32_t COMPAT_POPCOUNT(uint32_t x)
{
    return (uint32_t)__builtin_popcount(x);
}

/**
 * @brief 返回最低置位的位号 (find first set)
 * @param x 32 位无符号整数
 * @return 最低置位位号 (1-based); x 为 0 时返回 0
 */
COMPAT_STATIC_INLINE COMPAT_CONST_FUNC uint32_t COMPAT_FFS(uint32_t x)
{
    return (uint32_t)__builtin_ffs(x);
}

/**
 * @brief 预取内存到缓存
 * @param addr     目标地址
 * @param rw       0=读, 1=写
 * @param locality 时间局部性 0~3
 */
#define COMPAT_PREFETCH(addr, rw, locality) __builtin_prefetch((addr), (rw), (locality))

/**
 * @brief 假设指针对齐
 * @param ptr 指针
 * @param n   对齐字节数
 */
#if COMPAT_GNU_EXT_OK
#define COMPAT_ASSUME_ALIGNED(ptr, n) __builtin_assume_aligned((ptr), (n))
#else
#define COMPAT_ASSUME_ALIGNED(ptr, n) (ptr)
#endif

/**
 * @brief 编译期判断表达式是否为常量 (用于优化分支选择)
 * @param expr 待判断表达式
 */
#define COMPAT_CONSTANT_P(expr) __builtin_constant_p(expr)

/* ── 设备魔法槽枚举 ─────────────────────────────────────────────────────── */

/** @brief 魔法槽步长 (各设备类型基址间隔) */
#define COMPAT_MAGIC_SLOT_STRIDE 0x100u

/** @brief 设备魔法槽 X-Macro 表 */
#define COMPAT_MAGIC_TABLE(X)                                                                      \
    X(SPI, 0x00)                                                                                   \
    X(UART, 0x01)                                                                                  \
    X(I2C, 0x02)                                                                                   \
    X(I2S, 0x03)                                                                                   \
    X(USB, 0x04)                                                                                   \
    X(CAN, 0x05)                                                                                   \
    X(ETH, 0x06)                                                                                   \
    X(GPIO, 0x07)                                                                                  \
    X(SDIO, 0x08)                                                                                  \
    X(W25Q64, 0x09)                                                                                \
    X(TIM, 0x0A)                                                                                   \
    X(ADC, 0x0B)                                                                                   \
    X(DAC, 0x0C)                                                                                   \
    X(RTC, 0x0D)                                                                                   \
    X(IWDG, 0x0E)                                                                                  \
    X(WWDG, 0x0F)                                                                                  \
    X(WS2812, 0x10)                                                                                \
    X(ST7789, 0x11)                                                                                \
    X(MAX98357A, 0x12)                                                                             \
    X(GPIOKEY, 0x13)                                                                               \
    X(LIGHT, 0x14)                                                                                 \
    X(PWMBL, 0x15)                                                                                 \
    X(SHT30, 0x16)                                                                                 \
    X(MPU6050, 0x17)                                                                               \
    X(AT24C02, 0x18)                                                                               \
    X(SSD1306, 0x19)                                                                               \
    X(BH1750, 0x1A)                                                                                \
    X(BME280, 0x1B)                                                                                \
    X(FT5X06, 0x1C)                                                                                \
    X(XPT2046, 0x1D)                                                                               \
    X(SX1278, 0x1E)                                                                                \
    X(EPAPER, 0x1F)                                                                                \
    X(NEO_M8N, 0x20)                                                                               \
    X(AIR780E, 0x21)                                                                               \
    X(HC05, 0x22)                                                                                  \
    X(RS485_MODBUS, 0x23)                                                                          \
    X(DRV8833, 0x24)                                                                               \
    X(SG90, 0x25)                                                                                  \
    X(BUZZER, 0x26)                                                                                \
    X(RELAY, 0x27)                                                                                 \
    X(DS18B20, 0x28)                                                                               \
    X(SN65HVD230, 0x29)                                                                            \
    X(W25QXX, 0x2A)                                                                                \
    X(AHT20, 0x2B)                                                                                 \
    X(SHT40, 0x2C)                                                                                 \
    X(BMP280, 0x2D)                                                                                \
    X(VL53L0X, 0x2E)                                                                               \
    X(PCF8574, 0x2F)                                                                               \
    X(ADS1115, 0x30)                                                                               \
    X(INA219, 0x31)                                                                                \
    X(SH1106, 0x32)                                                                                \
    X(NRF24L01, 0x33)                                                                              \
    X(RC522, 0x34)                                                                                 \
    X(MAX7219, 0x35)                                                                               \
    X(A7670, 0x36)                                                                                 \
    X(PN532, 0x37)                                                                                 \
    X(DFPLAYER, 0x38)

/**
 * @brief 魔法槽枚举生成器
 * @param name 设备名称
 * @param slot 槽序号
 */
#define COMPAT_MAGIC_ENUM(name, slot)                                                              \
    COMPAT_MAGIC_##name = (uint32_t)((slot) * COMPAT_MAGIC_SLOT_STRIDE),

/** @brief 设备魔法槽枚举 */
enum
{
    COMPAT_MAGIC_TABLE(COMPAT_MAGIC_ENUM)
};

#undef COMPAT_MAGIC_ENUM

/**
 * @brief 获取设备魔法槽常量
 * @param x 设备名称 (SPI, UART, ...)
 */
#define COMPAT_MAGIC(x) COMPAT_MAGIC_##x

/* ── warn_unused_result / nodiscard ─────────────────────────────────────── */

/**
 * @brief warn_unused_result 属性
 * @details GCC/Clang 下标注函数返回值不可忽略
 */
#if COMPAT_WUR_ATTR_OK
#define COMPAT_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define COMPAT_WARN_UNUSED_RESULT
#endif

/**
 * @brief nodiscard 属性
 * @details C++17 [[nodiscard]] / C 下退化为 COMPAT_WARN_UNUSED_RESULT
 */
#ifdef __cplusplus
#if COMPAT_WUR_ATTR_OK
#define COMPAT_NODISCARD [[nodiscard]]
#else
#define COMPAT_NODISCARD
#endif
#else
#define COMPAT_NODISCARD COMPAT_WARN_UNUSED_RESULT
#endif

/* ── 返回值显式丢弃 ─────────────────────────────────────────────────────── */

/**
 * @brief 显式丢弃 warn_unused_result 标注函数的返回值
 * @param expr 表达式
 * @details GCC 14+ 下 (void)expr 对 warn_unused_result 无效, 须用此宏
 */
#if COMPAT_WUR_ATTR_OK
#define COMPAT_IGNORE_RESULT(expr)                                                                 \
    do                                                                                             \
    {                                                                                              \
        TYPEOF(expr) _compat_ign_ __attribute__((unused)) = (expr);                                \
    } while (0)
#else
#define COMPAT_IGNORE_RESULT(expr) ((void)(expr))
#endif

/* ── printf format 属性 ─────────────────────────────────────────────────── */

/**
 * @brief format 属性, 用 __printf__ 避免 poison printf 后属性标识符报错
 * @param fmt_arg   格式化字符串参数序号 (从 1 起)
 * @param first_var 第一个可变参数序号
 */
#if defined(__GNUC__)
#define COMPAT_FMT_PRINTF(fmt_arg, first_var)                                                      \
    __attribute__((format(__printf__, (fmt_arg), (first_var))))
#else
#define COMPAT_FMT_PRINTF(fmt_arg, first_var)
#endif

/* ── container_of ───────────────────────────────────────────────────────── */

/**
 * @brief Linux 风格 container_of — 从成员指针反推结构体指针
 * @param ptr    成员指针
 * @param type   结构体类型
 * @param member 成员名
 */
#if COMPAT_GNU_EXT_OK
#undef container_of
#define container_of(ptr, type, member)                                                            \
    ({                                                                                             \
        const TYPEOF(((type*)0)->member)* __mptr = (ptr);                                          \
        (type*)((char*)__mptr - __builtin_offsetof(type, member));                                 \
    })
#else
#ifndef container_of
#define container_of(ptr, type, member) ((type*)((char*)(ptr) - offsetof(type, member)))
#endif
#endif

/* ── likely / unlikely / unreachable ────────────────────────────────────── */

#if COMPAT_GNU_EXT_OK

#undef unlikely
#undef likely
#undef unreachable

/**
 * @brief 分支预测: 条件很可能为真
 * @param x 表达式
 */
#define likely(x) __builtin_expect(!!(x), 1)

/**
 * @brief 分支预测: 条件很可能为假
 * @param x 表达式
 */
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * @brief 标记不可达分支
 */
COMPAT_STATIC_INLINE COMPAT_NORETURN void unreachable(void) { __builtin_unreachable(); }

/**
 * @brief 静态池在 main 之前自动执行
 * @param x 优先级基数 — pre_execution(150) 实际为 constructor(250)
 * @details 高级用法: 用于 pre_execution 启动钩子
 */
#define pre_execution(x) __attribute__((constructor((x) + 100)))

#ifdef AUTO_FREE_PTR
/**
 * @brief cleanup 回调: 自动释放堆指针
 * @param ptr cleanup 传入的指针地址
 */
COMPAT_STATIC_INLINE void auto_free_ptr(void* ptr)
{
    void** real_ptr = (void**)ptr;
    if (*real_ptr != NULL)
    {
        free(*real_ptr);
        *real_ptr = NULL;
    }
}
/** @brief 作用域结束自动 free 的属性 */
#define AUTO_FREE __attribute__((cleanup(auto_free_ptr)))
#endif
#endif

/* ── RAM 执行段 ─────────────────────────────────────────────────────────── */

/**
 * @brief 将函数置于 RAM 执行段
 * @details 解决 Flash Cache Miss 导致的延迟抖动
 */
#define RAM_EXEC __attribute__((section(".ram_code")))

/* ── 伪随机数 ───────────────────────────────────────────────────────────── */

/** @brief Xorshift 全局状态 (ChaCha20 混合) */
static volatile uint32_t xorshift_state = 2463532242UL;

/**
 * @brief 伪随机数生成器 (ChaCha20 + Xorshift 混合)
 * @details 缓解 Flash Cache Miss 导致的延迟抖动; 调用方应自行加入噪音干扰
 * @param a 输入种子
 * @param b 输入种子
 * @param c 输入种子
 * @param d 输入种子
 * @return 伪随机数
 */
COMPAT_STATIC_INLINE uint32_t COMPAT_RAND(uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    a ^= xorshift_state;

    /* ChaCha20 四分之一轮 */
    a += b;
    d ^= a;
    d = (d << 16) | (d >> 16);
    c += d;
    b ^= c;
    b = (b << 12) | (b >> 20);
    a += b;
    d ^= a;
    d = (d << 8) | (d >> 24);
    c += d;
    b ^= c;
    b = (b << 7) | (b >> 25);

    /* ChaCha20 输出交替给 Xorshift */
    uint32_t x = a ^ b ^ c ^ d;

    /* Xorshift 核心变换 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;

    /* Xorshift 结果再与 ChaCha20 非线性项异或后输出 */
    xorshift_state = x ^ (c + d);
    return xorshift_state;
}

/* ── 内存操作封装 ───────────────────────────────────────────────────────── */

/**
 * @brief 内存设置 (带空指针检查)
 * @param dest 目标内存地址
 * @param src  填充字节值
 * @param size 内存大小
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数无效
 */
COMPAT_STATIC_INLINE int COMPAT_MEM_SET(void* dest, int src, size_t size)
{
    if (dest == NULL)
        return VFS_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memset(dest, src, size);
#else
        memset(dest, src, size);
#endif
    }
    return VFS_OK;
}

/**
 * @brief 内存复制 (带空指针检查)
 * @param dest 目标内存地址
 * @param src  源内存地址
 * @param size 内存大小
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数无效
 */
COMPAT_STATIC_INLINE int COMPAT_MEM_COPY(void* dest, const void* src, size_t size)
{
    if (dest == NULL || src == NULL)
        return VFS_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memcpy(dest, src, size);
#else
        memcpy(dest, src, size);
#endif
    }
    return VFS_OK;
}

/**
 * @brief 内存移动 (带空指针检查, 支持重叠区域)
 * @param dest 目标内存地址
 * @param src  源内存地址
 * @param size 内存大小
 * @return VFS_OK 成功, VFS_ERR_INVAL 参数无效
 */
COMPAT_STATIC_INLINE int COMPAT_MEM_MOVE(void* dest, const void* src, size_t size)
{
    if (dest == NULL || src == NULL)
        return VFS_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memmove(dest, src, size);
#else
        memmove(dest, src, size);
#endif
    }
    return VFS_OK;
}

/* ── 原子操作自适应层 ───────────────────────────────────────────────────── */

#if defined(__GNUC__) || defined(__clang__)

/**
 * @defgroup compat_atomic_gcc GCC / Clang 原子操作
 * @brief 统一调用 __atomic_* 内建函数, C/C++ 混编下行为一致
 * @{
 */

#define COMPAT_ATOMIC_INT int
#define COMPAT_ATOMIC_UINT uint32_t
#define COMPAT_ATOMIC_BOOL bool
#define COMPAT_ATOMIC_INIT(val)                                                                    \
    (val) /* 声明期初值: COMPAT_ATOMIC_INT x = COMPAT_ATOMIC_INIT(0);                         \
           */

#define COMPAT_MO_RELAXED __ATOMIC_RELAXED
#define COMPAT_MO_ACQUIRE __ATOMIC_ACQUIRE
#define COMPAT_MO_RELEASE __ATOMIC_RELEASE
#define COMPAT_MO_ACQ_REL __ATOMIC_ACQ_REL
#define COMPAT_MO_SEQ_CST __ATOMIC_SEQ_CST

#define COMPAT_ATOMIC_STORE(p, v, m) __atomic_store_n((p), (v), (m))
#define COMPAT_ATOMIC_LOAD(p, m) __atomic_load_n((p), (m))
#define COMPAT_ATOMIC_ADD_FETCH(p, v, m) __atomic_add_fetch((p), (v), (m))
#define COMPAT_ATOMIC_SUB_FETCH(p, v, m) __atomic_sub_fetch((p), (v), (m))
#define COMPAT_ATOMIC_FETCH_ADD(p, v, m) __atomic_fetch_add((p), (v), (m))
#define COMPAT_ATOMIC_FETCH_SUB(p, v, m) __atomic_fetch_sub((p), (v), (m))
#define COMPAT_ATOMIC_CAS(p, e, d, ms, mf) __atomic_compare_exchange_n((p), (e), (d), 0, (ms), (mf))
#define COMPAT_ATOMIC_EXCHANGE(p, v, m) __atomic_exchange_n((p), (v), (m))
#define COMPAT_ATOMIC_RUNTIME_INIT(p, val)                                                         \
    COMPAT_ATOMIC_STORE((p), (val), COMPAT_MO_RELAXED) /* 运行期初值, 等价 C11 atomic_init */

/** @} */

#elif defined(__cplusplus)

/**
 * @defgroup compat_atomic_cpp 纯 C++ 原子操作
 * @brief 使用 std::atomic, 适用于无 GCC 内建的 C++ 编译器
 * @{
 */

#include <atomic>

#define COMPAT_ATOMIC_INT std::atomic<int>
#define COMPAT_ATOMIC_UINT std::atomic<uint32_t>
#define COMPAT_ATOMIC_BOOL std::atomic<bool>
#define COMPAT_ATOMIC_INIT(val) std::atomic<int>(val)

#define COMPAT_MO_RELAXED std::memory_order_relaxed
#define COMPAT_MO_ACQUIRE std::memory_order_acquire
#define COMPAT_MO_RELEASE std::memory_order_release
#define COMPAT_MO_ACQ_REL std::memory_order_acq_rel
#define COMPAT_MO_SEQ_CST std::memory_order_seq_cst

#define COMPAT_ATOMIC_STORE(p, v, m) std::atomic_store_explicit((p), (v), (m))
#define COMPAT_ATOMIC_LOAD(p, m) std::atomic_load_explicit((p), (m))
#define COMPAT_ATOMIC_ADD_FETCH(p, v, m) (std::atomic_fetch_add_explicit((p), (v), (m)) + (v))
#define COMPAT_ATOMIC_SUB_FETCH(p, v, m) (std::atomic_fetch_sub_explicit((p), (v), (m)) - (v))
#define COMPAT_ATOMIC_FETCH_ADD(p, v, m) std::atomic_fetch_add_explicit((p), (v), (m))
#define COMPAT_ATOMIC_FETCH_SUB(p, v, m) std::atomic_fetch_sub_explicit((p), (v), (m))
#define COMPAT_ATOMIC_CAS(p, e, d, ms, mf)                                                         \
    std::atomic_compare_exchange_strong_explicit((p), (e), (d), (ms), (mf))
#define COMPAT_ATOMIC_EXCHANGE(p, v, m) std::atomic_exchange_explicit((p), (v), (m))
#define COMPAT_ATOMIC_RUNTIME_INIT(p, val) COMPAT_ATOMIC_STORE((p), (val), COMPAT_MO_RELAXED)

/** @} */

#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * @defgroup compat_atomic_c11 纯 C11 原子操作
 * @brief 使用 stdatomic.h, 适用于支持 C11 标准的编译器
 * @{
 */

#include <stdatomic.h>

#define COMPAT_ATOMIC_INT _Atomic(int)
#define COMPAT_ATOMIC_UINT _Atomic(uint32_t)
#define COMPAT_ATOMIC_BOOL _Atomic(bool)
#define COMPAT_ATOMIC_INIT(val) ATOMIC_VAR_INIT(val)

#define COMPAT_MO_RELAXED memory_order_relaxed
#define COMPAT_MO_ACQUIRE memory_order_acquire
#define COMPAT_MO_RELEASE memory_order_release
#define COMPAT_MO_ACQ_REL memory_order_acq_rel
#define COMPAT_MO_SEQ_CST memory_order_seq_cst

#define COMPAT_ATOMIC_STORE(p, v, m) atomic_store_explicit((p), (v), (m))
#define COMPAT_ATOMIC_LOAD(p, m) atomic_load_explicit((p), (m))
#define COMPAT_ATOMIC_ADD_FETCH(p, v, m) (atomic_fetch_add_explicit((p), (v), (m)) + (v))
#define COMPAT_ATOMIC_SUB_FETCH(p, v, m) (atomic_fetch_sub_explicit((p), (v), (m)) - (v))
#define COMPAT_ATOMIC_FETCH_ADD(p, v, m) atomic_fetch_add_explicit((p), (v), (m))
#define COMPAT_ATOMIC_FETCH_SUB(p, v, m) atomic_fetch_sub_explicit((p), (v), (m))
#define COMPAT_ATOMIC_CAS(p, e, d, ms, mf)                                                         \
    atomic_compare_exchange_strong_explicit((p), (e), (d), (ms), (mf))
#define COMPAT_ATOMIC_EXCHANGE(p, v, m) atomic_exchange_explicit((p), (v), (m))
#define COMPAT_ATOMIC_RUNTIME_INIT(p, val) COMPAT_ATOMIC_STORE((p), (val), COMPAT_MO_RELAXED)

/** @} */

#else

/**
 * @defgroup compat_atomic_fallback volatile 原子操作回退
 * @brief 不推荐在生产多线程环境使用, 仅用于编译跑通
 * @{
 */

#define COMPAT_ATOMIC_INT volatile int
#define COMPAT_ATOMIC_UINT volatile uint32_t
#define COMPAT_ATOMIC_BOOL volatile bool
#define COMPAT_ATOMIC_INIT(val) (val)

#define COMPAT_MO_RELAXED 0
#define COMPAT_MO_ACQUIRE 0
#define COMPAT_MO_RELEASE 0
#define COMPAT_MO_ACQ_REL 0
#define COMPAT_MO_SEQ_CST 0

#define COMPAT_ATOMIC_STORE(p, v, m) (*(p) = (v))
#define COMPAT_ATOMIC_LOAD(p, m) (*(p))
#define COMPAT_ATOMIC_ADD_FETCH(p, v, m) (*(p) += (v), *(p))
#define COMPAT_ATOMIC_SUB_FETCH(p, v, m) (*(p) -= (v), *(p))
#define COMPAT_ATOMIC_FETCH_ADD(p, v, m)                                                           \
    __extension__({                                                                                \
        __typeof__(*(p)) _o = *(p);                                                                \
        *(p) += (v);                                                                               \
        _o;                                                                                        \
    })
#define COMPAT_ATOMIC_FETCH_SUB(p, v, m)                                                           \
    __extension__({                                                                                \
        __typeof__(*(p)) _o = *(p);                                                                \
        *(p) -= (v);                                                                               \
        _o;                                                                                        \
    })
#define COMPAT_ATOMIC_CAS(p, e, d, ms, mf) ((*(p) == *(e)) ? (*(p) = (d), 1) : (*(e) = *(p), 0))
#define COMPAT_ATOMIC_EXCHANGE(p, v, m)                                                            \
    __extension__({                                                                                \
        __typeof__(*(p)) _o = *(p);                                                                \
        *(p) = (v);                                                                                \
        _o;                                                                                        \
    })
#define COMPAT_ATOMIC_RUNTIME_INIT(p, val) COMPAT_ATOMIC_STORE((p), (val), COMPAT_MO_RELAXED)

/** @} */

#endif

#endif /* COMPILER_COMPAT_H */
