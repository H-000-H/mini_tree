/* SPDX-License-Identifier: Apache-2.0 */
/*
 * System Command Dispatcher — 实现 (OS / Bare-metal 双后端)
 */

#include "system_cmd.hpp"

#include "compiler_compat.h"
#include <etl/char_traits.h>
#include <etl/string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 *  构造函数
 * ═══════════════════════════════════════════════════════════════════════════ */
SystemCmd::SystemCmd()
#ifndef CONFIG_OSAL_NULL
{
    m_lock = reinterpret_cast<struct osal_spinlock*>(m_lock_storage);
    COMPAT_IGNORE_RESULT(osal_spinlock_init(m_lock));
}
#else
    : m_count(0)
{
}
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  Singleton
 * ═══════════════════════════════════════════════════════════════════════════ */
SystemCmd& SystemCmd::get_instance()
{
    static SystemCmd instance;
    return instance;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  register_cmd — 无参数版
 * ═══════════════════════════════════════════════════════════════════════════ */
int SystemCmd::register_cmd(const char* name, bool (*handler)())
{
    if (!name || !handler)
        return MINI_ERR_INVAL;
    const size_t name_len = etl::strlen(name);
    if (name_len >= k_max_cmd_name_len)
        return MINI_ERR_INVAL;

    HandlerNode node;
    node.args_id = get_type_id<void>();
    node.ctx_id = get_type_id<void>();
    node.wrapper = [handler](const void*, size_t, void*) -> bool { return handler(); };

#ifndef CONFIG_OSAL_NULL
    CmdString cmd(name);
    COMPAT_IGNORE_RESULT(osal_spinlock_lock(m_lock));
    if (m_commands.full())
    {
        COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
        return MINI_ERR_NOSPC;
    }
    if (m_commands.contains(cmd))
    {
        COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
        return MINI_ERR_BUSY;
    }
    bool success = m_commands.insert(etl::make_pair(cmd, node)).second;
    COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
    return success ? MINI_OK : MINI_ERR_NOMEM;
#else
    for (size_t i = 0; i < m_count; i++)
        if (strcmp(m_entries[i].name, name) == 0)
            return MINI_ERR_BUSY;
    if (m_count >= k_max_commands)
        return MINI_ERR_NOSPC;
    m_entries[m_count].name = name;
    m_entries[m_count].node = node;
    m_count++;
    return MINI_OK;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  注销命令
 * ═══════════════════════════════════════════════════════════════════════════ */
int SystemCmd::unregister_cmd(const char* name)
{
    if (!name)
        return MINI_ERR_INVAL;

#ifndef CONFIG_OSAL_NULL
    COMPAT_IGNORE_RESULT(osal_spinlock_lock(m_lock));
    CmdString key(name);
    auto it = m_commands.find(key);
    if (it == m_commands.end())
    {
        COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
        return MINI_ERR_NODEV;
    }
    m_commands.erase(it);
    COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
    return MINI_OK;
#else
    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            m_entries[i] = m_entries[m_count - 1];
            m_count--;
            return MINI_OK;
        }
    }
    return MINI_ERR_NODEV;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  命令分发
 * ═══════════════════════════════════════════════════════════════════════════ */
int SystemCmd::dispatch(const char* name, const void* arg, size_t arg_len, void* ctx,
                        TypeIdToken expected_args_id, TypeIdToken expected_ctx_id) const
{
    if (!name)
        return MINI_ERR_INVAL;

#ifndef CONFIG_OSAL_NULL
    COMPAT_IGNORE_RESULT(osal_spinlock_lock(m_lock));
    CmdString key(name);
    auto it = m_commands.find(key);
    if (it == m_commands.end())
    {
        COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
        return MINI_ERR_NODEV;
    }
    /* 拷贝 HandlerNode 后再解锁, 避免 erase 导致悬垂引用 */
    HandlerNode node = it->second;
    COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));

    if (expected_args_id && node.args_id != expected_args_id)
        return MINI_ERR_NOTSUPP;
    if (expected_ctx_id && node.ctx_id != expected_ctx_id)
        return MINI_ERR_NOTSUPP;
    return node.wrapper(arg, arg_len, ctx) ? MINI_OK : MINI_ERR_INVAL;
#else
    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            const HandlerNode& node = m_entries[i].node;
            if (expected_args_id && node.args_id != expected_args_id)
                return MINI_ERR_NOTSUPP;
            if (expected_ctx_id && node.ctx_id != expected_ctx_id)
                return MINI_ERR_NOTSUPP;
            return node.wrapper(arg, arg_len, ctx) ? MINI_OK : MINI_ERR_INVAL;
        }
    }
    return MINI_ERR_NODEV;
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  查询 / 计数
 * ═══════════════════════════════════════════════════════════════════════════ */
bool SystemCmd::has_cmd(const char* name) const
{
    if (!name)
        return false;

#ifndef CONFIG_OSAL_NULL
    COMPAT_IGNORE_RESULT(osal_spinlock_lock(m_lock));
    CmdString key(name);
    bool found = m_commands.find(key) != m_commands.end();
    COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
    return found;
#else
    for (size_t i = 0; i < m_count; i++)
        if (strcmp(m_entries[i].name, name) == 0)
            return true;
    return false;
#endif
}

size_t SystemCmd::count() const
{
#ifndef CONFIG_OSAL_NULL
    COMPAT_IGNORE_RESULT(osal_spinlock_lock(m_lock));
    size_t sz = m_commands.size();
    COMPAT_IGNORE_RESULT(osal_spinlock_unlock(m_lock));
    return sz;
#else
    return m_count;
#endif
}
