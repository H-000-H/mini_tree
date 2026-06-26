#include "device.h"
#include "VFS.h"
#include "dev_lifecycle.h"
#include "osal.h"

#include "board_config.h"
#include "board_devtable.h"
#include "board_dma.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "compiler_compat.h"
#include "hal_cpu.h"
#include "event_bus.h"
#include "safe_state.h"
#include "compiler_compat_poison.h"

/* 编译期断言: 互斥锁池必须能覆盖最大设备数 */
_Static_assert(OSAL_MUTEX_POOL_SIZE >= DEV_ID_COUNT,
               "OSAL_MUTEX_POOL_SIZE too small for DEV_ID_COUNT devices");

/* ── 运行时设备实例表 ── */
static struct device s_devices[DEV_ID_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_device_lock_storage[DEV_ID_COUNT][OSAL_MUTEX_STORAGE_SIZE] COMPAT_ALIGNED(4);

/* ── device_set_status FSM 原子锁 (IEC 61508 2.7.1) ── */
static uint8_t s_status_lock_storage[OSAL_SPINLOCK_STORAGE_SIZE] COMPAT_ALIGNED(4);
static struct osal_spinlock* const s_status_lock = (struct osal_spinlock*)s_status_lock_storage;

static int device_status_can_transit(enum device_status from, enum device_status to)
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
        const struct device_node* node = board_node_get((device_id_t)i);
        s_devices[i].node        = node;
        s_devices[i].status      = node ? node->status : DEVICE_STATUS_DISABLED;
        s_devices[i].priv_data   = NULL;
        s_devices[i].ops         = NULL;
        s_devices[i].lock        = NULL;
        s_devices[i].platform_data = NULL;
        dev_lc_reset(&s_devices[i].lc);

        if (node && s_devices[i].status != DEVICE_STATUS_DISABLED
            && !(node->flags & DEVICE_FLAG_DIRECT))
        {
            /* dev->lock 需要递归: osal_mutex_create_static_recursive */
            struct osal_mutex* lock = NULL;
            if (osal_mutex_create_static_recursive(&lock, s_device_lock_storage[i], sizeof(s_device_lock_storage[i])) == 0)
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
        osal_log(OSAL_LOG_WARN, "board",
                 "device_tree_init: mutex pool >90%% used (%d/%d)\n",
                 board_dev_count(), OSAL_MUTEX_POOL_SIZE);
    }

    if (board_dma_register_channels() != VFS_OK)
        return VFS_ERR_IO;

    return board_dev_count() > 0 ? VFS_OK : VFS_ERR_IO;
}




/* ── 运行时设备实例访问 ── */
struct device* board_dev_get(device_id_t id)
{
    if ((int)id < 0 || (int)id >= DEV_ID_COUNT)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    return &s_devices[id];
}

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

struct device* device_get_phandle_dev(const struct device* dev, const char* key)
{
    const char* val;

    if (!dev || !key)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    if (device_get_prop_str(dev, key, &val) != VFS_OK)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    /* dtc-lite 将 phandle 引用存为 label 名字符串 */
    return device_find_by_label(val);
}

struct device* device_find_by_id(device_id_t id)
{
    return board_dev_get(id);
}

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

struct device* device_get_parent(const struct device* dev)
{
    const struct device_node* node;

    if (!dev || !dev->node)
        return (struct device*)ERR_PTR(VFS_ERR_INVAL);
    node = dev->node;
    if (node->dep_count <= 0 || !node->deps)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
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
int device_get_prop_int(const struct device* dev, const char* key, int* val)
{
    if (!dev || !dev->node || !key || !val) return VFS_ERR_INVAL;
    const struct device_node* node = dev->node;
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

int device_get_prop_int_array(const struct device* dev, const char* key, int* out_arr, int max_len)
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
        __builtin_memcpy(token, start, len);
        token[len] = '\0';

        if (safe_parse_int32(token, &out_arr[count]) != 0) return VFS_ERR_INVAL;
        count++;
    }

    return count;
}

int device_get_prop_str(const struct device* dev, const char* key, const char** val)
{
    if (!dev || !dev->node || !key || !val) return VFS_ERR_INVAL;
    const struct device_node* node = dev->node;
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

int device_get_prop_bool(const struct device* dev, const char* key, int* val)
{
    return device_get_prop_int(dev, key, val);
}

int device_get_reg(const struct device* dev, int idx, const struct device_reg** out)
{
    if (!dev || !dev->node || !out) return VFS_ERR_INVAL;
    if (idx < 0 || idx >= (int)dev->node->reg_count) return VFS_ERR_INVAL;
    if (!dev->node->regs) return VFS_ERR_INVAL;
    *out = &dev->node->regs[idx];
    return VFS_OK;
}

int device_get_irq(const struct device* dev, int idx, const struct device_irq** out)
{
    if (!dev || !dev->node || !out) return VFS_ERR_INVAL;
    if (idx < 0 || idx >= (int)dev->node->irq_count) return VFS_ERR_INVAL;
    if (!dev->node->irqs) return VFS_ERR_INVAL;
    *out = &dev->node->irqs[idx];
    return VFS_OK;
}

const char* device_get_name(const struct device* dev)
{
    return dev && dev->node ? dev->node->name : NULL;
}

const char* device_get_compatible(const struct device* dev)
{
    return dev && dev->node ? dev->node->compatible : NULL;
}

enum device_status device_get_status(const struct device* dev)
{
    return dev ? dev->status : DEVICE_STATUS_DISABLED;
}

enum device_criticality device_get_criticality(const struct device* dev)
{
    if (!dev || !dev->node) return DEVICE_CRIT_WARNING;
    return (enum device_criticality)dev->node->criticality;
}

int device_set_status(struct device* dev, enum device_status status)
{
    if (!dev) return VFS_ERR_INVAL;
    osal_spinlock_lock(s_status_lock);
    if (!device_status_can_transit(dev->status, status))
    {
        osal_spinlock_unlock(s_status_lock);
        return VFS_ERR_INVAL;
    }
    dev->status = status;
    osal_spinlock_unlock(s_status_lock);
    return VFS_OK;
}

int device_set_priv(struct device* dev, void* priv)
{
    if (!dev) return VFS_ERR_INVAL;
    dev->priv_data = priv;
    return VFS_OK;
}

void* device_get_priv(const struct device* dev)
{
    if (!dev)
        return ERR_PTR(VFS_ERR_INVAL);
    if (!dev->priv_data)
        return ERR_PTR(VFS_ERR_NODEV);
    return dev->priv_data;
}

/* ── 设备遍历 ── */
struct device* device_get_first(void)
{
    if (board_dev_count() <= 0)
        return (struct device*)ERR_PTR(VFS_ERR_NODEV);
    return board_dev_get((device_id_t)0);
}

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

int device_get_count(void)
{
    return board_dev_count();
}

/* ── VFS 转发层 ──
 * IEC 61508 §7.4.3.1: 所有 VFS 入口在持锁状态下完成状态检查 + ops 调用.
 *   device_open/close/suspend/resume + device_write/read/ioctl 全部
 *   在 device_lock(dev) 保护下执行 check-then-act, 阻断多线程重入.
 *
 * dev->lock 使用 osal_mutex_create_static_recursive; 驱动 io_lock 使用默认 plain 锁:
 *   - device_write(st7789) → write_cmd → device_write(spi) 持有不同锁, 安全
 *   - 驱动内部对 dev 自身递归加锁, 递归 mutex 放行
 *
 * device_ops_unregister() 用于 remove 路径清理 priv_data + ops.
 */
int device_open(struct device* dev, void* arg)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (!dev->ops || (!dev->ops->open && !dev->ops->init))
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }
    if (dev->status == DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_OK;
    }
    if (dev->status != DEVICE_STATUS_PROBED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }

    int ret = dev->ops->open ? dev->ops->open(dev, arg) : dev->ops->init(dev);
    if (ret == VFS_OK)
    {
        dev->status = DEVICE_STATUS_RUNNING;
    }
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return ret;
}

int device_close(struct device* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->close)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }
    if (dev->status != DEVICE_STATUS_RUNNING && dev->status != DEVICE_STATUS_SUSPENDED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }

    int ret = dev->ops->close(dev);
    if (ret == VFS_OK)
    {
        dev->status = DEVICE_STATUS_PROBED;
    }
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return ret;
}

int device_write(struct device* dev, const void* buf, size_t len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->write || dev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }
    int ret = dev->ops->write(dev, buf, len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return ret;
}

int device_read(struct device* dev, void* buf, size_t len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->read || dev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }
    int ret = dev->ops->read(dev, buf, len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return ret;
}

int device_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();
    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (!dev->ops || !dev->ops->ioctl || dev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }
    int ret = dev->ops->ioctl(dev, cmd, arg, arg_len, timeout_ms);
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return ret;
}

int device_suspend(struct device* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (dev->status != DEVICE_STATUS_RUNNING)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }

    int ret = VFS_OK;
    if (dev->ops && dev->ops->suspend)
    {
        ret = dev->ops->suspend(dev);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(device_unlock(dev));
            return ret;
        }
    }
    dev->status = DEVICE_STATUS_SUSPENDED;
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return VFS_OK;
}

int device_resume(struct device* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    HAL_ASSERT_NOT_ISR();

    if (device_lock(dev) != VFS_OK) return VFS_ERR_BUSY;
    if (dev->status != DEVICE_STATUS_SUSPENDED)
    {
        COMPAT_IGNORE_RESULT(device_unlock(dev));
        return VFS_ERR_IO;
    }

    int ret = VFS_OK;
    if (dev->ops && dev->ops->resume)
    {
        ret = dev->ops->resume(dev);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(device_unlock(dev));
            return ret;
        }
    }
    dev->status = DEVICE_STATUS_RUNNING;
    COMPAT_IGNORE_RESULT(device_unlock(dev));
    return VFS_OK;
}

/* ── 设备锁（启动期静态创建，运行期仅有限时加锁） ── */
int device_lock(struct device* dev)
{
    if (!dev) return VFS_ERR_INVAL;
    if (!dev->lock) return VFS_ERR_BUSY;
    return osal_mutex_lock(dev->lock, OSAL_LOCK_TIMEOUT_DEFAULT_MS) == 0 ? VFS_OK : VFS_ERR_BUSY;
}

int device_unlock(struct device* dev)
{
    if (!dev || !dev->lock) return VFS_ERR_INVAL;
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
void device_ops_unregister(struct device* dev)
{
    if (!dev) return;

    if (device_lock(dev) != VFS_OK) return;

    dev->status = DEVICE_STATUS_REMOVED;

    COMPAT_IGNORE_RESULT(device_unlock(dev));

    COMPAT_IGNORE_RESULT(event_bus_post(EVENT_SYS_DEVICE_REMOVED, (uintptr_t)dev));

    if (device_lock(dev) != VFS_OK) return;

    COMPAT_IGNORE_RESULT(device_set_priv(dev, NULL));
    dev->ops = NULL;

    COMPAT_IGNORE_RESULT(device_unlock(dev));
}

struct dev_lifecycle* device_lc(struct device* dev)
{
    if (!dev)
        return (struct dev_lifecycle*)ERR_PTR(VFS_ERR_INVAL);
    return &dev->lc;
}

void device_lc_bind(struct device* dev, struct osal_mutex* io_lock)
{
    if (dev)
        dev_lc_init(&dev->lc, io_lock);
}

static hal_pin_t pin_from_parts(int port, int pin)
{
    int p = (port >= 0) ? port : HAL_GPIO_PORT_DEFAULT;
    return hal_pin_make(p, (uint16_t)pin);
}

int hal_pin_probe(const struct device* dev, const char* port_key, const char* pin_key,
                  hal_pin_t* out)
{
    int port = 0;
    int pin  = -1;

    if (!dev || !pin_key || !out)
        return VFS_ERR_INVAL;

    if (device_get_prop_int(dev, pin_key, &pin))
        return VFS_ERR_INVAL;

#if !COMPAT_HAVE_KCONFIG || defined(CONFIG_HAL_GPIO_PORT_ENUM)
    if (port_key)
        COMPAT_IGNORE_RESULT(device_get_prop_int(dev, port_key, &port));
#else
    (void)port_key;
#endif

    *out = pin_from_parts(port, pin);
    return VFS_OK;
}
