/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file device.h
 *@brief device 头文件
 *@author H-000-H
 *@details
 *   device.h — 板级设备模型核心头文件
 *   定义编译期 device_node (dtc-lite 生成的只读 DTS 节点) 与运行时 device 实例,
 *   含 file_operations VFS 操作表、device_status/criticality 状态机枚举.
 *   声明设备查找、属性读取 (reg/irq/prop)、VFS 便捷包装 (持锁 open/read/write 等).
 */

#ifndef BOARD_DEVICE_H
#define BOARD_DEVICE_H

#include "board_nodes.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "hal_gpio.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ── 对外 API 返回值检查开关 ──
 * device/VFS 层是暴露给应用层的入口。默认关闭返回值强制检查
 * (应用层调用 device_open/read/write/ioctl 等可忽略返回值);
 * 需要严格检查时在 Kconfig 开启 DEVICE_WARN_UNUSED_RESULT。
 * 底层 HAL/bus 的 COMPAT_WARN_UNUSED_RESULT 不受此影响, 始终由
 * CONFIG_COMPILER_WARN_UNUSED_RESULT 控制 (默认开启)。
 */
#if defined(CONFIG_DEVICE_WARN_UNUSED_RESULT)
#define DEVICE_WARN_UNUSED_RESULT COMPAT_WARN_UNUSED_RESULT
#else
#define DEVICE_WARN_UNUSED_RESULT
#endif

/* ── 设备树常量 ── */
#define MAX_DEVICES DEV_ID_COUNT

    /* ── 编译期属性: dtc-lite 在构建期展开, runtime 只读静态表 ── */
    struct device_property
    {
        const char* key; /**< 属性键名 */
        const char* value; /**< 属性值字符串 */
    };

    /* ── 设备关键性等级 ── */
    enum device_criticality
    {
        DEVICE_CRIT_IGNORE = 0, /**< 可无声忽略 */
        DEVICE_CRIT_WARNING, /**< 失败时记录告警 (默认) */
        DEVICE_CRIT_FATAL, /**< 失败时触发 OSAL_PANIC 安全停机 */
    };

    /* ── 设备状态 ── */
    enum device_status
    {
        DEVICE_STATUS_DISABLED = 0, /**< 已禁用 */
        DEVICE_STATUS_UNINIT, /**< 未初始化 */
        DEVICE_STATUS_READY, /**< 就绪 */
        DEVICE_STATUS_PROBED, /**< 已探测 */
        DEVICE_STATUS_RUNNING, /**< 运行中 */
        DEVICE_STATUS_SUSPENDED, /**< 已挂起 */
        DEVICE_STATUS_ERROR, /**< 错误 */
        DEVICE_STATUS_REMOVED, /**< 已移除 */
    };

    /* ── reg 条目（由 dtc-lite 按 #address-cells / #size-cells 分组） ── */
    struct device_reg
    {
        const uint32_t* addr; /**< 地址值数组 [#address-cells 个] */
        const uint32_t* size; /**< 长度值数组 [#size-cells 个] (NULL 若 size-cells == 0) */
        uint8_t addr_cells; /**< 地址单元数 */
        uint8_t size_cells; /**< 长度单元数 */
    };

    /* ── interrupt 条目（由 dtc-lite 按 #interrupt-cells 分组） ── */
    struct device_irq
    {
        int irq; /**< 中断号（供 hal_irq_enable 使用） */
        int type; /**< 中断类型（GIC SPI=0, PPI=1, 或直接填 flags） */
        int flags; /**< 中断标志（IRQ_TYPE_LEVEL_HIGH 等） */
    };

    /* ── 前向声明 ── */
    struct device;
    /* 子系统操作表由驱动通过 priv_data 魔术头注入, 不在 struct device 中硬编码 */

    /* ── 编译期只读设备树节点 ── */
    struct device_node
    {
        const char* name; /**< 节点名称 */
        const char* label; /**< DTS label (如 pwm_backlight) */
        const char* compatible; /**< compatible 字符串 */
        const char* path; /**< DTS 全路径 (如 /soc/spi2@0) */
        const struct device_property* props; /**< 属性表 */
        const device_id_t* deps; /**< 依赖设备 ID 表 */
        const struct device_reg* regs; /**< reg 条目表（预分组, NULL 表示无 reg） */
        const struct device_irq* irqs; /**< interrupt 表（预分组, NULL 表示无 interrupts） */
        uint8_t status; /**< 编译期默认状态 */
        uint8_t criticality; /**< DEVICE_CRIT_xxx: probe 失败时的系统行为 */
        uint8_t flags; /**< DEVICE_FLAG_xxx */
        uint8_t prop_count; /**< 属性数量 */
        uint8_t dep_count; /**< 依赖数量 */
        uint8_t reg_count; /**< reg 条目数 */
        uint8_t irq_count; /**< interrupt 条目数 */
    };

/* 各 compatible 属性契约见 docs/cn/device_tree_porting.md */

/* 编译期节点标志 */
#define DEVICE_FLAG_DIRECT 0x01 /* 直接访问 (direct), 无需运行时 struct device 实例 */

    /* ── VFS 操作表 ── */
    struct file_operations
    {
        int (*init)(struct device* pdev); /**< 设备初始化 */
        int (*open)(struct device* pdev, void* arg); /**< 打开设备 */
        int (*close)(struct device* pdev); /**< 关闭设备 */
        int (*write)(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms); /**< 写数据 */
        int (*read)(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms); /**< 读数据 */
        int (*ioctl)(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms); /**< 控制命令 */
        int (*suspend)(struct device* pdev); /**< 挂起设备 */
        int (*resume)(struct device* pdev); /**< 恢复设备 */
    };

    /* ── 运行时设备实例 ── */
    struct device
    {
        const struct device_node* node; /**< 指向编译期节点 */
        enum device_status status; /**< 运行时状态 */
        void* priv_data; /**< 驱动私有数据 (VFS 层) */
        const struct file_operations* ops; /**< 操作函数表 */
        struct osal_mutex* lock; /**< per-device 递归锁 (create_static_recursive) */
        struct dev_lifecycle lc; /**< 驱动 I/O 生命周期 (probe 时 device_lc_bind) */
        void* platform_data; /**< board 层注入的静态数据, probe 前设置 */
    };

    /* ── 查找设备 ── */
    /**
     * @brief 按名称查找设备
     * @param[in] name 设备名称
     * @return 设备实例指针; 未找到返回 NULL
     */
    struct device* device_find(const char* name);
    /**
     * @brief 按 DTS label 查找设备
     * @param[in] label DTS label (如 pwm_backlight)
     * @return 设备实例指针; 未找到返回 NULL
     */
    struct device* device_find_by_label(const char* label);
    /**
     * @brief 按 compatible 字符串查找设备
     * @param[in] compatible compatible 字符串
     * @return 设备实例指针; 未找到返回 NULL
     */
    struct device* device_find_by_compatible(const char* compatible);
    /**
     * @brief 按设备 ID 查找设备
     * @param[in] id 设备 ID (device_id_t)
     * @return 设备实例指针; 未找到返回 NULL
     */
    struct device* device_find_by_id(device_id_t id);
    /**
     * @brief 按 DTS 全路径查找设备
     * @param[in] path DTS 全路径 (如 /soc/spi2@0)
     * @return 设备实例指针; 未找到返回 NULL
     */
    struct device* device_find_by_path(const char* path) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 获取设备父节点
     * @param[in] pdev 设备对象指针
     * @return 父设备实例指针; 无父节点返回 NULL
     */
    struct device* device_get_parent(const struct device* pdev);

    /* ── 从属性中解析 phandle 引用并返回目标设备 ── */
    /**
     * @brief 解析属性中的 phandle 引用并返回目标设备
     * @param[in] pdev 设备对象指针
     * @param[in] key 属性键名 (含 phandle 引用)
     * @return 目标设备实例指针; 解析失败返回 NULL
     */
    struct device* device_get_phandle_dev(const struct device* pdev, const char* key) DEVICE_WARN_UNUSED_RESULT;

    /* ── 读取属性（从 pdev->node 读取） ── */
    /**
     * @brief 读取 int 属性
     * @param[in] pdev 设备对象指针
     * @param[in] key 属性键名
     * @param[out] val 回传属性值
     * @return 成功返回 MINI_OK, 属性缺失返回 MINI_ERR_NODEV
     */
    int device_get_prop_int(const struct device* pdev, const char* key, int* val) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 读取 int 数组属性
     * @param[in] pdev 设备对象指针
     * @param[in] key 属性键名
     * @param[out] out_arr 回传数组
     * @param[in] max_len 数组容量
     * @return 成功返回 MINI_OK, 属性缺失或超长返回 MINI_ERR_NODEV
     */
    int device_get_prop_int_array(const struct device* pdev, const char* key, int* out_arr, int max_len) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 读取字符串属性
     * @param[in] pdev 设备对象指针
     * @param[in] key 属性键名
     * @param[out] val 回传字符串指针 (指向节点只读内存)
     * @return 成功返回 MINI_OK, 属性缺失返回 MINI_ERR_NODEV
     */
    int device_get_prop_str(const struct device* pdev, const char* key, const char** val) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 读取 bool 属性
     * @param[in] pdev 设备对象指针
     * @param[in] key 属性键名
     * @param[out] val 回传布尔值 (0/1)
     * @return 成功返回 MINI_OK, 属性缺失返回 MINI_ERR_NODEV
     */
    int device_get_prop_bool(const struct device* pdev, const char* key, int* val) DEVICE_WARN_UNUSED_RESULT;

    /**
     * @brief 获取设备名称
     * @param[in] pdev 设备对象指针
     * @return 设备名称字符串
     */
    const char* device_get_name(const struct device* pdev);
    /**
     * @brief 获取设备 compatible 字符串
     * @param[in] pdev 设备对象指针
     * @return compatible 字符串
     */
    const char* device_get_compatible(const struct device* pdev);
    /**
     * @brief 获取设备运行时状态
     * @param[in] pdev 设备对象指针
     * @return device_status 状态
     */
    enum device_status device_get_status(const struct device* pdev);
    /**
     * @brief 获取设备关键性等级
     * @param[in] pdev 设备对象指针
     * @return device_criticality 等级
     */
    enum device_criticality device_get_criticality(const struct device* pdev);

    /* ── 读取第 idx 条 reg 条目（按 #address-cells / #size-cells 分组） ── */
    /**
     * @brief 读取第 idx 条 reg 条目
     * @param[in] pdev 设备对象指针
     * @param[in] idx reg 条目索引
     * @param[out] out 回传 reg 条目指针
     * @return 成功返回 MINI_OK, 索引越界返回 MINI_ERR_INVAL
     */
    int device_get_reg(const struct device* pdev, int idx, const struct device_reg** out) DEVICE_WARN_UNUSED_RESULT;

    /* ── 读取第 idx 条 interrupt 条目（按 #interrupt-cells 分组） ── */
    /**
     * @brief 读取第 idx 条 interrupt 条目
     * @param[in] pdev 设备对象指针
     * @param[in] idx interrupt 条目索引
     * @param[out] out 回传 interrupt 条目指针
     * @return 成功返回 MINI_OK, 索引越界返回 MINI_ERR_INVAL
     */
    int device_get_irq(const struct device* pdev, int idx, const struct device_irq** out) DEVICE_WARN_UNUSED_RESULT;

    /* ── 运行时状态管理 ── */
    /**
     * @brief 设置设备状态
     * @param[in] pdev 设备对象指针
     * @param[in] status 设备状态
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int device_set_status(struct device* pdev, enum device_status status) DEVICE_WARN_UNUSED_RESULT;

    /**
     * @brief 设置设备私有数据
     * @param[in] pdev 设备对象指针
     * @param[in] priv 私有数据指针
     * @return 成功返回 MINI_OK, 失败返回 MINI_ERR_INVAL
     */
    int device_set_priv(struct device* pdev, void* priv) DEVICE_WARN_UNUSED_RESULT;

    /**
     * @brief 获取设备私有数据
     * @param[in] pdev 设备对象指针
     * @return 私有数据指针
     */
    void* device_get_priv(const struct device* pdev);

    /* ── 设备遍历 ── */
    /**
     * @brief 获取首个设备 (遍历起点)
     * @return 首个设备指针; 无设备返回 NULL
     */
    struct device* device_get_first(void);
    /**
     * @brief 获取下一个设备
     * @param[in] prev 当前设备指针
     * @return 下一设备指针; 遍历结束返回 NULL
     */
    struct device* device_get_next(const struct device* prev);
    /**
     * @brief 查询设备总数
     * @return 设备数量
     */
    int device_get_count(void);

    /* ── 设备树加载 ── */
    /**
     * @brief 加载设备树并全量静态分配设备实例
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_tree_init(void) DEVICE_WARN_UNUSED_RESULT;

    /* ── 设备锁（device_tree_init 中已完成全量静态分配） ── */
    /**
     * @brief 加锁设备 (per-device 递归锁)
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_lock(struct device* pdev) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 解锁设备
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_unlock(struct device* pdev) DEVICE_WARN_UNUSED_RESULT;

    /* ── 驱动卸载清理 ──
     * 清除 pdev->priv_data + pdev->ops, 切断幽灵指针链.
     * 由 driver remove 函数在最后调用, 替代手写 device_set_priv(pdev,NULL)+pdev->ops=NULL.
     */
    /**
     * @brief 注销设备操作: 清除 priv_data 与 ops, 切断幽灵指针链
     * @param[in] pdev 设备对象指针
     */
    void device_ops_unregister(struct device* pdev);

    /* ── 驱动 I/O 生命周期 (dev_lifecycle 绑定在 struct device 上) ── */
    /**
     * @brief 获取设备生命周期状态机指针
     * @param[in] pdev 设备对象指针
     * @return 生命周期状态机指针
     */
    struct dev_lifecycle* device_lc(struct device* pdev);
    /**
     * @brief 绑定设备生命周期状态机 (probe 时调用)
     * @param[in] pdev 设备对象指针
     */
    void device_lc_bind(struct device* pdev);

    /* ── VFS 便捷包装（框架层自动持锁） ──
     * device_open/close/suspend/resume + device_write/read/ioctl 均在持锁状态下
     * 完成状态检查与 ops 调用, 确保 check-then-act 的原子性.
     */
    /**
     * @brief 打开设备 (持锁)
     * @param[in] pdev 设备对象指针
     * @param[in] arg 打开参数 (可为 NULL)
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_open(struct device* pdev, void* arg) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 关闭设备 (持锁)
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_close(struct device* pdev) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 写数据 (持锁)
     * @param[in] pdev 设备对象指针
     * @param[in] buf 发送缓冲区
     * @param[in] len 发送长度
     * @param[in] timeout_ms 超时毫秒数
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_write(struct device* pdev, const void* buf, size_t len, uint32_t timeout_ms) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 读数据 (持锁)
     * @param[in] pdev 设备对象指针
     * @param[out] buf 接收缓冲区
     * @param[in] len 接收长度
     * @param[in] timeout_ms 超时毫秒数
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_read(struct device* pdev, void* buf, size_t len, uint32_t timeout_ms) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 执行控制命令 (持锁)
     * @param[in] pdev 设备对象指针
     * @param[in] cmd 控制命令
     * @param[in] arg 命令参数
     * @param[in] arg_len 参数长度
     * @param[in] timeout_ms 超时毫秒数
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len, uint32_t timeout_ms) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 挂起设备 (持锁)
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_suspend(struct device* pdev) DEVICE_WARN_UNUSED_RESULT;
    /**
     * @brief 恢复设备 (持锁)
     * @param[in] pdev 设备对象指针
     * @return 成功返回 MINI_OK, 失败返回负数错误码
     */
    int device_resume(struct device* pdev) DEVICE_WARN_UNUSED_RESULT;

#ifdef __cplusplus
}
#endif

#endif /* BOARD_DEVICE_H */
