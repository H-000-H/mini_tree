/* SPDX-License-Identifier: Apache-2.0 */
/*
 * System Command Dispatcher — 双后端 (OS / Bare-metal)
 *
 * Kconfig 自动选择后端:
 *   CONFIG_OSAL_NULL=y       → bare-metal: 纯数组 + const char* + 无锁
 *   CONFIG_OSAL_FREERTOS=y
 *   | CONFIG_OSAL_RTTHREAD=y → OS:  etl::map + etl::string + spinlock
 *
 * 公共 API 完全一致, 应用层代码不感知后端差异.
 */
#pragma once

#include <etl/string.h>
#include <etl/type_traits.h>
#include <etl/placement_new.h>
#include <etl/utility.h>
#include <etl/char_traits.h>
#include "osal.h"

/* 后端选择: bare-metal 不需要 ETL */
#ifndef CONFIG_OSAL_NULL
#include <etl/map.h>
#include <etl/string.h>
#endif
#ifndef SYS_CMD_MAX_NAME_LEN
#define SYS_CMD_MAX_NAME_LEN    16
#endif
#ifndef SYS_CMD_MAX_COUNT
#define SYS_CMD_MAX_COUNT       8
#endif
#ifndef SYS_CMD_WRAPPER_FN_SZ
#define SYS_CMD_WRAPPER_FN_SZ   40
#endif

/* ═══════════════════════════════════════════════════════════════════════
 * CmdFn — 固定容量内联函数包装器 (Small-Buffer-Optimized Callable Wrapper)
 *
 * 通用后端, 不依赖 ETL / OSAL, 两个后端共用.
 * ═══════════════════════════════════════════════════════════════════════ */
template <size_t StorageSz = SYS_CMD_WRAPPER_FN_SZ>
class CmdFn
{
    /** @brief 虚函数表 — 类型擦除后的操作入口 */
    struct Vtable
    {
        bool (*invoke)(void* self, const void* arg, size_t len, void* ctx);  /**< 调用包装的可调用对象 */
        void (*copy  )(void* dst, const void* src);                          /**< 拷贝构造 */
        void (*destroy)(void* self);                                         /**< 析构销毁 */
    };

    template <typename F>
    static bool invoke_fn(void* self, const void* arg, size_t len, void* ctx)
    {
        return (*static_cast<F*>(self))(arg, len, ctx);
    }

    template <typename F>
    static void copy_fn(void* dst, const void* src)
    {
        new (dst) F(*static_cast<const F*>(src));
    }

    template <typename F>
    static void destroy_fn(void* self)
    {
        static_cast<F*>(self)->~F();
    }

    const Vtable* m_vtable;   /**< 当前存储对象的虚表指针 (nullptr = 空) */
    union Storage { char data[StorageSz - sizeof(const Vtable*)]; max_align_t align_; } m_storage;  /**< SBO 内联存储区 */

    static_assert(StorageSz > sizeof(const Vtable*),
                  "StorageSz must be larger than pointer size");

public:
    CmdFn() : m_vtable(nullptr) {}

    template <typename F, typename = etl::enable_if_t<!etl::is_same_v<etl::decay_t<F>, CmdFn>>>
    CmdFn(F&& f)
        : m_vtable(&s_vtable<etl::decay_t<F>>)
    {
        static_assert(sizeof(etl::decay_t<F>) <= sizeof(m_storage.data),
                      "Callable object too large for CmdFn storage");
        static_assert(alignof(etl::decay_t<F>) <= alignof(decltype(m_storage.align_)),
                      "Callable object alignment exceeds CmdFn storage");
        new (m_storage.data) etl::decay_t<F>(etl::forward<F>(f));
    }

    CmdFn(const CmdFn& other) : m_vtable(other.m_vtable)
    {
        if (m_vtable) m_vtable->copy(m_storage.data, other.m_storage.data);
    }

    CmdFn& operator=(const CmdFn& other)
    {
        if (this != &other)
        {
            if (m_vtable) m_vtable->destroy(m_storage.data);
            m_vtable = other.m_vtable;
            if (m_vtable) m_vtable->copy(m_storage.data, other.m_storage.data);
        }
        return *this;
    }

    ~CmdFn()
    {
        if (m_vtable) m_vtable->destroy(m_storage.data);
    }

    bool operator()(const void* arg, size_t len, void* ctx) const
    {
        if (!m_vtable) return false;
        return m_vtable->invoke(const_cast<char*>(m_storage.data), arg, len, ctx);
    }

    explicit operator bool() const { return m_vtable != nullptr; }

private:
    template <typename F>
    static const Vtable s_vtable;
};

template <size_t StorageSz>
template <typename F>
const typename CmdFn<StorageSz>::Vtable CmdFn<StorageSz>::s_vtable = {
    &CmdFn<StorageSz>::template invoke_fn<F>,
    &CmdFn<StorageSz>::template copy_fn<F>,
    &CmdFn<StorageSz>::template destroy_fn<F>
};

/* ═══════════════════════════════════════════════════════════════════════
 * SystemCmd — 平台无关 API
 * ═══════════════════════════════════════════════════════════════════════ */
/**
 * @brief 系统命令分发器 — 单例模式, 双后端 (OS / Bare-metal)
 *
 * 支持注册/注销/分发命令, 带轻量级 RTTI 类型安全校验
 */
class SystemCmd
{
public:
    static constexpr size_t kMaxCmdNameLen = SYS_CMD_MAX_NAME_LEN;  /**< 命令名最大长度 (含 '\0') */
    static constexpr size_t kMaxCommands   = SYS_CMD_MAX_COUNT;     /**< 最大注册命令数 */

    using RawHandler = CmdFn<>;  /**< 类型擦除后的命令处理函数包装器 */

    // 轻量级 RTTI 替代方案的类型令牌
    using TypeIdToken = const void*;  /**< 类型标识令牌 (每个类型唯一地址) */
    template<typename T>
    static TypeIdToken getTypeId() {
        static const char type_marker = 0;
        return static_cast<TypeIdToken>(&type_marker);
    }

    /** @brief 命令处理节点 — 存储包装器及参数/上下文类型信息 */
    struct HandlerNode {
        RawHandler  wrapper;   /**< 类型擦除后的处理函数 */
        TypeIdToken args_id;   /**< 参数类型令牌 (用于分发时校验) */
        TypeIdToken ctx_id;    /**< 上下文类型令牌 (用于分发时校验) */
    };

    /* ── 后端无关的公共 API ── */
    static SystemCmd& getInstance();  /**< 获取单例引用 */

    /** @brief 注册命令: handler(const Args&, Ctx*) */
    template<typename Args, typename Ctx = void>
    bool registerCmd(const char* name, bool (*handler)(const Args&, Ctx*));

    /** @brief 注册命令: handler(const Args&, const Ctx*) */
    template<typename Args, typename Ctx>
    bool registerCmd(const char* name, bool (*handler)(const Args&, const Ctx*));

    /** @brief 注册命令: handler(Ctx*), 无参数 */
    template<typename Ctx = void>
    bool registerCmd(const char* name, bool (*handler)(Ctx*));

    /** @brief 注册命令: handler(), 无参数无上下文 */
    bool registerCmd(const char* name, bool (*handler)());

    bool unregisterCmd(const char* name);  /**< 注销已注册命令 */
    bool hasCmd(const char* name) const;   /**< 查询命令是否已注册 */
    size_t count() const;                  /**< 当前已注册命令数 */

    /** @brief 原始分发 (无类型校验, 内部使用) */
    bool dispatch(const char* name, const void* arg, size_t arg_len,
                  void* ctx = nullptr,
                  TypeIdToken expected_args_id = nullptr,
                  TypeIdToken expected_ctx_id = nullptr) const;

    /** @brief 类型安全分发 (带参数 + 上下文) */
    template<typename Args, typename Ctx = void>
    bool dispatchSecure(const char* name, const Args& arg, Ctx* ctx = nullptr) const {
        return dispatch(name, &arg, sizeof(Args), ctx,
                        getTypeId<Args>(), getTypeId<Ctx>());
    }

    /** @brief 类型安全分发 (无参数, 仅上下文) */
    template<typename Ctx = void>
    bool dispatchSecure(const char* name, Ctx* ctx = nullptr) const {
        return dispatch(name, nullptr, 0, ctx,
                        getTypeId<void>(), getTypeId<Ctx>());
    }

private:
    SystemCmd();                              /**< 私有构造 (单例) */
    ~SystemCmd() = default;
    SystemCmd(const SystemCmd&) = delete;     /**< 禁止拷贝 */
    SystemCmd& operator=(const SystemCmd&) = delete;  /**< 禁止赋值 */

    /* ════════════════════════════════════════════════════════════════════
     *  后端存储
     * ════════════════════════════════════════════════════════════════════ */
#ifndef CONFIG_OSAL_NULL
    /* ── OS 后端: etl::map + spinlock ── */
    using CmdString = etl::string<kMaxCmdNameLen>;  /**< 命令名存储类型 */
    using CmdMap    = etl::map<CmdString, HandlerNode, kMaxCommands>;  /**< 命令映射表 */

    CmdMap m_commands;          /**< 命令名 → 处理节点 映射 */
    mutable struct osal_spinlock* m_lock;  /**< 自旋锁指针 (保护并发访问) */
    uint8_t m_lock_storage[OSAL_SPINLOCK_STORAGE_SIZE] COMPAT_ALIGNED(4);  /**< 自旋锁存储区 */
#else
    /* ── Bare-metal 后端: 普通数组 + const char* + 无锁 ── */
    /** @brief 命令条目 (bare-metal 后端) */
    struct CmdEntry {
        const char* name;        /**< 命令名 (必须指向静态字符串) */
        HandlerNode node;        /**< 处理节点 */
    };
    CmdEntry  m_entries[kMaxCommands];  /**< 命令数组 */
    size_t m_count;                     /**< 当前已注册命令数 */
#endif
};

/* ═══════════════════════════════════════════════════════════════════════
 *  registerCmd 模板实现 — 双后端条件分支
 * ═══════════════════════════════════════════════════════════════════════ */

template<typename Args, typename Ctx>
inline bool SystemCmd::registerCmd(const char* name, bool (*handler)(const Args&, Ctx*))
{
    if (!name || !handler) return false;
    const size_t name_len = etl::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    static_assert(etl::is_trivially_copyable_v<Args>, "Args must be trivially copyable");
    static_assert(etl::is_default_constructible_v<Args>, "Args must be default constructible");

    HandlerNode node;
    node.args_id = getTypeId<Args>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void* raw_arg, size_t len, void* raw_ctx) -> bool {
        if (!raw_arg || len < sizeof(Args)) return false;
        if constexpr (!etl::is_same_v<Ctx, void>) {
            if (raw_ctx == nullptr) return false;
        }
        Args typed_arg;
        COMPAT_MEM_COPY(&typed_arg, raw_arg, sizeof(Args));
        auto* ctx = static_cast<Ctx*>(raw_ctx);
        return handler(typed_arg, ctx);
    };

#ifndef CONFIG_OSAL_NULL
    CmdString cmd(name);
    osal_spinlock_lock(m_lock);
    if (m_commands.full() || m_commands.contains(cmd)) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    bool success = m_commands.insert(etl::make_pair(cmd, node)).second;
    osal_spinlock_unlock(m_lock);
    return success;
#else
    for (size_t i = 0; i < m_count; i++) {
        if (strcmp(m_entries[i].name, name) == 0)
            return false;
    }
    if (m_count >= kMaxCommands)
        return false;
    m_entries[m_count].name = name;
    m_entries[m_count].node = node;
    m_count++;
    return true;
#endif
}

template<typename Args, typename Ctx>
inline bool SystemCmd::registerCmd(const char* name, bool (*handler)(const Args&, const Ctx*))
{
    if (!name || !handler) return false;
    const size_t name_len = etl::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    static_assert(etl::is_trivially_copyable_v<Args>, "Args must be trivially copyable");
    static_assert(etl::is_default_constructible_v<Args>, "Args must be default constructible");

    HandlerNode node;
    node.args_id = getTypeId<Args>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void* raw_arg, size_t len, void* raw_ctx) -> bool {
        if (!raw_arg || len < sizeof(Args)) return false;
        if (raw_ctx == nullptr) return false;
        Args typed_arg;
        COMPAT_MEM_COPY(&typed_arg, raw_arg, sizeof(Args));
        const auto* ctx = static_cast<const Ctx*>(raw_ctx);
        return handler(typed_arg, ctx);
    };

#ifndef CONFIG_OSAL_NULL
    CmdString cmd(name);
    osal_spinlock_lock(m_lock);
    if (m_commands.full() || m_commands.contains(cmd)) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    bool success = m_commands.insert(etl::make_pair(cmd, node)).second;
    osal_spinlock_unlock(m_lock);
    return success;
#else
    for (size_t i = 0; i < m_count; i++) {
        if (strcmp(m_entries[i].name, name) == 0)
            return false;
    }
    if (m_count >= kMaxCommands)
        return false;
    m_entries[m_count].name = name;
    m_entries[m_count].node = node;
    m_count++;
    return true;
#endif
}

template<typename Ctx>
inline bool SystemCmd::registerCmd(const char* name, bool (*handler)(Ctx*))
{
    if (!name || !handler) return false;
    const size_t name_len = etl::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    HandlerNode node;
    node.args_id = getTypeId<void>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void*, size_t, void* raw_ctx) -> bool {
        if constexpr (!etl::is_same_v<Ctx, void>) {
            if (raw_ctx == nullptr) return false;
        }
        auto* ctx = static_cast<Ctx*>(raw_ctx);
        return handler(ctx);
    };

#ifndef CONFIG_OSAL_NULL
    CmdString cmd(name);
    osal_spinlock_lock(m_lock);
    if (m_commands.full() || m_commands.contains(cmd)) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    bool success = m_commands.insert(etl::make_pair(cmd, node)).second;
    osal_spinlock_unlock(m_lock);
    return success;
#else
    for (size_t i = 0; i < m_count; i++) {
        if (strcmp(m_entries[i].name, name) == 0)
            return false;
    }
    if (m_count >= kMaxCommands)
        return false;
    m_entries[m_count].name = name;
    m_entries[m_count].node = node;
    m_count++;
    return true;
#endif
}
