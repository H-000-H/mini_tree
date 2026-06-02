#ifndef BOARD_DEVICE_H
#define BOARD_DEVICE_H

#include <stdint.h>
#include <stddef.h>

#include "board_nodes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 设备树常量 ── */
#define MAX_DEVICES   DEV_ID_COUNT

/* ── 编译期属性: dtc-lite 在构建期展开, runtime 只读静态表 ── */
typedef struct
{
    const char* key;
    const char* value;
} device_prop_t;

/* ── 设备关键性等级 ── */
typedef enum
{
    DEVICE_CRIT_IGNORE = 0,   /* 可无声忽略 */
    DEVICE_CRIT_WARNING,      /* 失败时记录告警 (默认) */
    DEVICE_CRIT_FATAL,        /* 失败时触发 OSAL_PANIC 安全停机 */
} device_criticality_t;

/* ── 设备状态 ── */
typedef enum
{
    DEVICE_STATUS_DISABLED = 0,
    DEVICE_STATUS_UNINIT,
    DEVICE_STATUS_READY,
    DEVICE_STATUS_PROBED,
    DEVICE_STATUS_RUNNING,
    DEVICE_STATUS_SUSPENDED,
    DEVICE_STATUS_ERROR,
    DEVICE_STATUS_REMOVED,
} device_status_t;

/* ── 前向声明 ── */
typedef struct device_instance device_t;
/* 子系统操作表由驱动通过 priv_data 魔术头注入, 不在 device_t 中硬编码 */

/* ── 编译期只读设备树节点 ── */
typedef struct device_node
{
    const char*         name;
    const char*         label;          /* DTS label (如 pwm_backlight) */
    const char*         compatible;
    const char*         path;           /* DTS 全路径 (如 /soc/spi2@0) */
    const device_prop_t* props;
    const device_id_t*  deps;
    uint8_t             status;         /* 编译期默认状态 */
    uint8_t             criticality;    /* DEVICE_CRIT_xxx: probe 失败时的系统行为 */
    uint8_t             flags;          /* DEVICE_FLAG_xxx */
    uint8_t             prop_count;
    uint8_t             dep_count;
} device_node_t;

/* 编译期节点标志 */
#define DEVICE_FLAG_DIRECT    0x01  /* 直接访问 (direct), 无需运行时 device_t 实例 */


/* ── VFS 操作表 ── */
typedef struct file_operation
{
    int (*init) (device_t* dev);
    int (*open) (device_t* dev, void* arg);
    int (*close)(device_t* dev);
    int (*write)(device_t* dev, const void* buffer, size_t len, uint32_t timeout_ms);
    int (*read) (device_t* dev, void* buffer, size_t len, uint32_t timeout_ms);
    int (*ioctl)(device_t* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms);
    int (*suspend)(device_t* dev);
    int (*resume)(device_t* dev);
} file_operation_t;

/* ── 运行时设备实例 ── */
typedef struct device_instance
{
    const device_node_t*   node;       /* 指向编译期节点 */
    device_status_t        status;     /* 运行时状态 */
    void*                  priv_data;  /* 驱动私有数据 (VFS 层) */
    void*                  subsys_priv;/* 子系统私有数据 (sensor_if/display_if 魔术头, 零偏移假设, MISRA 11.3 合规) */
    const file_operation_t* ops;       /* 操作函数表 */
    struct osal_mutex*     lock;       /* per-device mutex (device_tree_init 中编译期静态分配) */
    void*                  platform_data; /* board 层注入的静态数据, probe 前设置 */
} device_t;

/* ── 查找设备 ── */
device_t* device_find(const char* name);
device_t* device_find_by_label(const char* label);
device_t* device_find_by_compatible(const char* compatible);
device_t* device_find_by_id(device_id_t id);
device_t* device_find_by_path(const char* path);
device_t* device_get_parent(const device_t* dev);

/* ── 从属性中解析 phandle 引用并返回目标设备 ── */
device_t* device_get_phandle_dev(const device_t* dev, const char* key);

/* ── 读取属性（从 dev->node 读取） ── */
int device_get_prop_int(const device_t* dev, const char* key, int* val);
int device_get_prop_int_array(const device_t* dev, const char* key, int* out_arr, int max_len);
int device_get_prop_str(const device_t* dev, const char* key, const char** val);
int device_get_prop_bool(const device_t* dev, const char* key, int* val);
const char* device_get_name(const device_t* dev);
const char* device_get_compatible(const device_t* dev);
device_status_t device_get_status(const device_t* dev);
device_criticality_t device_get_criticality(const device_t* dev);

/* ── 运行时状态管理 ── */
int device_set_status(device_t* dev, device_status_t status);
int device_set_priv(device_t* dev, void* priv);
void* device_get_priv(const device_t* dev);

/* ── 子系统私有数据 (MISRA C 2012 Rule 11.3 合规, 替代隐式偏移继承) ── */
int device_set_subsys_priv(device_t* dev, void* subsys_priv);
void* device_get_subsys_priv(const device_t* dev);

/* ── 设备遍历 ── */
device_t* device_get_first(void);
device_t* device_get_next(const device_t* prev);
int device_get_count(void);

/* ── 设备树加载 ── */
int device_tree_init(void);

/* ── 设备锁（device_tree_init 中已完成全量静态分配） ── */
int device_lock(device_t* dev);
int device_unlock(device_t* dev);

/* ── 驱动卸载清理 ──
 * 清除 dev->priv_data + dev->ops, 切断幽灵指针链.
 * 由 driver remove 函数在最后调用, 替代手写 device_set_priv(dev,NULL)+dev->ops=NULL.
 */
void device_ops_unregister(device_t* dev);

/* ── VFS 便捷包装（框架层自动持锁, IEC 61508 §7.4.3.1） ──
 * device_open/close/suspend/resume + device_write/read/ioctl 均在持锁状态下
 * 完成状态检查与 ops 调用, 确保 check-then-act 的原子性.
 */
int device_open(device_t* dev, void* arg);
int device_close(device_t* dev);
int device_write(device_t* dev, const void* buf, size_t len, uint32_t timeout_ms);
int device_read(device_t* dev, void* buf, size_t len, uint32_t timeout_ms);
int device_ioctl(device_t* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms);
int device_suspend(device_t* dev);
int device_resume(device_t* dev);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DEVICE_H */
