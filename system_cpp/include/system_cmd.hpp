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

#include <cstring>
#include <cstddef>
#include <type_traits>
#include <new>
#include "osal.h"

/* 后端选择: bare-metal 不需要 ETL */
#ifndef CONFIG_OSAL_NULL
#include <etl/map.h>
#include <etl/string.h>
#include <etl/type_traits.h>
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
    struct Vtable
    {
        bool (*invoke)(void* self, const void* arg, size_t len, void* ctx);
        void (*copy  )(void* dst, const void* src);
        void (*destroy)(void* self);
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

    const Vtable* m_vtable;
    union Storage { char data[StorageSz - sizeof(const Vtable*)]; max_align_t align_; } m_storage;

    static_assert(StorageSz > sizeof(const Vtable*),
                  "StorageSz must be larger than pointer size");

public:
    CmdFn() : m_vtable(nullptr) {}

    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, CmdFn>>>
    CmdFn(F&& f)
        : m_vtable(&s_vtable<std::decay_t<F>>)
    {
        static_assert(sizeof(std::decay_t<F>) <= sizeof(m_storage.data),
                      "Callable object too large for CmdFn storage");
        static_assert(alignof(std::decay_t<F>) <= alignof(decltype(m_storage.align_)),
                      "Callable object alignment exceeds CmdFn storage");
        new (m_storage.data) std::decay_t<F>(std::forward<F>(f));
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
class SystemCmd
{
public:
    static constexpr std::size_t kMaxCmdNameLen = SYS_CMD_MAX_NAME_LEN;
    static constexpr std::size_t kMaxCommands   = SYS_CMD_MAX_COUNT;

    using RawHandler = CmdFn<>;

    // 轻量级 RTTI 替代方案的类型令牌
    using TypeIdToken = const void*;
    template<typename T>
    static TypeIdToken getTypeId() {
        static const char type_marker = 0;
        return static_cast<TypeIdToken>(&type_marker);
    }

    struct HandlerNode {
        RawHandler  wrapper;
        TypeIdToken args_id;
        TypeIdToken ctx_id;
    };

    /* ── 后端无关的公共 API ── */
    static SystemCmd& getInstance();

    template<typename Args, typename Ctx = void>
    bool registerCmd(const char* name, bool (*handler)(const Args&, Ctx*));

    template<typename Args, typename Ctx>
    bool registerCmd(const char* name, bool (*handler)(const Args&, const Ctx*));

    template<typename Ctx = void>
    bool registerCmd(const char* name, bool (*handler)(Ctx*));

    bool registerCmd(const char* name, bool (*handler)());

    bool unregisterCmd(const char* name);
    bool hasCmd(const char* name) const;
    std::size_t count() const;

    bool dispatch(const char* name, const void* arg, std::size_t arg_len,
                  void* ctx = nullptr,
                  TypeIdToken expected_args_id = nullptr,
                  TypeIdToken expected_ctx_id = nullptr) const;

    template<typename Args, typename Ctx = void>
    bool dispatchSecure(const char* name, const Args& arg, Ctx* ctx = nullptr) const {
        return dispatch(name, &arg, sizeof(Args), ctx,
                        getTypeId<Args>(), getTypeId<Ctx>());
    }

    template<typename Ctx = void>
    bool dispatchSecure(const char* name, Ctx* ctx = nullptr) const {
        return dispatch(name, nullptr, 0, ctx,
                        getTypeId<void>(), getTypeId<Ctx>());
    }

private:
    SystemCmd();
    ~SystemCmd() = default;
    SystemCmd(const SystemCmd&) = delete;
    SystemCmd& operator=(const SystemCmd&) = delete;

    /* ════════════════════════════════════════════════════════════════════
     *  后端存储
     * ════════════════════════════════════════════════════════════════════ */
#ifndef CONFIG_OSAL_NULL
    /* ── OS 后端: etl::map + spinlock ── */
    using CmdString = etl::string<kMaxCmdNameLen>;
    using CmdMap    = etl::map<CmdString, HandlerNode, kMaxCommands>;

    CmdMap m_commands;
    mutable struct osal_spinlock* m_lock;
    uint8_t m_lock_storage[OSAL_SPINLOCK_STORAGE_SIZE];
#else
    /* ── Bare-metal 后端: 普通数组 + const char* + 无锁 ── */
    struct CmdEntry {
        const char* name;        // 命令名 (必须指向静态字符串)
        HandlerNode node;
    };
    CmdEntry  m_entries[kMaxCommands];
    std::size_t m_count;
#endif
};

/* ═══════════════════════════════════════════════════════════════════════
 *  registerCmd 模板实现 — 双后端条件分支
 * ═══════════════════════════════════════════════════════════════════════ */

template<typename Args, typename Ctx>
inline bool SystemCmd::registerCmd(const char* name, bool (*handler)(const Args&, Ctx*))
{
    if (!name || !handler) return false;
    const std::size_t name_len = std::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    static_assert(std::is_trivially_copyable_v<Args>, "Args must be trivially copyable");
    static_assert(std::is_default_constructible_v<Args>, "Args must be default constructible");

    HandlerNode node;
    node.args_id = getTypeId<Args>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void* raw_arg, std::size_t len, void* raw_ctx) -> bool {
        if (!raw_arg || len < sizeof(Args)) return false;
        if constexpr (!std::is_same_v<Ctx, void>) {
            if (raw_ctx == nullptr) return false;
        }
        Args typed_arg;
        std::memcpy(&typed_arg, raw_arg, sizeof(Args));
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
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0)
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
    const std::size_t name_len = std::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    static_assert(std::is_trivially_copyable_v<Args>, "Args must be trivially copyable");
    static_assert(std::is_default_constructible_v<Args>, "Args must be default constructible");

    HandlerNode node;
    node.args_id = getTypeId<Args>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void* raw_arg, std::size_t len, void* raw_ctx) -> bool {
        if (!raw_arg || len < sizeof(Args)) return false;
        if (raw_ctx == nullptr) return false;
        Args typed_arg;
        std::memcpy(&typed_arg, raw_arg, sizeof(Args));
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
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0)
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
    const std::size_t name_len = std::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    HandlerNode node;
    node.args_id = getTypeId<void>();
    node.ctx_id  = getTypeId<Ctx>();
    node.wrapper = [handler](const void*, std::size_t, void* raw_ctx) -> bool {
        if constexpr (!std::is_same_v<Ctx, void>) {
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
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0)
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
