#include "device.h"
#include "VFS.h"
#include "osal.h"

#include "board_config.h"
#include "board_devtable.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler_compat.h"
#include "hal_cpu_fast.h"
#include "event_bus.hpp"
#include "safe_state.h"

/* 编译期断言: 互斥锁池必须能覆盖最大设备数 */
_Static_assert(OSAL_MUTEX_POOL_SIZE >= DEV_ID_COUNT,
               "OSAL_MUTEX_POOL_SIZE too small for DEV_ID_COUNT devices");

/* ── 运行时设备实例表 ── */
static device_t s_devices[DEV_ID_COUNT];
static uint8_t s_device_lock_storage[DEV_ID_COUNT][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4);

/* ── device_set_status FSM 原子锁 (IEC 61508 2.7.1) ── */
static uint8_t s_status_lock_storage[OSAL_SPINLOCK_STORAGE_SIZE] COMPAT_ALIGNED(4);
static osal_spinlock_t* const s_status_lock = (osal_spinlock_t*)s_status_lock_storage;

static int device_status_can_transit(device_status_t from, device_status_t to)
{
    if (from == to) return 1;

    switch (from)
    {
    case DEVICE_STATUS_DISABLED:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_UNINIT;
    case DEVICE_STATUS_UNINIT:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_ERROR || to == DEVICE_STATUS_DISABLED;
    case DEVICE_STATUS_READY:
        return to == DEVICE_STATUS_PROBED || to == DEVICE_STATUS_DISABLED || to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_PROBED:
        return to == DEVICE_STATUS_RUNNING || to == DEVICE_STATUS_SUSPENDED ||
               to == DEVICE_STATUS_READY || to == DEVICE_STATUS_REMOVED || to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_RUNNING:
        return to == DEVICE_STATUS_SUSPENDED || to == DEVICE_STATUS_READY ||
               to == DEVICE_STATUS_REMOVED  || to == DEVICE_STATUS_ERROR ||
               to == DEVICE_STATUS_PROBED;
    case DEVICE_STATUS_SUSPENDED:
        return to == DEVICE_STATUS_RUNNING || to == DEVICE_STATUS_READY ||
               to == DEVICE_STATUS_REMOVED || to == DEVICE_STATUS_ERROR;
    case DEVICE_STATUS_ERROR:
        return 0;
    case DEVICE_STATUS_REMOVED:
        return to == DEVICE_STATUS_READY || to == DEVICE_STATUS_DISABLED;
    default:
        return 0;
    }
}

int device_tree_init(void)
{
    for (int i = 0; i < DEV_ID_COUNT; i++)
    {
        const device_node_t* node = board_node_get((device_id_t)i);
        s_devices[i].node        = node;
        s_devices[i].status      = node ? node->status : DEVICE_STATUS_DISABLED;
        s_devices[i].priv_data   = NULL;
        s_devices[i].subsys_priv = NULL;
        s_devices[i].ops         = NULL;
        s_devices[i].lock        = NULL;
        s_devices[i].platform_data = NULL;

        if (node && s_devices[i].status != DEVICE_STATUS_DISABLED
            && !(node->flags & DEVICE_FLAG_DIRECT))
        {
            osal_mutex_t* lock = NULL;
            if (osal_mutex_create_static(&lock, s_device_lock_storage[i], sizeof(s_device_lock_storage[i])) == 0)
            {
                s_devices[i].lock = lock;
            }
            else
            {
                enter_safe_state("device_tree_init: mutex create failed");
            }
        }
    }
    osal_spinlock_init(s_status_lock);

    /* 池水位线预警 */
    if (board_dev_count() >= OSAL_MUTEX_POOL_SIZE * 9 / 10)
    {
        printf("[WARN] device_tree_init: mutex pool >90%% used (%d/%d)\n",
               board_dev_count(), OSAL_MUTEX_POOL_SIZE);
    }

    return board_dev_count() > 0 ? 0 : -1;
}




/* ── 运行时设备实例访问 ── */
device_t* board_dev_get(device_id_t id)
{
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;
    return &s_devices[id];
}

device_t* device_find(const char* name)
{
    device_id_t id = board_dev_find(name);
    if ((int)id < 0) return NULL;
    return board_dev_get(id);
}

device_t* device_find_by_label(const char* label)
{
    device_id_t id = board_dev_find_by_label(label);
    if ((int)id < 0) return NULL;
    return board_dev_get(id);
}

device_t* device_get_phandle_dev(const device_t* dev, const char* key)
{
    const char* val;
    if (device_get_prop_str(dev, key, &val) != 0) return NULL;
    /* dtc-lite 将 phandle 引用存为 label 名字符串 */
    return device_find_by_label(val);
}

device_t* device_find_by_id(device_id_t id)
{
    return board_dev_get(id);
}

device_t* device_find_by_path(const char* path)
{
    if (!path) return NULL;
    for (int i = 0; i < DEV_ID_COUNT; i++)
    {
        const device_node_t* node = board_node_get((device_id_t)i);
        if (node && node->path && strcmp(node->path, path) == 0)
            return board_dev_get((device_id_t)i);
    }
    return NULL;
}

device_t* device_find_by_compatible(const char* compatible)
{
    device_id_t id = board_dev_find_by_compat(compatible);
    if ((int)id < 0) return NULL;
    return board_dev_get(id);
}

device_t* device_get_parent(const device_t* dev)
{
    if (!dev || !dev->node) return NULL;
    const device_node_t* node = dev->node;
    if (node->dep_count <= 0 || !node->deps) return NULL;
    return board_dev_get(node->deps[0]);
}

/* ── safe_parse_int32: MISRA C 2012 Rule 21.6 合规替代 strtol ──
 * 无 errno 依赖, 线程安全, 支持 dec/hex/oct 前缀.
 * 返回 0 成功, -1 非法字符或溢出.
 */
static int safe_parse_int32(const char* str, int* out)
{
    if (!str || !*str || !out) return -1;

    int sign = 1;
    const char* p = str;
    if (*p == '-')
    { sign = -1; p++; }
    else if (*p == '+')
    { p++; }

    int base = 10;
    if (*p == '0')
    {
        p++;
        if (*p == 'x' || *p == 'X')
        { base = 16; p++; }
        else if (*p != '\0')
        { base = 8; }
    }

    if (!*p) return -1;

    uint32_t val = 0;
    const uint32_t limit = (sign > 0) ? (uint32_t)INT32_MAX : (uint32_t)INT32_MAX + 1UL;

    while (*p)
    {
        /* 空格作为自然终止符 — 兼容 multi-int 属性串如 "1073758208 1024" */
        if (*p == ' ') break;

        uint32_t digit;
        if (*p >= '0' && *p <= '9')      digit = (uint32_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') digit = (uint32_t)(*p - 'a' + 10);
        else if (*p >= 'A' && *p <= 'F') digit = (uint32_t)(*p - 'A' + 10);
        else return -1;

        if (digit >= (uint32_t)base) return -1;

        if (val > (limit - digit) / (uint32_t)base) return -1;
        val = val * (uint32_t)base + digit;
        p++;
    }

    *out = (sign > 0) ? (int)val : -(int)val;
    return 0;
}

/* ── 属性读取（通过 dev->node） ── */
int device_get_prop_int(const device_t* dev, const char* key, int* val)
{
    if (!dev || !dev->node || !key || !val) return VFS_ERR_INVAL;
    const device_node_t* node = dev->node;
    for (int i = 0; i < node->prop_count; i++)
    {
        if (strcmp(node->props[i].key, key) == 0)
            return safe_parse_int32(node->props[i].value, val);
    }
    return VFS_ERR_INVAL;
}

int device_get_prop_int_array(const device_t* dev, const char* key, int* out_arr, int max_len)
{
    if (!dev || !dev->node || !key || !out_arr || max_len <= 0) return VFS_ERR_INVAL;

    const char* value = NULL;
    for (int i = 0; i < dev->node->prop_count; i++)
    {
        if (strcmp(dev->node->props[i].key, key) == 0)
        {
            value = dev->node->props[i].value;
            break;
        }
    }
    if (!value) return VFS_ERR_INVAL;

    /* 解析空格分隔的整数串 */
    int count = 0;
    const char* p = value;
    while (*p && count < max_len)
    {
        while (*p == ' ') p++;
        if (!*p) break;

        /* 计算当前 token 长度 */
        const char* start = p;
        while (*p && *p != ' ') p++;

        /* 复制 token 到临时缓冲区 */
        char token[64];
        size_t len = (size_t)(p - start);
        if (len >= sizeof(token)) return VFS_ERR_INVAL;
        memcpy(token, start, len);
        token[len] = '\0';

        if (safe_parse_int32(token, &out_arr[count]) != 0) return VFS_ERR_INVAL;
        count++;
    }

    return count;
}

int device_get_prop_str(const device_t* dev, const char* key, const char** val)
{
    if (!dev || !dev->node || !key || !val) return VFS_ERR_INVAL;
    const device_node_t* node = dev->node;
    for (int i = 0; i < node->prop_count; i++)
    {
        if (strcmp(node->props[i].key, key) == 0)
        {
            *val = node->props[i].value;
            return 0;
        }
    }
    return VFS_ERR_INVAL;
}

int device_get_prop_bool(const device_t* dev, const char* key, int* val)
{
    return device_get_prop_int(dev, key, val);
}

const char* device_get_name(const device_t* dev)
{
    return dev && dev->node ? dev->node->name : NULL;
}

const char* device_get_compatible(const device_t* dev)
{
    return dev && dev->node ? dev->node->compatible : NULL;
}

device_status_t device_get_status(const device_t* dev)
{
    return dev ? dev->status : DEVICE_STATUS_DISABLED;
}

device_criticality_t device_get_criticality(const device_t* dev)
{
    if (!dev || !dev->node) return DEVICE_CRIT_WARNING;
    return (device_criticality_t)dev->node->criticality;
}

int device_set_status(device_t* dev, device_status_t status)
{
    if (!dev) return -1;
    osal_spinlock_lock(s_status_lock);
    if (!device_status_can_transit(dev->status, status))
    {
        osal_spinlock_unlock(s_status_lock);
        return -1;
    }
    dev->status = status;
    osal_spinlock_unlock(s_status_lock);
    return 0;
}

int device_set_priv(device_t* dev, void* priv)
{
    if (!dev) return -1;
    dev->priv_data = priv;
    return 0;
}

void* device_get_priv(const device_t* dev)
{
    return dev ? dev->priv_data : NULL;
}

int device_set_subsys_priv(device_t* dev, void* subsys_priv)
{
    if (!dev) return -1;
    dev->subsys_priv = subsys_priv;
    return 0;
}

void* device_get_subsys_priv(const device_t* dev)
{
    return dev ? dev->subsys_priv : NULL;
}

/* ── 设备遍历 ── */
device_t* device_get_first(void)
{
    return board_dev_count() > 0 ? board_dev_get((device_id_t)0) : NULL;
}

device_t* device_get_next(const device_t* prev)
{
    if (!prev) return NULL;
    for (int i = 0; i < board_dev_count(); i++)
    {
        if (board_dev_get((device_id_t)i) == prev)
        {
            int next = i + 1;
            if (next >= board_dev_count()) return NULL;
            return board_dev_get((device_id_t)next);
        }
    }
    return NULL;
}

int device_get_count(void)
{
    return board_dev_count();
}

/* ── VFS 转发层 ──
 * IEC 61508 §7.4.3.1: 所有 VFS 入口在持锁状态下完成状态检查 + ops 调用.
 *   device_open/close/suspend/resume + device_write/read/ioctl 全部
 *   在 device_lock(dev) 保护下执行 check-then-act, 阻断多线程重入.
 *
 * 递归 mutex 保证嵌套调用安全:
 *   - device_write(st7789) → write_cmd → device_write(spi) 持有不同锁, 安全
 *   - 驱动内部对 dev 自身递归加锁, 递归 mutex 放行
 *
 * device_ops_unregister() 用于 remove 路径清理 priv_data + ops.
 */
int device_open(device_t* dev, void* arg)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (!dev->ops || (!dev->ops->open && !dev->ops->init))
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }
    if (dev->status != DEVICE_STATUS_PROBED)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }

    int ret = dev->ops->open ? dev->ops->open(dev, arg) : dev->ops->init(dev);
    if (ret == 0)
    {
        dev->status = DEVICE_STATUS_RUNNING;
    }
    device_unlock(dev);
    return ret;
}

int device_close(device_t* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->close)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }
    if (dev->status != DEVICE_STATUS_RUNNING && dev->status != DEVICE_STATUS_SUSPENDED)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }

    int ret = dev->ops->close(dev);
    if (ret == 0)
    {
        dev->status = DEVICE_STATUS_PROBED;
    }
    device_unlock(dev);
    return ret;
}

int device_write(device_t* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->write || dev->status != DEVICE_STATUS_RUNNING)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }
    int ret = dev->ops->write(dev, buf, len, timeout_ms);
    device_unlock(dev);
    return ret;
}

int device_read(device_t* dev, void* buf, size_t len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->read || dev->status != DEVICE_STATUS_RUNNING)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }
    int ret = dev->ops->read(dev, buf, len, timeout_ms);
    device_unlock(dev);
    return ret;
}

int device_ioctl(device_t* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->ioctl || dev->status != DEVICE_STATUS_RUNNING)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }
    int ret = dev->ops->ioctl(dev, cmd, arg, arg_len, timeout_ms);
    device_unlock(dev);
    return ret;
}

int device_suspend(device_t* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (dev->status != DEVICE_STATUS_RUNNING)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }

    int ret = 0;
    if (dev->ops && dev->ops->suspend)
    {
        ret = dev->ops->suspend(dev);
        if (ret != 0)
        {
            device_unlock(dev);
            return ret;
        }
    }
    dev->status = DEVICE_STATUS_SUSPENDED;
    device_unlock(dev);
    return 0;
}

int device_resume(device_t* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != 0) return VFS_ERR_BUSY;
    if (dev->status != DEVICE_STATUS_SUSPENDED)
    {
        device_unlock(dev);
        return VFS_ERR_IO;
    }

    int ret = 0;
    if (dev->ops && dev->ops->resume)
    {
        ret = dev->ops->resume(dev);
        if (ret != 0)
        {
            device_unlock(dev);
            return ret;
        }
    }
    dev->status = DEVICE_STATUS_RUNNING;
    device_unlock(dev);
    return 0;
}

/* ── 设备锁（启动期静态创建，运行期仅有限时加锁） ── */
int device_lock(device_t* dev)
{
    if (!dev) return -1;
    if (!dev->lock) return VFS_ERR_BUSY;
    return osal_mutex_lock(dev->lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) == 0 ? VFS_OK : VFS_ERR_BUSY;
}

int device_unlock(device_t* dev)
{
    if (!dev || !dev->lock) return -1;
    return osal_mutex_unlock(dev->lock) == 0 ? VFS_OK : VFS_ERR_IO;
}

/* ── 驱动卸载清理：状态锁定 → 广播 → 持锁斩断 ──
 * IEC 61508 §7.4.3.1: 必须在持有 dev->lock 的前提下置空 ops,
 * 阻断 TOCTOU 竞态 (Thread A 在 device_read 中已通过 status 检查,
 * Thread B 同时卸载置空 ops → NULL 解引用 → HardFault).
 *
 * 1. 获取 dev->lock, 阻断所有正在进行的 VFS 操作
 * 2. 标记 REMOVED, 阻断新 I/O 重入
 * 3. 广播 DeviceRemoved 事件, 通知 UI/异步任务立即释引用
 * 4. 持锁置空 priv_data 与 ops
 * 5. 释放锁
 */
void device_ops_unregister(device_t* dev)
{
    if (!dev) return;

    if (device_lock(dev) != 0) return;

    dev->status = DEVICE_STATUS_REMOVED;

    device_unlock(dev);

    event_bus_post(EVENT_SYS_DEVICE_REMOVED, (uintptr_t)dev);

    if (device_lock(dev) != 0) return;

    device_set_priv(dev, NULL);
    dev->subsys_priv = NULL;
    dev->ops = NULL;

    device_unlock(dev);
}
