/* SPDX-License-Identifier: Apache-2.0 */
/*
 * board_device.c — 板级设备模型运行时实现
 *
 * 维护 device 实例表与递归互斥锁池 (device_tree_init 静态分配, 池水位线预警).
 * 实现设备查找、属性解析 (safe_parse_int32 替代 strtol).
 * VFS 转发层在 pdev->lock 保护下完成 check-then-act; device_ops_unregister
 * 持锁斩断 ops 防 TOCTOU 竞态.
 */
#include "board_config.h"
#include "board_devtable.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "event_bus.h"
#include "hal_amp.h"
#include "osal.h"
#include "safe_state.h"
#include "status.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler_compat_poison.h"

/* 编译期断言: 互斥锁池必须能覆盖最大设备数 */
_Static_assert(OSAL_MUTEX_POOL_SIZE >= DEV_ID_COUNT,
               "OSAL_MUTEX_POOL_SIZE too small for DEV_ID_COUNT devices");

/* ── 运行时设备实例表 ── */
static struct device s_devices[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_device_lock_storage[DEV_ID_COUNT][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4);

/**
 * @brief 判断设备状态机是否允许 from→to 迁移
 * @param from 当前状态
 * @param to 目标状态
 * @return 1 允许, 0 禁止
 */
static int device_status_can_transit(enum device_status from, enum device_status to)
{
    if (from == to)
        return 1;

    switch (from)
    {
    case DEVICE_STATUS_DISABLED:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_UNINIT;
    case DEVICE_STATUS_UNINIT:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_ERROR ||
               to == DEVICE_STATUS_DISABLED;
    case DEVICE_STATUS_READY:
        return to == DEVICE_STATUS_PROBED || to == DEVICE_STATUS_DISABLED ||
               to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_PROBED:
        return to == DEVICE_STATUS_RUNNING || to == DEVICE_STATUS_SUSPENDED ||
               to == DEVICE_STATUS_READY || to == DEVICE_STATUS_REMOVED ||
               to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_RUNNING:
        return to == DEVICE_STATUS_SUSPENDED || to == DEVICE_STATUS_READY ||
               to == DEVICE_STATUS_REMOVED || to == DEVICE_STATUS_ERROR ||
               to == DEVICE_STATUS_PROBED;
    case DEVICE_STATUS_SUSPENDED:
        return to == DEVICE_STATUS_RUNNING || to == DEVICE_STATUS_READY ||
               to == DEVICE_STATUS_REMOVED || to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_ERROR:
        return to == DEVICE_STATUS_REMOVED;
    case DEVICE_STATUS_REMOVED:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_DISABLED;
    default:
        return 0;
    }
}

/**
 * @brief 初始化设备树运行时实例表 (device/lock/lifecycle)
 * @return 有设备返回 VFS_OK, 无设备返回 VFS_ERR_IO
 */
int device_tree_init(void)
{
    for (int i = 0; i < DEV_ID_COUNT; i++)
    {
        const struct device_node* node = board_node_get((device_id_t)i);
        s_devices[i].node = node;
        s_devices[i].status = node ? node->status : DEVICE_STATUS_DISABLED;
        s_devices[i].priv_data = NULL;
        s_devices[i].ops = NULL;
        s_devices[i].lock = NULL;
        s_devices[i].platform_data = NULL;
        dev_lc_reset(&s_devices[i].lc);

        if (node && s_devices[i].status != DEVICE_STATUS_DISABLED &&
            !(node->flags & DEVICE_FLAG_DIRECT))
        {
            /* pdev->lock 需要递归: osal_mutex_create_static_recursive */
            struct osal_mutex* lock = NULL;
            if (osal_mutex_create_static_recursive(&lock, s_device_lock_storage[i],
                                                   sizeof(s_device_lock_storage[i])) == OSAL_OK)
            {
                s_devices[i].lock = lock;
                device_lc_bind(&s_devices[i]);
            }
            else
            {
#ifdef CONFIG_SYSTEM
                enter_safe_state("device_tree_init: mutex create failed");
#else
                /* System 模块关闭时无 safe_state 实现：退化为忙循环停机 */
                /* No safe_state when System is off: busy-loop halt */
                while (1)
                {
                }
#endif
            }
        }
    }

    /* 池水位线预警 */
    if (board_dev_count() >= OSAL_MUTEX_POOL_SIZE * 9 / 10)
    {
        osal_log(OSAL_LOG_WARN, "board", "device_tree_init: mutex pool >90%% used (%d/%d)\n",
                 board_dev_count(), OSAL_MUTEX_POOL_SIZE);
    }

    return board_dev_count() > 0 ? VFS_OK : VFS_ERR_IO;
}

/* ── 运行时设备实例访问 ── */
/**
 * @brief 按 device_id 获取运行时 device 实例
 * @param id 设备 ID
 * @return 成功返回 device 指针, 非法 id 返回 ERR_PTR
 */
struct device* board_dev_get(device_id_t id)
{
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    return &s_devices[id];
}

/**
 * @brief 按设备名查找 device
 * @param name 设备名字符串
 * @return 成功返回 device 指针, 未找到返回 ERR_PTR
 */
struct device* device_find(const char* name)
{
    device_id_t id;

    if (!name)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    id = board_dev_find(name);
    if ((int)id < 0)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get(id);
}

/**
 * @brief 按 label 查找 device
 * @param label 设备 label 字符串
 * @return 成功返回 device 指针, 未找到返回 ERR_PTR
 */
struct device* device_find_by_label(const char* label)
{
    device_id_t id;

    if (!label)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    id = board_dev_find_by_label(label);
    if ((int)id < 0)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get(id);
}

/**
 * @brief 解析 phandle 属性并返回关联 device
 * @param pdev 当前 device 指针
 * @param key phandle 属性键名
 * @return 成功返回关联 device 指针, 失败返回 ERR_PTR
 */
struct device* device_get_phandle_dev(const struct device* pdev, const char* key)
{
    const char* val;

    if (!pdev || !key)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    if (device_get_prop_str(pdev, key, &val) != VFS_OK)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    /* dtc-lite 将 phandle 引用存为 label 名字符串 */
    return device_find_by_label(val);
}

/**
 * @brief 按 device_id 查找 device (board_dev_get 别名)
 * @param id 设备 ID
 * @return device 指针或 ERR_PTR
 */
struct device* device_find_by_id(device_id_t id) { return board_dev_get(id); }

/**
 * @brief 按设备树路径查找 device
 * @param path 设备树路径字符串
 * @return 成功返回 device 指针, 未找到返回 ERR_PTR
 */
struct device* device_find_by_path(const char* path)
{
    if (!path)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    for (int i = 0; i < DEV_ID_COUNT; i++)
    {
        const struct device_node* node = board_node_get((device_id_t)i);
        if (node && node->path && strcmp(node->path, path) == 0)
            return board_dev_get((device_id_t)i);
    }
    return (struct device*)ERR_PTR(VFS_ERR_NODEV);
}

/**
 * @brief 按 compatible 字符串查找 device
 * @param compatible compatible 字符串
 * @return 成功返回 device 指针, 未找到返回 ERR_PTR
 */
struct device* device_find_by_compatible(const char* compatible)
{
    device_id_t id;

    if (!compatible)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    id = board_dev_find_by_compat(compatible);
    if ((int)id < 0)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get(id);
}

/**
 * @brief 获取 device 的父 device (deps[0])
 * @param pdev 当前 device 指针
 * @return 成功返回父 device 指针, 无父节点返回 ERR_PTR
 */
struct device* device_get_parent(const struct device* pdev)
{
    const struct device_node* node;

    if (!pdev || !pdev->node)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    node = pdev->node;
    if (node->dep_count <= 0 || !node->deps)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get(node->deps[0]);
}

/**
 * @brief MISRA C 2012 Rule 21.6 合规替代 strtol 的 int32 解析
 * @param str 待解析字符串 (支持 dec/hex/oct 前缀, 空格为自然终止符)
 * @param out 输出解析结果
 * @return 0 成功, -1 非法字符或溢出
 * @note 无 errno 依赖, 线程安全
 */
static int safe_parse_int32(const char* str, int* out)
{
    if (!str || !*str || !out)
        return -1;

    int sign = 1;
    const char* p = str;
    if (*p == '-')
    {
        sign = -1;
        p++;
    }
    else if (*p == '+')
    {
        p++;
    }

    int base = 10;
    if (*p == '0')
    {
        p++;
        if (*p == 'x' || *p == 'X')
        {
            base = 16;
            p++;
        }
        else if (*p == '\0')
        {
            *out = 0;
            return 0;
        }
        else
        {
            base = 8;
        }
    }

    if (!*p)
        return -1;

    uint32_t val = 0;
    const uint32_t limit = (sign > 0) ? (uint32_t)INT32_MAX : (uint32_t)INT32_MAX + 1UL;

    while (*p)
    {
        /* 空格作为自然终止符 — 兼容 multi-int 属性串如 "1073758208 1024" */
        if (*p == ' ')
            break;

        uint32_t digit;
        if (*p >= '0' && *p <= '9')
            digit = (uint32_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f')
            digit = (uint32_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F')
            digit = (uint32_t)(*p - 'A' + 10);
        else
            return -1;

        if (digit >= (uint32_t)base)
            return -1;

        if (val > (limit - digit) / (uint32_t)base)
            return -1;
        val = val * (uint32_t)base + digit;
        p++;
    }

    *out = (sign > 0) ? (int)val : -(int)val;
    return 0;
}

/* ── 属性读取（通过 pdev->node） ── */
/**
 * @brief 读取整型设备树属性
 * @param pdev device 指针
 * @param key 属性键名
 * @param val 输出整型值
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_get_prop_int(const struct device* pdev, const char* key, int* val)
{
    if (!pdev || !pdev->node || !key || !val)
        return VFS_ERR_INVAL;
    const struct device_node* node = pdev->node;
    for (int i = 0; i < node->prop_count; i++)
    {
        if (strcmp(node->props[i].key, key) == 0)
        {
            if (safe_parse_int32(node->props[i].value, val) != 0)
                return VFS_ERR_INVAL;
            return VFS_OK;
        }
    }
    return VFS_ERR_INVAL;
}

/**
 * @brief 读取整型数组设备树属性 (空格分隔)
 * @param pdev device 指针
 * @param key 属性键名
 * @param out_arr 输出整型数组
 * @param max_len 数组最大容量
 * @return 成功返回解析元素个数, 失败返回负数错误码
 */
int device_get_prop_int_array(const struct device* pdev, const char* key, int* out_arr, int max_len)
{
    if (!pdev || !pdev->node || !key || !out_arr || max_len <= 0)
        return VFS_ERR_INVAL;

    const char* value = NULL;
    for (int i = 0; i < pdev->node->prop_count; i++)
    {
        if (strcmp(pdev->node->props[i].key, key) == 0)
        {
            value = pdev->node->props[i].value;
            break;
        }
    }
    if (!value)
        return VFS_ERR_INVAL;

    /* 解析空格分隔的整数串 */
    int count = 0;
    const char* p = value;
    while (*p && count < max_len)
    {
        while (*p == ' ')
            p++;
        if (!*p)
            break;

        /* 计算当前 token 长度 */
        const char* start = p;
        while (*p && *p != ' ')
            p++;

        /* 复制 token 到临时缓冲区 */
        char token[64];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(token))
            return VFS_ERR_INVAL;
        __builtin_memcpy(token, start, len);
        token[len] = '\0';

        if (safe_parse_int32(token, &out_arr[count]) != 0)
            return VFS_ERR_INVAL;
        count++;
    }

    return count;
}

/**
 * @brief 读取字符串设备树属性
 * @param pdev device 指针
 * @param key 属性键名
 * @param val 输出字符串指针 (指向 node 内存储)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_get_prop_str(const struct device* pdev, const char* key, const char** val)
{
    if (!pdev || !pdev->node || !key || !val)
        return VFS_ERR_INVAL;
    const struct device_node* node = pdev->node;
    for (int i = 0; i < node->prop_count; i++)
    {
        if (strcmp(node->props[i].key, key) == 0)
        {
            *val = node->props[i].value;
            return VFS_OK;
        }
    }
    return VFS_ERR_INVAL;
}

/**
 * @brief 读取布尔型设备树属性 (同 get_prop_int)
 * @param pdev device 指针
 * @param key 属性键名
 * @param val 输出整型布尔值 (0/1)
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_get_prop_bool(const struct device* pdev, const char* key, int* val)
{
    return device_get_prop_int(pdev, key, val);
}

/**
 * @brief 获取设备 reg 描述符
 * @param pdev device 指针
 * @param idx reg 索引
 * @param out 输出 reg 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_get_reg(const struct device* pdev, int idx, const struct device_reg** out)
{
    if (!pdev || !pdev->node || !out)
        return VFS_ERR_INVAL;
    if (idx < 0 || idx >= (int)pdev->node->reg_count)
        return VFS_ERR_INVAL;
    if (!pdev->node->regs)
        return VFS_ERR_INVAL;
    *out = &pdev->node->regs[idx];
    return VFS_OK;
}

/**
 * @brief 获取设备 irq 描述符
 * @param pdev device 指针
 * @param idx irq 索引
 * @param out 输出 irq 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_get_irq(const struct device* pdev, int idx, const struct device_irq** out)
{
    if (!pdev || !pdev->node || !out)
        return VFS_ERR_INVAL;
    if (idx < 0 || idx >= (int)pdev->node->irq_count)
        return VFS_ERR_INVAL;
    if (!pdev->node->irqs)
        return VFS_ERR_INVAL;
    *out = &pdev->node->irqs[idx];
    return VFS_OK;
}

/**
 * @brief 获取 device 名称
 * @param pdev device 指针
 * @return 名称字符串, pdev 无效返回 NULL
 */
const char* device_get_name(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev) || !pdev->node)
        return NULL;
    return pdev->node->name;
}

/**
 * @brief 获取 device compatible 字符串
 * @param pdev device 指针
 * @return compatible 字符串, pdev 无效返回 NULL
 */
const char* device_get_compatible(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev) || !pdev->node)
        return NULL;
    return pdev->node->compatible;
}

/**
 * @brief 获取 device 当前状态
 * @param pdev device 指针
 * @return 设备状态枚举, pdev 无效返回 DEVICE_STATUS_DISABLED
 */
enum device_status device_get_status(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev))
        return DEVICE_STATUS_DISABLED;
    return pdev->status;
}

/**
 * @brief 获取 device 关键性等级
 * @param pdev device 指针
 * @return 关键性枚举, pdev 无效返回 DEVICE_CRIT_WARNING
 */
enum device_criticality device_get_criticality(const struct device* pdev)
{
    if (IS_ERR_OR_NULL(pdev) || !pdev->node)
        return DEVICE_CRIT_WARNING;
    return (enum device_criticality)pdev->node->criticality;
}

/**
 * @brief 设置 device 状态 (持锁校验状态机迁移)
 * @param pdev device 指针
 * @param status 目标状态
 * @return 成功返回 VFS_OK, 非法迁移返回 VFS_ERR_INVAL
 */
int device_set_status(struct device* pdev, enum device_status status)
{
    int ret = VFS_OK;

    if (!pdev)
        return VFS_ERR_INVAL;
    if (pdev->lock && osal_mutex_lock(pdev->lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) != OSAL_OK)
        return VFS_ERR_BUSY;

    if (!device_status_can_transit(pdev->status, status))
        ret = VFS_ERR_INVAL;
    else
        pdev->status = status;

    if (pdev->lock)
        (void)osal_mutex_unlock(pdev->lock);
    return ret;
}

/**
 * @brief 设置 device 私有数据指针
 * @param pdev device 指针
 * @param priv 私有数据指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_INVAL
 */
int device_set_priv(struct device* pdev, void* priv)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    pdev->priv_data = priv;
    return VFS_OK;
}

/**
 * @brief 获取 device 私有数据指针
 * @param pdev device 指针
 * @return 成功返回 priv 指针, 未设置或非法返回 ERR_PTR
 */
void* device_get_priv(const struct device* pdev)
{
    if (!pdev)
        return ERR_PTR(VFS_ERR_INVAL);
    if (!pdev->priv_data)
        return ERR_PTR(VFS_ERR_NODEV);
    return pdev->priv_data;
}

/* ── 设备遍历 ── */
/**
 * @brief 获取第一个 device (按 devtable 顺序)
 * @return 成功返回 device 指针, 无设备返回 ERR_PTR
 */
struct device* device_get_first(void)
{
    if (board_dev_count() <= 0)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get((device_id_t)0);
}

/**
 * @brief 获取下一个 device (devtable 顺序)
 * @param prev 当前 device 指针
 * @return 成功返回下一 device 指针, 末尾或非法返回 ERR_PTR
 */
struct device* device_get_next(const struct device* prev)
{
    if (!prev)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    if (IS_ERR(prev))
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    for (int i = 0; i < board_dev_count(); i++)
    {
        if (board_dev_get((device_id_t)i) == prev)
        {
            int next = i + 1;
            if (next >= board_dev_count())
                return (struct device*)ERR_PTR(VFS_ERR_NODEV);
            return board_dev_get((device_id_t)next);
        }
    }
    return (struct device*)ERR_PTR(VFS_ERR_INVAL);
}

/**
 * @brief 获取已注册 device 数量
 * @return device 数量
 */
int device_get_count(void) { return board_dev_count(); }

/* ── VFS 转发层 ──
 * 所有 VFS 入口在持锁状态下完成状态检查 + ops 调用.
 *   device_open/close/suspend/resume + device_write/read/ioctl 全部
 *   在 device_lock(pdev) 保护下执行 check-then-act, 阻断多线程重入.
 *
 * pdev->lock 使用 osal_mutex_create_static_recursive; 驱动 io_lock 使用默认 plain 锁:
 *   - device_write(st7789) → write_cmd → device_write(spi) 持有不同锁, 安全
 *   - 驱动内部对 pdev 自身递归加锁, 递归 mutex 放行
 *
 * device_ops_unregister() 用于 remove 路径清理 priv_data + ops.
 */
/**
 * @brief 打开 device (持锁, PROBED→RUNNING)
 * @param pdev device 指针
 * @param arg 传递给驱动 open/init 的参数
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_open(struct device* pdev, void* arg)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (!pdev->ops || (!pdev->ops->open && !pdev->ops->init))
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }
    if (pdev->status == DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_OK;
    }
    if (pdev->status != DEVICE_STATUS_PROBED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }

    int ret = pdev->ops->open ? pdev->ops->open(pdev, arg) : pdev->ops->init(pdev);
    if (ret == VFS_OK)
        COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_RUNNING));
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return ret;
}

/**
 * @brief 关闭 device (持锁, RUNNING/SUSPENDED→PROBED)
 * @param pdev device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_close(struct device* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (!pdev->ops || !pdev->ops->close)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }
    if (pdev->status != DEVICE_STATUS_RUNNING && pdev->status != DEVICE_STATUS_SUSPENDED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }

    int ret = pdev->ops->close(pdev);
    if (ret == VFS_OK)
        COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_PROBED));
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return ret;
}

/**
 * @brief 写 device (持锁, 需 RUNNING 且 ops->write)
 * @param pdev device 指针
 * @param buf 数据缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK 或驱动返回值, 失败返回负数错误码
 */
int device_write(struct device* pdev, const void* buf, size_t len, uint32_t timeout_ms)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (!pdev->ops || !pdev->ops->write || pdev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }
    int ret = pdev->ops->write(pdev, buf, len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return ret;
}

/**
 * @brief 读 device (持锁, 需 RUNNING 且 ops->read)
 * @param pdev device 指针
 * @param buf 数据缓冲
 * @param len 字节数
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回已读字节数或 VFS_OK, 失败返回负数错误码
 */
int device_read(struct device* pdev, void* buf, size_t len, uint32_t timeout_ms)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (!pdev->ops || !pdev->ops->read || pdev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }
    int ret = pdev->ops->read(pdev, buf, len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return ret;
}

/**
 * @brief ioctl 控制 device (持锁, 需 RUNNING 且 ops->ioctl)
 * @param pdev device 指针
 * @param cmd 控制命令
 * @param arg 命令参数指针
 * @param arg_len 参数长度
 * @param timeout_ms 超时 (毫秒)
 * @return 成功返回 VFS_OK 或驱动返回值, 失败返回负数错误码
 */
int device_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (!pdev->ops || !pdev->ops->ioctl || pdev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }
    int ret = pdev->ops->ioctl(pdev, cmd, arg, arg_len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return ret;
}

/**
 * @brief 挂起 device (RUNNING→SUSPENDED)
 * @param pdev device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_suspend(struct device* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (pdev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }

    int ret = VFS_OK;
    if (pdev->ops && pdev->ops->suspend)
    {
        ret = pdev->ops->suspend(pdev);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(device_unlock(pdev));
            return ret;
        }
    }
    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_SUSPENDED));
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return VFS_OK;
}

/**
 * @brief 恢复 device (SUSPENDED→RUNNING)
 * @param pdev device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_resume(struct device* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(pdev) != VFS_OK)
        return VFS_ERR_BUSY;
    if (pdev->status != DEVICE_STATUS_SUSPENDED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(pdev));
        return VFS_ERR_IO;
    }

    int ret = VFS_OK;
    if (pdev->ops && pdev->ops->resume)
    {
        ret = pdev->ops->resume(pdev);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(device_unlock(pdev));
            return ret;
        }
    }
    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_RUNNING));
    COMPAT_IGNORE_RESULT(device_unlock(pdev));
    return VFS_OK;
}

/* ── 设备锁（启动期静态创建，运行期仅有限时加锁） ── */
/**
 * @brief 获取 device 递归互斥锁
 * @param pdev device 指针
 * @return 成功返回 VFS_OK, 失败返回 VFS_ERR_BUSY 或 VFS_ERR_INVAL
 */
int device_lock(struct device* pdev)
{
    if (!pdev)
        return VFS_ERR_INVAL;
    if (!pdev->lock)
        return VFS_ERR_BUSY;
    return osal_mutex_lock(pdev->lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) == OSAL_OK ? VFS_OK :
                                                                                  VFS_ERR_BUSY;
}

/**
 * @brief 释放 device 递归互斥锁
 * @param pdev device 指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
int device_unlock(struct device* pdev)
{
    if (!pdev || !pdev->lock)
        return VFS_ERR_INVAL;
    return osal_mutex_unlock(pdev->lock) == OSAL_OK ? VFS_OK : VFS_ERR_IO;
}

/* ── 驱动卸载清理：状态锁定 → 广播 → 持锁斩断 ──
 * 必须在持有 pdev->lock 的前提下置空 ops,
 * 阻断 TOCTOU 竞态 (Thread A 在 device_read 中已通过 status 检查,
 * Thread B 同时卸载置空 ops → NULL 解引用 → HardFault).
 *
 * 1. 获取 pdev->lock, 阻断所有正在进行的 VFS 操作
 * 2. 标记 REMOVED, 阻断新 I/O 重入
 * 3. 广播 DeviceRemoved 事件, 通知 UI/异步任务立即释引用
 * 4. 持锁置空 priv_data 与 ops
 * 5. 释放锁
 */
/**
 * @brief 卸载驱动: 标记 REMOVED、广播事件、持锁清空 ops/priv
 * @param pdev device 指针
 */
void device_ops_unregister(struct device* pdev)
{
    if (!pdev)
        return;

    if (device_lock(pdev) != VFS_OK)
        return;

    COMPAT_IGNORE_RESULT(device_set_status(pdev, DEVICE_STATUS_REMOVED));

    COMPAT_IGNORE_RESULT(device_unlock(pdev));

#ifdef CONFIG_EVENT_BUS
    COMPAT_IGNORE_RESULT(event_bus_post(EVENT_SYS_DEVICE_REMOVED, (uintptr_t)pdev));
#endif

    if (device_lock(pdev) != VFS_OK)
        return;

    COMPAT_IGNORE_RESULT(device_set_priv(pdev, NULL));
    pdev->ops = NULL;

    COMPAT_IGNORE_RESULT(device_unlock(pdev));
}

/**
 * @brief 获取 device 关联的生命周期状态机
 * @param pdev device 指针
 * @return 成功返回 dev_lifecycle 指针, 非法返回 ERR_PTR
 */
struct dev_lifecycle* device_lc(struct device* pdev)
{
    if (!pdev)
        return (struct dev_lifecycle*)ERR_PTR(VFS_ERR_INVAL);
    return &pdev->lc;
}

/**
 * @brief 初始化并绑定 device 生命周期状态机
 * @param pdev device 指针
 */
void device_lc_bind(struct device* pdev)
{
    if (pdev)
        dev_lc_init(&pdev->lc);
}
