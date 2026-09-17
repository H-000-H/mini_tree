/* SPDX-License-Identifier: Apache-2.0 */
/*
 * SystemCmd 实现 — 单份实现, 命令表访问统一走 mini_critical 临界区
 */
#include "system_cmd.hpp"

#include <string.h>

/* -------------------------------------------------------------------------- */
/* 构造函数                                                                    */
/* -------------------------------------------------------------------------- */
SystemCmd::SystemCmd() : m_count(0)
{
}

/* -------------------------------------------------------------------------- */
/* Singleton                                                                   */
/* -------------------------------------------------------------------------- */
SystemCmd& SystemCmd::get_instance()
{
    static SystemCmd instance;
    return instance;
}

/* -------------------------------------------------------------------------- */
/* 唯一的注册实现 — register_cmd 模板收敛到这里                                 */
/* -------------------------------------------------------------------------- */
int SystemCmd::register_raw(const char* name, const RawHandler& wrapper, TypeIdToken args_id,
                            TypeIdToken ctx_id)
{
    if (name == nullptr)
        return MINI_ERR_INVAL;
    const size_t name_len = etl::strlen(name);
    if (name_len >= k_max_cmd_name_len)
        return MINI_ERR_INVAL;

    mini_irq_state_t irq = mini_critical_enter();

    if (m_count >= k_max_commands)
    {
        mini_critical_exit(irq);
        return MINI_ERR_NOSPC;
    }
    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            mini_critical_exit(irq);
            return MINI_ERR_BUSY;
        }
    }

    MINI_MEM_COPY(m_entries[m_count].name, name, name_len + 1);
    m_entries[m_count].node.wrapper = wrapper;
    m_entries[m_count].node.args_id = args_id;
    m_entries[m_count].node.ctx_id  = ctx_id;
    m_count++;

    mini_critical_exit(irq);
    return MINI_OK;
}

/* -------------------------------------------------------------------------- */
/* 注销命令                                                                    */
/* -------------------------------------------------------------------------- */
int SystemCmd::unregister_cmd(const char* name)
{
    if (name == nullptr)
        return MINI_ERR_INVAL;

    mini_irq_state_t irq = mini_critical_enter();

    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            m_entries[i] = m_entries[m_count - 1]; /* 末项填补, 命令顺序无语义 */
            m_count--;
            mini_critical_exit(irq);
            return MINI_OK;
        }
    }

    mini_critical_exit(irq);
    return MINI_ERR_NODEV;
}

/* -------------------------------------------------------------------------- */
/* 命令分发                                                                    */
/* -------------------------------------------------------------------------- */
int SystemCmd::dispatch(const char* name, const void* arg, size_t arg_len, void* ctx,
                        TypeIdToken expected_args_id, TypeIdToken expected_ctx_id) const
{
    if (name == nullptr)
        return MINI_ERR_INVAL;

    bool        found = false;
    HandlerNode node;

    mini_irq_state_t irq = mini_critical_enter();
    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            node  = m_entries[i].node; /* 拷贝后再退临界区: 回调里可能改表 */
            found = true;
            break;
        }
    }
    mini_critical_exit(irq);

    if (!found)
        return MINI_ERR_NODEV;
    if ((expected_args_id != nullptr) && (node.args_id != expected_args_id))
        return MINI_ERR_NOTSUPP;
    if ((expected_ctx_id != nullptr) && (node.ctx_id != expected_ctx_id))
        return MINI_ERR_NOTSUPP;

    return node.wrapper(arg, arg_len, ctx) ? MINI_OK : MINI_ERR_INVAL;
}

/* -------------------------------------------------------------------------- */
/* 查询 / 计数                                                                 */
/* -------------------------------------------------------------------------- */
bool SystemCmd::has_cmd(const char* name) const
{
    if (name == nullptr)
        return false;

    bool found = false;

    mini_irq_state_t irq = mini_critical_enter();
    for (size_t i = 0; i < m_count; i++)
    {
        if (strcmp(m_entries[i].name, name) == 0)
        {
            found = true;
            break;
        }
    }
    mini_critical_exit(irq);

    return found;
}

size_t SystemCmd::count() const
{
    mini_irq_state_t irq = mini_critical_enter();
    const size_t      sz = m_count;
    mini_critical_exit(irq);
    return sz;
}
