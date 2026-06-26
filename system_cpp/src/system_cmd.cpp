/*
 * System Command Dispatcher — 实现 (OS / Bare-metal 双后端)
 */

#include "system_cmd.hpp"

#include <cstring>

/* ═══════════════════════════════════════════════════════════════════════════
 *  构造函数
 * ═══════════════════════════════════════════════════════════════════════════ */
SystemCmd::SystemCmd()
#ifndef CONFIG_OSAL_NULL
{
    m_lock = reinterpret_cast<struct osal_spinlock*>(m_lock_storage);
    osal_spinlock_init(m_lock);
}
#else
    : m_count(0)
{
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  Singleton
 * ═══════════════════════════════════════════════════════════════════════════ */
SystemCmd& SystemCmd::getInstance()
{
    static SystemCmd instance;
    return instance;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  registerCmd — 无参数版
 * ═══════════════════════════════════════════════════════════════════════════ */
bool SystemCmd::registerCmd(const char* name, bool (*handler)())
{
    if (!name || !handler) return false;
    const std::size_t name_len = std::strlen(name);
    if (name_len >= kMaxCmdNameLen) return false;

    HandlerNode node;
    node.args_id = getTypeId<void>();
    node.ctx_id  = getTypeId<void>();
    node.wrapper = [handler](const void*, std::size_t, void*) -> bool {
        return handler();
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

/* ═══════════════════════════════════════════════════════════════════════════
 *  注销命令
 * ═══════════════════════════════════════════════════════════════════════════ */
bool SystemCmd::unregisterCmd(const char* name)
{
    if (!name) return false;

#ifndef CONFIG_OSAL_NULL
    osal_spinlock_lock(m_lock);
    CmdString key(name);
    auto it = m_commands.find(key);
    if (it == m_commands.end()) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    m_commands.erase(it);
    osal_spinlock_unlock(m_lock);
    return true;
#else
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0) {
            m_entries[i] = m_entries[m_count - 1];
            m_count--;
            return true;
        }
    }
    return false;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  命令分发
 * ═══════════════════════════════════════════════════════════════════════════ */
bool SystemCmd::dispatch(const char* name, const void* arg, std::size_t arg_len,
                          void* ctx, TypeIdToken expected_args_id,
                          TypeIdToken expected_ctx_id) const
{
    if (!name) return false;

#ifndef CONFIG_OSAL_NULL
    osal_spinlock_lock(m_lock);
    CmdString key(name);
    auto it = m_commands.find(key);
    if (it == m_commands.end()) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    const HandlerNode& node = it->second;
    if (expected_args_id && node.args_id != expected_args_id) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    if (expected_ctx_id && node.ctx_id != expected_ctx_id) {
        osal_spinlock_unlock(m_lock);
        return false;
    }
    osal_spinlock_unlock(m_lock);
    return node.wrapper(arg, arg_len, ctx);
#else
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0) {
            const HandlerNode& node = m_entries[i].node;
            if (expected_args_id && node.args_id != expected_args_id)
                return false;
            if (expected_ctx_id && node.ctx_id != expected_ctx_id)
                return false;
            return node.wrapper(arg, arg_len, ctx);
        }
    }
    return false;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  查询 / 计数
 * ═══════════════════════════════════════════════════════════════════════════ */
bool SystemCmd::hasCmd(const char* name) const
{
    if (!name) return false;

#ifndef CONFIG_OSAL_NULL
    osal_spinlock_lock(m_lock);
    CmdString key(name);
    bool found = m_commands.find(key) != m_commands.end();
    osal_spinlock_unlock(m_lock);
    return found;
#else
    for (std::size_t i = 0; i < m_count; i++) {
        if (std::strcmp(m_entries[i].name, name) == 0)
            return true;
    }
    return false;
#endif
}

std::size_t SystemCmd::count() const
{
#ifndef CONFIG_OSAL_NULL
    osal_spinlock_lock(m_lock);
    std::size_t sz = m_commands.size();
    osal_spinlock_unlock(m_lock);
    return sz;
#else
    return m_count;
#endif
}
