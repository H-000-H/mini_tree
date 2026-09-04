/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file compiler_compat.h
 *@brief compiler compat 头文件
 *@author H-000-H
 *@details
 *   compiler_compat — 编译器兼容性抽象层
 *   统一 GCC/Clang 的 __attribute__ 与内置函数差异, 功能受 Kconfig 开关控制
 *   提供 warn_unused_result、format、container_of、likely/unlikely、MINI_RAM_EXEC 等通用宏
 */

#ifndef COMPILER_COMPAT_H
#define COMPILER_COMPAT_H

#include "compiler_inline.h"
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* MINI_TYPEOF */
/* -------------------------------------------------------------------------- */

/**
 * @brief 获取表达式的类型
 * @param[in] expr 表达式
 */
#ifdef __cplusplus
#define MINI_TYPEOF(expr) decltype(expr)
#else
#define MINI_TYPEOF(expr) typeof(expr)
#endif

/* -------------------------------------------------------------------------- */
/* Kconfig 配置回退 */
/* -------------------------------------------------------------------------- */

#ifndef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 0
#if defined(__has_include)
#if __has_include("config.h")
/* IWYU pragma: keep — Kconfig 生成头 (cmake KCONFIG_GEN_DIR), 头里大量 CONFIG_* 宏被下方 COMAT_CFG_ENABLED / 单元隐式消费, clangd 在 IDE 阶段会误报 unused-includes。 */
#include "config.h" /* IWYU pragma: keep */
#undef COMPAT_HAVE_KCONFIG
#define COMPAT_HAVE_KCONFIG 1
#endif
#endif
#endif

/* -------------------------------------------------------------------------- */
/* 功能开关 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 功能开关宏
 * @param[in] sym 功能开关名称 (不含 CONFIG_ 前缀)
 */
#define COMPAT_CFG_ENABLED(sym) ((!COMPAT_HAVE_KCONFIG) || defined(CONFIG_##sym))

/**
 * @brief GNU 扩展是否可用
 * @details 通过嵌套 #if 计算为字面量 0/1; 受 CONFIG_COMPILER_GNU_EXTENSIONS 控制
 */
#if (defined(__GNUC__) || defined(__clang__))
#if COMPAT_HAVE_KCONFIG
#ifdef CONFIG_COMPILER_GNU_EXTENSIONS
#define MINI_GNU_EXT_OK 1
#else
#define MINI_GNU_EXT_OK 0
#endif
#else
#define MINI_GNU_EXT_OK 1
#endif
#else
#define MINI_GNU_EXT_OK 0
#endif

/**
 * @brief warn_unused_result 属性是否可用
 * @details 通过嵌套 #if 计算为字面量 0/1; 受 CONFIG_COMPILER_WARN_UNUSED_RESULT 控制
 */
#if MINI_GNU_EXT_OK
#if COMPAT_HAVE_KCONFIG
#ifdef CONFIG_COMPILER_WARN_UNUSED_RESULT
#define MINI_WUR_ATTR_OK 1
#else
#define MINI_WUR_ATTR_OK 0
#endif
#else
#define MINI_WUR_ATTR_OK 1
#endif
#else
#define MINI_WUR_ATTR_OK 0
#endif

/* -------------------------------------------------------------------------- */
/* 基础属性 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 对齐属性
 * @param[in] n 对齐字节数
 */
#define MINI_ALIGNED(n) __attribute__((aligned(n)))
/** @brief 弱符号属性 */
#define MINI_WEAK __attribute__((weak))
/** @brief 不返回函数属性 */
#define MINI_NO_RETURN __attribute__((noreturn))
/** @brief 打包属性 */
#define MINI_PACKED __attribute__((packed))
/** @brief 强制内联属性 */
#define MINI_ALWAYS_INLINE __attribute__((always_inline)) inline
/** @brief 禁止内联属性 */
#define MINI_NO_INLINE __attribute__((noinline))
/** @brief 展开调用链所有内联属性 */
#define MINI_FLATTEN __attribute__((flatten))
/** @brief 纯函数属性 */
#define MINI_PURE __attribute__((pure))
/** @brief 常量函数属性 */
#define MINI_CONST_FUNC __attribute__((const))
/** @brief 热路径函数属性 */
#define MINI_HOT __attribute__((hot))
/** @brief 冷路径函数属性 */
#define MINI_COLD __attribute__((cold))
/** @brief 使用属性 (防止链接器剔除) */
#define MINI_USED __attribute__((used))
/** @brief 未使用属性 (抑制警告) */
#define MINI_UNUSED __attribute__((unused))
/** @brief 未使用参数压制 (用于函数体内显式标记参数/局部变量未引用)
 * @note 必须为宏而非内联函数: 内联函数里的 (void) 只作用于函数自身的参数,
 *       无法抑制调用处变量的 unused 警告. */
#define MINI_UNUSED_PARAM(x) ((void)(x))
/** @brief 可能别名属性 */
#define MINI_MAY_ALIAS __attribute__((may_alias))

/* -------------------------------------------------------------------------- */
/* 扩展属性 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 贯穿属性 (switch fallthrough)
 */
#if defined(__cplusplus) && __cplusplus >= 201703L
#define MINI_FALLTHROUGH [[fallthrough]]
#elif MINI_GNU_EXT_OK
#define MINI_FALLTHROUGH __attribute__((fallthrough))
#else
#define MINI_FALLTHROUGH ((void)0)
#endif

/**
 * @brief 静态断言
 * @param[in] cond 断言条件
 * @param[in] msg  断言消息
 */
#if defined(__cplusplus) && __cplusplus >= 201103L
#define MINI_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define MINI_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
#define MINI_STATIC_ASSERT(cond, msg) typedef char COMPAT_SA_##__LINE__[(cond) ? 1 : -1]
#endif

/**
 * @brief 编译期错误属性 (触发时直接编译失败)
 * @param[in] msg 编译期错误消息
 */
#if MINI_GNU_EXT_OK
#define MINI_COMPILE_ERROR(msg) __attribute__((error(msg)))
#else
#define MINI_COMPILE_ERROR(msg)
#endif

/* -------------------------------------------------------------------------- */
/* 编译器内建函数 */
/* -------------------------------------------------------------------------- */

#include "status.h"

/**
 * @brief 触发 CPU 陷阱, 用于不可恢复错误
 */
MINI_STATIC_INLINE MINI_NO_RETURN void MINI_TRAP(void) { __builtin_trap(); }

/**
 * @brief 计算 x 的末尾零位 (count trailing zeros)
 * @param[in] x 32 位无符号整数
 * @return 末尾零位个数; x 为 0 时返回 32
 */
#if defined(__CORTEX_M0) || defined(__CORTEX_M0PLUS)
MINI_STATIC_INLINE uint32_t MINI_CTZ(uint32_t x) /*二分查找*/
{
    if (x == 0)
        return 32; // 0无定义，自定义返回32做异常标记
    uint8_t  count = 0;
    uint32_t shift;
    for (shift = 16; shift; shift >>= 1)
    {
        if ((x & ((1U << shift) - 1U)) == 0)
        {
            count += shift;
            x >>= shift;
        }
    }
    return count;
}
#else
MINI_STATIC_INLINE uint32_t MINI_CTZ(uint32_t x)
{
    if (x == 0U)
        return 32U;
    return (uint32_t)__builtin_ctz(x);
}
#endif
/**
 * @brief 计算 x 的前导零位 (count leading zeros)
 * @param[in] x 32 位无符号整数
 * @return 前导零位个数; x 为 0 时返回 32
 */

#if defined(__CORTEX_M0) || defined(__CORTEX_M0PLUS)
MINI_STATIC_INLINE MINI_CONST_FUNC uint32_t MINI_CLZ(uint32_t x)
{
    uint32_t count = 31;
    if (x == 0)
        return 32;

    for (uint32_t shift = 16; shift; shift >>= 1)
    {
        uint32_t val = x >> shift;
        if (val)
        {
            x = val;
            count -= shift;
        }
    }
    return count;
}
#else
MINI_STATIC_INLINE MINI_CONST_FUNC uint32_t MINI_CLZ(uint32_t x)
{
    if (x == 0U)
        return 32U;
    return (uint32_t)__builtin_clz(x);
}
#endif
/**
 * @brief 计算 x 的置位个数 (population count)
 * @param[in] x 32 位无符号整数
 * @return 置位个数
 */
MINI_STATIC_INLINE MINI_CONST_FUNC uint32_t MINI_POPCOUNT(uint32_t x) { return (uint32_t)__builtin_popcount(x); }

/**
 * @brief 返回最低置位的位号 (find first set)
 * @param[in] x 32 位无符号整数
 * @return 最低置位位号 (1-based); x 为 0 时返回 0
 */
MINI_STATIC_INLINE MINI_CONST_FUNC uint32_t MINI_FFS(uint32_t x) { return (uint32_t)__builtin_ffs(x); }

/**
 * @brief 预取内存到缓存
 * @param[in] addr     目标地址
 * @param[in] rw       0=读, 1=写
 * @param[in] locality 时间局部性 0~3
 */
#define MINI_PREFETCH(addr, rw, locality) __builtin_prefetch((addr), (rw), (locality))

/**
 * @brief 假设指针对齐
 * @param[in] ptr 指针
 * @param[in] n   对齐字节数
 */
#if MINI_GNU_EXT_OK
#define MINI_ASSUME_ALIGNED(ptr, n) __builtin_assume_aligned((ptr), (n))
#else
#define MINI_ASSUME_ALIGNED(ptr, n) (ptr)
#endif

/**
 * @brief 编译期判断表达式是否为常量 (用于优化分支选择)
 * @param[in] expr 待判断表达式
 */
#define MINI_CONSTANT_P(expr) __builtin_constant_p(expr)

/* -------------------------------------------------------------------------- */
/* 设备魔法槽枚举 */
/* -------------------------------------------------------------------------- */

/** @brief 魔法槽步长 (各设备类型基址间隔) */
#define MINI_MAGIC_SLOT_STRIDE 0x100u

/** @brief 设备魔法槽 X-Macro 表 */
#define MINI_MAGIC_TABLE(X)                                                                                                                          \
    X(SPI, 0x00)                                                                                                                                     \
    X(UART, 0x01)                                                                                                                                    \
    X(I2C, 0x02)                                                                                                                                     \
    X(I2S, 0x03)                                                                                                                                     \
    X(USB, 0x04)                                                                                                                                     \
    X(CAN, 0x05)                                                                                                                                     \
    X(ETH, 0x06)                                                                                                                                     \
    X(GPIO, 0x07)                                                                                                                                    \
    X(SDIO, 0x08)                                                                                                                                    \
    X(W25Q64, 0x09)                                                                                                                                  \
    X(TIM, 0x0A)                                                                                                                                     \
    X(ADC, 0x0B)                                                                                                                                     \
    X(DAC, 0x0C)                                                                                                                                     \
    X(RTC, 0x0D)                                                                                                                                     \
    X(IWDG, 0x0E)                                                                                                                                    \
    X(WWDG, 0x0F)                                                                                                                                    \
    X(WS2812, 0x10)                                                                                                                                  \
    X(ST7789, 0x11)                                                                                                                                  \
    X(MAX98357A, 0x12)                                                                                                                               \
    X(GPIOKEY, 0x13)                                                                                                                                 \
    X(LIGHT, 0x14)                                                                                                                                   \
    X(PWMBL, 0x15)                                                                                                                                   \
    X(SHT30, 0x16)                                                                                                                                   \
    X(MPU6050, 0x17)                                                                                                                                 \
    X(AT24C02, 0x18)                                                                                                                                 \
    X(SSD1306, 0x19)                                                                                                                                 \
    X(BH1750, 0x1A)                                                                                                                                  \
    X(BME280, 0x1B)                                                                                                                                  \
    X(FT5X06, 0x1C)                                                                                                                                  \
    X(XPT2046, 0x1D)                                                                                                                                 \
    X(SX1278, 0x1E)                                                                                                                                  \
    X(EPAPER, 0x1F)                                                                                                                                  \
    X(NEO_M8N, 0x20)                                                                                                                                 \
    X(HC05, 0x22)                                                                                                                                    \
    X(RS485_MODBUS, 0x23)                                                                                                                            \
    X(DRV8833, 0x24)                                                                                                                                 \
    X(SG90, 0x25)                                                                                                                                    \
    X(BUZZER, 0x26)                                                                                                                                  \
    X(RELAY, 0x27)                                                                                                                                   \
    X(DS18B20, 0x28)                                                                                                                                 \
    X(SN65HVD230, 0x29)                                                                                                                              \
    X(W25QXX, 0x2A)                                                                                                                                  \
    X(AHT20, 0x2B)                                                                                                                                   \
    X(SHT40, 0x2C)                                                                                                                                   \
    X(BMP280, 0x2D)                                                                                                                                  \
    X(VL53L0X, 0x2E)                                                                                                                                 \
    X(PCF8574, 0x2F)                                                                                                                                 \
    X(ADS1115, 0x30)                                                                                                                                 \
    X(INA219, 0x31)                                                                                                                                  \
    X(SH1106, 0x32)                                                                                                                                  \
    X(NRF24L01, 0x33)                                                                                                                                \
    X(RC522, 0x34)                                                                                                                                   \
    X(MAX7219, 0x35)                                                                                                                                 \
    X(PN532, 0x37)                                                                                                                                   \
    X(DFPLAYER, 0x38)                                                                                                                                \
    X(DISPLAY, 0x39)                                                                                                                                 \
    X(MODEM, 0x3A)

/**
 * @brief 魔法槽枚举生成器
 * @param[in] name 设备名称
 * @param[in] slot 槽序号
 */
#define MINI_MAGIC_ENUM(name, slot) MINI_MAGIC_##name = (uint32_t)((slot) * MINI_MAGIC_SLOT_STRIDE),

/** @brief 设备魔法槽枚举 */
enum
{
    MINI_MAGIC_TABLE(MINI_MAGIC_ENUM)
};

#undef MINI_MAGIC_ENUM

/**
 * @brief 获取设备魔法槽常量
 * @param[in] x 设备名称 (SPI, UART, ...)
 */
#define MINI_MAGIC(x) MINI_MAGIC_##x

/* -------------------------------------------------------------------------- */
/* warn_unused_result / nodiscard */
/* -------------------------------------------------------------------------- */

/**
 * @brief warn_unused_result 属性
 * @details GCC/Clang 下标注函数返回值不可忽略
 */
#if MINI_WUR_ATTR_OK
#define MINI_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#else
#define MINI_WARN_UNUSED_RESULT
#endif

/**
 * @brief nodiscard 属性
 * @details C++17 [[nodiscard]] / C 下退化为 MINI_WARN_UNUSED_RESULT
 */
#ifdef __cplusplus
#if MINI_WUR_ATTR_OK
#define MINI_NODISCARD [[nodiscard]]
#else
#define MINI_NODISCARD
#endif
#else
#define MINI_NODISCARD MINI_WARN_UNUSED_RESULT
#endif

/* -------------------------------------------------------------------------- */
/* 返回值显式丢弃 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 显式丢弃 warn_unused_result 标注函数的返回值
 * @param[in] expr 表达式
 * @details GCC 14+ 下 (void)expr 对 warn_unused_result 无效, 须用此宏
 */
#if MINI_WUR_ATTR_OK
#define MINI_IGNORE_RESULT(expr)                                                                                                                     \
    do                                                                                                                                               \
    {                                                                                                                                                \
        MINI_TYPEOF(expr) _compat_ign_ __attribute__((unused)) = (expr);                                                                             \
    } while (0)
#else
#define MINI_IGNORE_RESULT(expr) ((void)(expr))
#endif

/* -------------------------------------------------------------------------- */
/* printf format 属性 */
/* -------------------------------------------------------------------------- */

/**
 * @brief format 属性, 用 __printf__ 避免 poison printf 后属性标识符报错
 * @param[in] fmt_arg   格式化字符串参数序号 (从 1 起)
 * @param[in] first_var 第一个可变参数序号
 */
#if defined(__GNUC__)
#define MINI_FMT_PRINTF(fmt_arg, first_var) __attribute__((format(__printf__, (fmt_arg), (first_var))))
#else
#define MINI_FMT_PRINTF(fmt_arg, first_var)
#endif

/* -------------------------------------------------------------------------- */
/* container_of */
/* -------------------------------------------------------------------------- */

/**
 * @brief Linux 风格 container_of — 从成员指针反推结构体指针
 * @param[in] ptr    成员指针
 * @param[in] type   结构体类型
 * @param[in] member 成员名
 */
#if MINI_GNU_EXT_OK
#undef container_of
#define container_of(ptr, type, member)                                                                                                              \
    ({                                                                                                                                               \
        const MINI_TYPEOF(((type*)0)->member)* __mptr = (ptr);                                                                                       \
        (type*)((char*)__mptr - __builtin_offsetof(type, member));                                                                                   \
    })
#else
#ifndef container_of
#define container_of(ptr, type, member) ((type*)((char*)(ptr) - offsetof(type, member)))
#endif
#endif

/* -------------------------------------------------------------------------- */
/* likely / unlikely / unreachable */
/* -------------------------------------------------------------------------- */

#if MINI_GNU_EXT_OK

#undef unlikely
#undef likely
#undef unreachable

/**
 * @brief 分支预测: 条件很可能为真
 * @param[in] x 表达式
 */
#define likely(x) __builtin_expect(!!(x), 1)

/**
 * @brief 分支预测: 条件很可能为假
 * @param[in] x 表达式
 */
#define unlikely(x) __builtin_expect(!!(x), 0)

/**
 * @brief 标记不可达分支
 */
MINI_STATIC_INLINE MINI_NO_RETURN void unreachable(void) { __builtin_unreachable(); }

/* -------------------------------------------------------------------------- */
/* mini_pre_execution 启动优先级 */
/* constructor 实际优先级 = 基数 + 100; 数值越小越先执行。 */
/* 依赖链: 总线/OSAL 资源池(150) → 信号量/队列/事件组池(151/152/153) → 驱动池(160) → 调度器(161) → */
/* 中断下半部池(170)。 */
/* -------------------------------------------------------------------------- */
#define MINI_PRE_EXEC_PRIO_RES_POOL 150    /* 总线池 / VFS 私有池 / OSAL 互斥锁池 */
#define MINI_PRE_EXEC_PRIO_SEM_POOL 151    /* OSAL 信号量池 / DAC 私有池 */
#define MINI_PRE_EXEC_PRIO_QUEUE_POOL 152  /* OSAL 队列池 (osal_null) */
#define MINI_PRE_EXEC_PRIO_EVENT_POOL 153  /* OSAL 事件组池 (CONFIG_OSAL_EVENT) */
#define MINI_PRE_EXEC_PRIO_DRIVER_POOL 160 /* 驱动静态池 / VFS client 池 / 协调式调度器 */
#define MINI_PRE_EXEC_PRIO_SCHEDULER 161   /* 抢占式调度器早期初始化 */
#define MINI_PRE_EXEC_PRIO_IRQ_BOTTOM 170  /* 中断下半部池 */

/**
 * @brief 静态池在 main 之前自动执行
 * @param[in] x 优先级基数 (建议用 MINI_PRE_EXEC_PRIO_* 常量) — mini_pre_execution(150) 实际为
 * constructor(250)
 * @details 高级用法: 用于 mini_pre_execution 启动钩子
 */
#define mini_pre_execution(x) __attribute__((constructor((x) + 100)))

#ifdef AUTO_FREE_PTR
/**
 * @brief cleanup 回调: 自动释放堆指针
 * @param[in] ptr cleanup 传入的指针地址
 */
MINI_STATIC_INLINE void auto_free_ptr(void* ptr)
{
    void** real_ptr = (void**)ptr;
    if (*real_ptr != NULL)
    {
        free(*real_ptr);
        *real_ptr = NULL;
    }
}
/** @brief 作用域结束自动 free 的属性 */
#define MINI_AUTO_FREE __attribute__((cleanup(auto_free_ptr)))
#endif
#endif

/* -------------------------------------------------------------------------- */
/* RAM 执行段 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 将函数置于 RAM 执行段
 * @details 解决 Flash Cache Miss 导致的延迟抖动
 */
#define MINI_RAM_EXEC __attribute__((section(".ram_code")))

/* -------------------------------------------------------------------------- */
/* 低功耗等待 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 进入低功耗等待 (Wait For Interrupt)
 * @note  平台无关的 WFI 封装: Cortex-M 用 __WFI, RISCV 用 wfi;
 *        调用方需保证随后确有中断到达, 否则 CPU 睡死.
 */
#if defined(__CORTEX_M0) || defined(__CORTEX_M0PLUS) || defined(__CORTEX_M3) || defined(__CORTEX_M4) || defined(__CORTEX_M4F) || defined(__CORTEX_M7)
#define MINI_WFI() __WFI()
#elif defined(__riscv)
#define MINI_WFI() __asm__ volatile("wfi")
#else
#define MINI_WFI() ((void)0)
#endif

/* -------------------------------------------------------------------------- */
/* 内存操作封装 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 内存设置 (带空指针检查)
 * @param[in] dest 目标内存地址
 * @param[in] src  填充字节值
 * @param[in] size 内存大小
 * @return MINI_OK 成功, MINI_ERR_INVAL 参数无效
 */
MINI_STATIC_INLINE int MINI_MEM_SET(void* dest, int src, size_t size)
{
    if (dest == NULL)
        return MINI_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memset(dest, src, size);
#else
        memset(dest, src, size);
#endif
    }
    return MINI_OK;
}

/**
 * @brief 内存复制 (带空指针检查)
 * @param[in] dest 目标内存地址
 * @param[in] src  源内存地址
 * @param[in] size 内存大小
 * @return MINI_OK 成功, MINI_ERR_INVAL 参数无效
 */
MINI_STATIC_INLINE int MINI_MEM_COPY(void* dest, const void* src, size_t size)
{
    if (dest == NULL || src == NULL)
        return MINI_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memcpy(dest, src, size);
#else
        memcpy(dest, src, size);
#endif
    }
    return MINI_OK;
}

/**
 * @brief 内存移动 (带空指针检查, 支持重叠区域)
 * @param[in] dest 目标内存地址
 * @param[in] src  源内存地址
 * @param[in] size 内存大小
 * @return MINI_OK 成功, MINI_ERR_INVAL 参数无效
 */
MINI_STATIC_INLINE int MINI_MEM_MOVE(void* dest, const void* src, size_t size)
{
    if (dest == NULL || src == NULL)
        return MINI_ERR_INVAL;
    if (size > 0)
    {
#if defined(__GNUC__) || defined(__clang__)
        __builtin_memmove(dest, src, size);
#else
        memmove(dest, src, size);
#endif
    }
    return MINI_OK;
}

/* -------------------------------------------------------------------------- */
/* MMIO 寄存器访问 */
/* -------------------------------------------------------------------------- */

/**
 * @defgroup compat_reg MMIO 寄存器访问
 * @brief 内存映射寄存器 (MMIO) 读写, 全部 static inline, 编译期零开销
 *
 * 设计说明:
 * - 寄存器是独立内存映射地址, 单次 volatile 读/写在 C 语义上已不可分割,
 * - 地址参数统一用 uintptr_t, 平台可直接传裸地址常量或符号地址.
 * @{
 */

/** @brief 读 32 位寄存器 */
MINI_STATIC_INLINE uint32_t MINI_REG_READ32(uintptr_t addr) { return *(volatile uint32_t*)addr; }

/** @brief 写 32 位寄存器 */
MINI_STATIC_INLINE void MINI_REG_WRITE32(uintptr_t addr, uint32_t val) { *(volatile uint32_t*)addr = val; }

/**
 * @brief 读 64 位寄存器
 * @note 在 32 位 MCU 上 64 位访问由两次 32 位总线事务组成, 非原子;
 *       如需原子读请用两个 MINI_REG_READ32 并在临界区内组合.
 */
MINI_STATIC_INLINE uint64_t MINI_REG_READ64(uintptr_t addr) { return *(volatile uint64_t*)addr; }

/**
 * @brief 写 64 位寄存器
 * @note 在 32 位 MCU 上 64 位访问由两次 32 位总线事务组成, 非原子;
 *       如需原子写请用两个 MINI_REG_WRITE32 并在临界区内组合.
 */
MINI_STATIC_INLINE void MINI_REG_WRITE64(uintptr_t addr, uint64_t val) { *(volatile uint64_t*)addr = val; }

/** @brief 读 16 位寄存器 */
MINI_STATIC_INLINE uint16_t MINI_REG_READ16(uintptr_t addr) { return *(volatile uint16_t*)addr; }

/** @brief 写 16 位寄存器 */
MINI_STATIC_INLINE void MINI_REG_WRITE16(uintptr_t addr, uint16_t val) { *(volatile uint16_t*)addr = val; }

/** @brief 读 8 位寄存器 */
MINI_STATIC_INLINE uint8_t MINI_REG_READ8(uintptr_t addr) { return *(volatile uint8_t*)addr; }

/** @brief 写 8 位寄存器 */
MINI_STATIC_INLINE void MINI_REG_WRITE8(uintptr_t addr, uint8_t val) { *(volatile uint8_t*)addr = val; }

/**
 * @brief 读-改-写 32 位寄存器 (非原子)
 * @param[in] addr       寄存器地址
 * @param[in] clear_mask 先清零的位掩码 (1 = 清零)
 * @param[in] set_mask   再置位的位掩码 (1 = 置位)
 * @note 同一地址并发 RMW 需要调用方保护临界区 (关中断 / spinlock);
 *       需要原子 RMW 时改用 MINI_ATOMIC_*.
 */
MINI_STATIC_INLINE void MINI_REG_MODIFY32(uintptr_t addr, uint32_t clear_mask, uint32_t set_mask)
{
    uint32_t reg_value = MINI_REG_READ32(addr);
    reg_value = (reg_value & ~clear_mask) | set_mask;
    MINI_REG_WRITE32(addr, reg_value);
}

/* -------------------------------------------------------------------------- */
/* Bit 操作 */
/* -------------------------------------------------------------------------- */

/**
 * @brief 生成位掩码: MINI_BIT(n) = 1U << n
 * @param[in] n 位序号 (0..31)
 * @return (1U << n)
 * @note 宏 + __typeof__ 编译期类型检查:
 *       - GCC/Clang (C): 用 __builtin_types_compatible_p 断言 n 为整数类型,
 *         类型不符时结果为 void 表达式, 用于值上下文即编译失败;
 *       - 同时保留编译期常量能力 (类型正确时结果是常量表达式,
 *         可用于数组维度 / case 标签);
 *       - C++ / 其他编译器: 退化为普通宏 (C++ 强类型本身有保护).
 */
#if defined(__cplusplus)
#define MINI_BIT(n) (1UL << (n))
#elif defined(__GNUC__) || defined(__clang__)
#define MINI_BIT(n)                                                                                                                                  \
    __builtin_choose_expr(__builtin_types_compatible_p(MINI_TYPEOF(n), unsigned int) || __builtin_types_compatible_p(MINI_TYPEOF(n), int) ||         \
                              __builtin_types_compatible_p(MINI_TYPEOF(n), unsigned long) || __builtin_types_compatible_p(MINI_TYPEOF(n), long),     \
                          (1UL << (n)), ((void)0)) /* 非整数类型: 结果 void, 用于值上下文即编译错误 */
#else
#define MINI_BIT(n) (1UL << (n))
#endif

/**
 * @brief 置位寄存器中的若干位 (read-modify-write, 非原子)
 * @param[in] addr 寄存器地址
 * @param[in] mask 置位掩码 (1 = 置位, 等价 reg |= mask)
 * @note 等价于 `reg |= mask`; 并发 RMW 需调用方保护临界区
 */
MINI_STATIC_INLINE void MINI_REG_SET_BITS(uintptr_t addr, uint32_t mask) { MINI_REG_MODIFY32(addr, 0U, mask); }

/**
 * @brief 清位寄存器中的若干位 (read-modify-write, 非原子)
 * @param[in] addr 寄存器地址
 * @param[in] mask 清位掩码 (1 = 清零, 等价 reg &= ~mask)
 * @note 等价于 `reg &= ~mask`; 并发 RMW 需调用方保护临界区
 */
MINI_STATIC_INLINE void MINI_REG_CLR_BITS(uintptr_t addr, uint32_t mask) { MINI_REG_MODIFY32(addr, mask, 0U); }

/**
 * @brief 翻转寄存器中的若干位 (read-modify-write, 非原子)
 * @param[in] addr 寄存器地址
 * @param[in] mask 翻转掩码 (1 = 取反, 等价 reg ^= mask)
 * @note 等价于 `reg ^= mask`; 并发 RMW 需调用方保护临界区
 */
MINI_STATIC_INLINE void MINI_REG_TOGGLE_BITS(uintptr_t addr, uint32_t mask)
{
    uint32_t reg_value = MINI_REG_READ32(addr);
    reg_value ^= mask;
    MINI_REG_WRITE32(addr, reg_value);
}

/**
 * @brief 置位寄存器中的单个位
 * @param[in] addr 寄存器地址
 * @param[in] n    位序号 (0..31)
 */
MINI_STATIC_INLINE void MINI_REG_SET_BIT(uintptr_t addr, uint32_t n) { MINI_REG_SET_BITS(addr, MINI_BIT(n)); }

/**
 * @brief 清位寄存器中的单个位
 * @param[in] addr 寄存器地址
 * @param[in] n    位序号 (0..31)
 */
MINI_STATIC_INLINE void MINI_REG_CLR_BIT(uintptr_t addr, uint32_t n) { MINI_REG_CLR_BITS(addr, MINI_BIT(n)); }

/**
 * @brief 翻转寄存器中的单个位
 * @param[in] addr 寄存器地址
 * @param[in] n    位序号 (0..31)
 */
MINI_STATIC_INLINE void MINI_REG_TOGGLE_BIT(uintptr_t addr, uint32_t n) { MINI_REG_TOGGLE_BITS(addr, MINI_BIT(n)); }

/**
 * @brief 读取寄存器中若干位 (掩码提取, 不右移)
 * @param[in] addr 寄存器地址
 * @param[in] mask 要提取的位掩码
 * @return reg & mask
 */
MINI_STATIC_INLINE uint32_t MINI_REG_GET_BITS(uintptr_t addr, uint32_t mask) { return MINI_REG_READ32(addr) & mask; }

/**
 * @brief 提取寄存器字段并右移对齐到 0 位
 * @param[in] addr  寄存器地址
 * @param[in] mask  字段掩码 (如 0x0F00)
 * @param[in] shift 字段低位偏移 (如 8)
 * @return (reg & mask) >> shift
 */
MINI_STATIC_INLINE uint32_t MINI_REG_FIELD_GET(uintptr_t addr, uint32_t mask, uint32_t shift) { return (MINI_REG_READ32(addr) & mask) >> shift; }

/**
 * @brief 更新寄存器字段 (读-改-写, 非原子)
 * @param[in] addr  寄存器地址
 * @param[in] mask  字段掩码 (如 0x0F00)
 * @param[in] shift 字段低位偏移 (如 8)
 * @param[in] val   字段新值 (未预移位, 函数内自动左移)
 * @note 等价于 reg = (reg & ~mask) | ((val << shift) & mask); 并发 RMW 需临界区
 */
MINI_STATIC_INLINE void MINI_REG_FIELD_SET(uintptr_t addr, uint32_t mask, uint32_t shift, uint32_t val)
{
    MINI_REG_MODIFY32(addr, mask, (val << shift) & mask);
}

/** @} */

/* -------------------------------------------------------------------------- */
/* 原子操作自适应层 */
/* -------------------------------------------------------------------------- */

#if defined(__GNUC__) || defined(__clang__)

/**
 * @defgroup compat_atomic_gcc GCC / Clang 原子操作
 * @brief 统一调用 __atomic_* 内建函数, C/C++ 混编下行为一致
 * @{
 */

#define MINI_ATOMIC_INT int
#define MINI_ATOMIC_UINT uint32_t
#define MINI_ATOMIC_UINT16 uint16_t
#define MINI_ATOMIC_UINT8 uint8_t
#define MINI_ATOMIC_UINT32 uint32_t
#define MINI_ATOMIC_BOOL bool
#define MINI_ATOMIC_INIT(val)                                                                                                                        \
    (val) /* 声明期初值: MINI_ATOMIC_INT x = MINI_ATOMIC_INIT(0);                                                                               \
           */

#define MINI_RELAXED __ATOMIC_RELAXED
#define MINI_ACQUIRE __ATOMIC_ACQUIRE
#define MINI_RELEASE __ATOMIC_RELEASE
#define MINI_ACQ_REL __ATOMIC_ACQ_REL
#define MINI_SEQ_CST __ATOMIC_SEQ_CST

#define MINI_ATOMIC_STORE(p, v, m) __atomic_store_n((p), (v), (m))
#define MINI_ATOMIC_LOAD(p, m) __atomic_load_n((p), (m))
#define MINI_ATOMIC_ADD_FETCH(p, v, m) __atomic_add_fetch((p), (v), (m))
#define MINI_ATOMIC_SUB_FETCH(p, v, m) __atomic_sub_fetch((p), (v), (m))
#define MINI_ATOMIC_FETCH_ADD(p, v, m) __atomic_fetch_add((p), (v), (m))
#define MINI_ATOMIC_FETCH_SUB(p, v, m) __atomic_fetch_sub((p), (v), (m))
#define MINI_ATOMIC_CAS(p, e, d, ms, mf) __atomic_compare_exchange_n((p), (e), (d), 0, (ms), (mf))
#define MINI_ATOMIC_EXCHANGE(p, v, m) __atomic_exchange_n((p), (v), (m))
#define MINI_ATOMIC_RUNTIME_INIT(p, val) MINI_ATOMIC_STORE((p), (val), MINI_RELAXED) /* 运行期初值, 等价 C11 atomic_init */

/** @} */

#elif defined(__cplusplus)

/**
 * @defgroup compat_atomic_cpp 纯 C++ 原子操作
 * @brief 使用 std::atomic, 适用于无 GCC 内建的 C++ 编译器
 * @{
 */

#include <atomic>

#define MINI_ATOMIC_INT std::atomic<int>
#define MINI_ATOMIC_UINT std::atomic<uint32_t>
#define MINI_ATOMIC_BOOL std::atomic<bool>
#define MINI_ATOMIC_INIT(val) std::atomic<int>(val)

#define MINI_RELAXED std::memory_order_relaxed
#define MINI_ACQUIRE std::memory_order_acquire
#define MINI_RELEASE std::memory_order_release
#define MINI_ACQ_REL std::memory_order_acq_rel
#define MINI_SEQ_CST std::memory_order_seq_cst

#define MINI_ATOMIC_STORE(p, v, m) std::atomic_store_explicit((p), (v), (m))
#define MINI_ATOMIC_LOAD(p, m) std::atomic_load_explicit((p), (m))
#define MINI_ATOMIC_ADD_FETCH(p, v, m) (std::atomic_fetch_add_explicit((p), (v), (m)) + (v))
#define MINI_ATOMIC_SUB_FETCH(p, v, m) (std::atomic_fetch_sub_explicit((p), (v), (m)) - (v))
#define MINI_ATOMIC_FETCH_ADD(p, v, m) std::atomic_fetch_add_explicit((p), (v), (m))
#define MINI_ATOMIC_FETCH_SUB(p, v, m) std::atomic_fetch_sub_explicit((p), (v), (m))
#define MINI_ATOMIC_CAS(p, e, d, ms, mf) std::atomic_compare_exchange_strong_explicit((p), (e), (d), (ms), (mf))
#define MINI_ATOMIC_EXCHANGE(p, v, m) std::atomic_exchange_explicit((p), (v), (m))
#define MINI_ATOMIC_RUNTIME_INIT(p, val) MINI_ATOMIC_STORE((p), (val), MINI_RELAXED)

/** @} */

#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L

/**
 * @defgroup compat_atomic_c11 纯 C11 原子操作
 * @brief 使用 stdatomic.h, 适用于支持 C11 标准的编译器
 * @{
 */

#include <stdatomic.h>

#define MINI_ATOMIC_INT _Atomic(int)
#define MINI_ATOMIC_UINT _Atomic(uint32_t)
#define MINI_ATOMIC_BOOL _Atomic(bool)
#define MINI_ATOMIC_INIT(val) ATOMIC_VAR_INIT(val)

#define MINI_RELAXED memory_order_relaxed
#define MINI_ACQUIRE memory_order_acquire
#define MINI_RELEASE memory_order_release
#define MINI_ACQ_REL memory_order_acq_rel
#define MINI_SEQ_CST memory_order_seq_cst

#define MINI_ATOMIC_STORE(p, v, m) atomic_store_explicit((p), (v), (m))
#define MINI_ATOMIC_LOAD(p, m) atomic_load_explicit((p), (m))
#define MINI_ATOMIC_ADD_FETCH(p, v, m) (atomic_fetch_add_explicit((p), (v), (m)) + (v))
#define MINI_ATOMIC_SUB_FETCH(p, v, m) (atomic_fetch_sub_explicit((p), (v), (m)) - (v))
#define MINI_ATOMIC_FETCH_ADD(p, v, m) atomic_fetch_add_explicit((p), (v), (m))
#define MINI_ATOMIC_FETCH_SUB(p, v, m) atomic_fetch_sub_explicit((p), (v), (m))
#define MINI_ATOMIC_CAS(p, e, d, ms, mf) atomic_compare_exchange_strong_explicit((p), (e), (d), (ms), (mf))
#define MINI_ATOMIC_EXCHANGE(p, v, m) atomic_exchange_explicit((p), (v), (m))
#define MINI_ATOMIC_RUNTIME_INIT(p, val) MINI_ATOMIC_STORE((p), (val), MINI_RELAXED)

/** @} */

#else

/**
 * @defgroup compat_atomic_fallback volatile 原子操作回退
 * @brief 不推荐在生产多线程环境使用, 仅用于编译跑通
 * @{
 */

#define MINI_ATOMIC_INT volatile int
#define MINI_ATOMIC_UINT volatile uint32_t
#define MINI_ATOMIC_BOOL volatile bool
#define MINI_ATOMIC_INIT(val) (val)

#define MINI_RELAXED 0
#define MINI_ACQUIRE 0
#define MINI_RELEASE 0
#define MINI_ACQ_REL 0
#define MINI_SEQ_CST 0

#define MINI_ATOMIC_STORE(p, v, m) (*(p) = (v))
#define MINI_ATOMIC_LOAD(p, m) (*(p))
#define MINI_ATOMIC_ADD_FETCH(p, v, m) (*(p) += (v), *(p))
#define MINI_ATOMIC_SUB_FETCH(p, v, m) (*(p) -= (v), *(p))
#define MINI_ATOMIC_FETCH_ADD(p, v, m)                                                                                                               \
    __extension__({                                                                                                                                  \
        __typeof__(*(p)) _o = *(p);                                                                                                                  \
        *(p) += (v);                                                                                                                                 \
        _o;                                                                                                                                          \
    })
#define MINI_ATOMIC_FETCH_SUB(p, v, m)                                                                                                               \
    __extension__({                                                                                                                                  \
        __typeof__(*(p)) _o = *(p);                                                                                                                  \
        *(p) -= (v);                                                                                                                                 \
        _o;                                                                                                                                          \
    })
#define MINI_ATOMIC_CAS(p, e, d, ms, mf) ((*(p) == *(e)) ? (*(p) = (d), 1) : (*(e) = *(p), 0))
#define MINI_ATOMIC_EXCHANGE(p, v, m)                                                                                                                \
    __extension__({                                                                                                                                  \
        __typeof__(*(p)) _o = *(p);                                                                                                                  \
        *(p) = (v);                                                                                                                                  \
        _o;                                                                                                                                          \
    })
#define MINI_ATOMIC_RUNTIME_INIT(p, val) MINI_ATOMIC_STORE((p), (val), MINI_RELAXED)

#endif

#endif /* COMPILER_COMPAT_H */
