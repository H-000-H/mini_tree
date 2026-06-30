/* SPDX-License-Identifier: Apache-2.0 */
/*
 * device.h — 板级设备模型核心头文件
 *
 * 定义编译期 device_node (dtc-lite 生成的只读 DTS 节点) 与运行时 device 实例,
 * 含 file_operations VFS 操作表、device_status/criticality 状态机枚举.
 * 声明设备查找、属性读取 (reg/irq/prop)、VFS 便捷包装 (持锁 open/read/write 等).
 */
#ifndef BOARD_DEVICE_H
#define BOARD_DEVICE_H

#include <stdint.h>
#include <stddef.h>

#include "board_nodes.h"
#include "dev_lifecycle.h"
#include "compiler_compat.h"
#include "hal_gpio.h"

#ifdef __cplusplus
extern "C" 
{
#endif

                                                            /*设备树常量*/
/*===========================================================================================================================================================*/
#define MAX_DEVICES   DEV_ID_COUNT

/* 编译期节点标志 */
#define DEVICE_FLAG_DIRECT    0x01  /* 直接访问 (direct), 无需运行时 struct device 实例 */
/*===========================================================================================================================================================*/

                                                            /*编译期属性*/
/*===========================================================================================================================================================*/
/* ── 编译期属性: dtc-lite 在构建期展开, runtime 只读静态表 ── */
struct device_property
{
    const char* key;
    const char* value;
};
/*===========================================================================================================================================================*/

                                                            /*设备关键性枚举*/
/*===========================================================================================================================================================*/
enum device_criticality
{
    DEVICE_CRIT_IGNORE = 0,   /* 可无声忽略 */
    DEVICE_CRIT_WARNING,      /* 失败时记录告警 (默认) */
    DEVICE_CRIT_FATAL,        /* 失败时触发 OSAL_PANIC 安全停机 */
};
/*===========================================================================================================================================================*/

                                                            /*设备状态枚举*/
/*===========================================================================================================================================================*/
enum device_status
{
    DEVICE_STATUS_DISABLED = 0,
    DEVICE_STATUS_UNINIT,
    DEVICE_STATUS_READY,
    DEVICE_STATUS_PROBED,
    DEVICE_STATUS_RUNNING,
    DEVICE_STATUS_SUSPENDED,
    DEVICE_STATUS_ERROR,
    DEVICE_STATUS_REMOVED,
};
/*===========================================================================================================================================================*/

                                                            /*reg 与 interrupt 条目*/
/*===========================================================================================================================================================*/
/* ── reg 条目（由 dtc-lite 按 #address-cells / #size-cells 分组） ── */
struct device_reg
{
    const uint32_t* addr;       /* 地址值数组 [#address-cells 个] */
    const uint32_t* size;       /* 长度值数组 [#size-cells 个] (NULL 若 size-cells == 0) */
    uint8_t         addr_cells;
    uint8_t         size_cells;
};

/* ── interrupt 条目（由 dtc-lite 按 #interrupt-cells 分组） ── */
struct device_irq
{
    int             irq;        /* 中断号（供 hal_irq_enable 使用） */
    int             type;       /* 中断类型（GIC SPI=0, PPI=1, 或直接填 flags） */
    int             flags;      /* 中断标志（IRQ_TYPE_LEVEL_HIGH 等） */
};
/*===========================================================================================================================================================*/

                                                            /*前向声明*/
/*===========================================================================================================================================================*/
struct device;
/* 子系统操作表由驱动通过 priv_data 魔术头注入, 不在 struct device 中硬编码 */
/*===========================================================================================================================================================*/

                                                            /*编译期设备树节点*/
/*===========================================================================================================================================================*/
struct device_node
{
    const char*         name;
    const char*         label;          /* DTS label (如 pwm_backlight) */
    const char*         compatible;
    const char*         path;           /* DTS 全路径 (如 /soc/spi2@0) */
    const struct device_property* props;
    const device_id_t*  deps;
    const struct device_reg* regs;      /* reg 条目表（预分组, NULL 表示无 reg） */
    const struct device_irq* irqs;      /* interrupt 表（预分组, NULL 表示无 interrupts） */
    uint8_t             status;         /* 编译期默认状态 */
    uint8_t             criticality;    /* DEVICE_CRIT_xxx: probe 失败时的系统行为 */
    uint8_t             flags;          /* DEVICE_FLAG_xxx */
    uint8_t             prop_count;
    uint8_t             dep_count;
    uint8_t             reg_count;      /* reg 条目数 */
    uint8_t             irq_count;      /* interrupt 条目数 */
};

/* 各 compatible 属性契约见 board/docs/devicetree.md */
/*===========================================================================================================================================================*/

                                                            /*VFS 操作表*/
/*===========================================================================================================================================================*/
struct file_operations
{
    int (*init) (struct device* dev);
    int (*open) (struct device* dev, void* arg);
    int (*close)(struct device* dev);
    int (*write)(struct device* dev, const void* buffer, size_t len, uint32_t timeout_ms);
    int (*read) (struct device* dev, void* buffer, size_t len, uint32_t timeout_ms);
    int (*ioctl)(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms);
    int (*suspend)(struct device* dev);
    int (*resume)(struct device* dev);
};
/*===========================================================================================================================================================*/

                                                            /*运行时设备实例*/
/*===========================================================================================================================================================*/
struct device 
{
    const struct device_node* node;       /* 指向编译期节点 */
    enum device_status        status;     /* 运行时状态 */
    void*                     priv_data;  /* 驱动私有数据 (VFS 层) */
    const struct file_operations* ops;    /* 操作函数表 */
    struct osal_mutex*        lock;       /* per-device 递归锁 (create_static_recursive) */
    struct dev_lifecycle           lc;         /* 驱动 I/O 生命周期 (probe 时 device_lc_bind) */
    void*                     platform_data; /* board 层注入的静态数据, probe 前设置 */
};
/*===========================================================================================================================================================*/

                                                            /*设备查找*/
/*===========================================================================================================================================================*/
/* ── 查找设备 (失败返回 ERR_PTR(errno), 调用方用 IS_ERR/PTR_ERR 解码) ── */
struct device* device_find(const char* name);
struct device* device_find_by_label(const char* label);
struct device* device_find_by_compatible(const char* compatible);
struct device* device_find_by_id(device_id_t id);
struct device* device_find_by_path(const char* path) COMPAT_WARN_UNUSED_RESULT;
struct device* device_get_parent(const struct device* dev);
/*===========================================================================================================================================================*/

                                                            /*phandle 引用解析*/
/*===========================================================================================================================================================*/
/* ── 从属性中解析 phandle 引用并返回目标设备 ── */
struct device* device_get_phandle_dev(const struct device* dev, const char* key)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*属性读取*/
/*===========================================================================================================================================================*/
/* ── 读取属性（从 dev->node 读取） ── */
int device_get_prop_int(const struct device* dev, const char* key, int* val)
    COMPAT_WARN_UNUSED_RESULT;
int device_get_prop_int_array(const struct device* dev, const char* key, int* out_arr, int max_len)
    COMPAT_WARN_UNUSED_RESULT;
int device_get_prop_str(const struct device* dev, const char* key, const char** val)
    COMPAT_WARN_UNUSED_RESULT;
int device_get_prop_bool(const struct device* dev, const char* key, int* val)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*引脚探测与基本信息*/
/*===========================================================================================================================================================*/
const char* device_get_name(const struct device* dev);
const char* device_get_compatible(const struct device* dev);
enum device_status device_get_status(const struct device* dev);
enum device_criticality device_get_criticality(const struct device* dev);
/*===========================================================================================================================================================*/

                                                            /*reg 与 interrupt 读取*/
/*===========================================================================================================================================================*/
/* ── 读取第 idx 条 reg 条目（按 #address-cells / #size-cells 分组） ── */
int device_get_reg(const struct device* dev, int idx, const struct device_reg** out)
    COMPAT_WARN_UNUSED_RESULT;

/* ── 读取第 idx 条 interrupt 条目（按 #interrupt-cells 分组） ── */
int device_get_irq(const struct device* dev, int idx, const struct device_irq** out)
    COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*运行时状态管理*/
/*===========================================================================================================================================================*/
int device_set_status(struct device* dev, enum device_status status) COMPAT_WARN_UNUSED_RESULT;
int device_set_priv(struct device* dev, void* priv) COMPAT_WARN_UNUSED_RESULT;
/* 失败返回 ERR_PTR(errno); priv_data 未设置返回 ERR_PTR(VFS_ERR_NODEV) */
void* device_get_priv(const struct device* dev);
/*===========================================================================================================================================================*/

                                                            /*设备遍历*/
/*===========================================================================================================================================================*/
/* ── 设备遍历 (无设备/迭代结束返回 ERR_PTR(VFS_ERR_NODEV)) ── */
struct device* device_get_first(void);
struct device* device_get_next(const struct device* prev);
int device_get_count(void);
/*===========================================================================================================================================================*/

                                                            /*设备树加载*/
/*===========================================================================================================================================================*/
int device_tree_init(void) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*设备锁*/
/*===========================================================================================================================================================*/
/* ── 设备锁（device_tree_init 中已完成全量静态分配） ── */
int device_lock(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int device_unlock(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

                                                            /*驱动卸载清理*/
/*===========================================================================================================================================================*/
/* ── 驱动卸载清理 ──
 * 清除 dev->priv_data + dev->ops, 切断幽灵指针链.
 * 由 driver remove 函数在最后调用, 替代手写 device_set_priv(dev,NULL)+dev->ops=NULL.
 */
void device_ops_unregister(struct device* dev);
/*===========================================================================================================================================================*/

                                                            /*I/O 生命周期绑定*/
/*===========================================================================================================================================================*/
/* ── 驱动 I/O 生命周期 (dev_lifecycle 绑定在 struct device 上) ── */
/* 失败时返回 ERR_PTR(errno), 调用方用 IS_ERR/PTR_ERR 解码 */
struct dev_lifecycle* device_lc(struct device* dev);
void device_lc_bind(struct device* dev, struct osal_mutex* io_lock);
/*===========================================================================================================================================================*/

                                                            /*VFS 便捷包装*/
/*===========================================================================================================================================================*/
/* ── VFS 便捷包装（框架层自动持锁, IEC 61508 §7.4.3.1） ──
 * device_open/close/suspend/resume + device_write/read/ioctl 均在持锁状态下
 * 完成状态检查与 ops 调用, 确保 check-then-act 的原子性.
 */
int device_open(struct device* dev, void* arg) COMPAT_WARN_UNUSED_RESULT;
int device_close(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int device_write(struct device* dev, const void* buf, size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int device_read(struct device* dev, void* buf, size_t len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int device_ioctl(struct device* dev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms)
    COMPAT_WARN_UNUSED_RESULT;
int device_suspend(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
int device_resume(struct device* dev) COMPAT_WARN_UNUSED_RESULT;
/*===========================================================================================================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DEVICE_H */
