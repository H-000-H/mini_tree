/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * CAN VFS 实现 : Host + Client
 *
 * DTS:
 *   can@n (can-host)                    ← host
 *   └── can-client (heterogeneous,can-client) ← client (fops)
 *
 * write/read: struct can_frame; 一律经 can_hook 弱钩子 (无强符号=普通 Classic CAN)
 * ioctl: TRANSFER / SET_FILTER / GET_STATE
 *@=========================================================================================================================*/
#define CAN_VFS_IMPL
#include "vfs-can.h"

#include "can_bus.h"
#include "can_hook.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "board_define_can.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"

/*===========================================================================================================================================================*/
/*Host VFS*/
/*===========================================================================================================================================================*/
/* 池大小宏见 board_define_can.h (数量由 DTS 节点数自动生成) */

/** @brief CAN Host 私有数据 (静态池, 存 host 配置 + 池索引) */
struct vfs_can_priv
{
    struct hal_can_bus_config cfg; /**< host 总线配置 (DTSI 直投) */
    int pool_idx; /**< 池索引 */
};

static struct vfs_can_priv s_can_priv_pool[CAN_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_can_priv_used[CAN_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_can_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_host_tag = "can_vfs_host";

/**
 * @brief CAN Host 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void vfs_can_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_can_priv_pool_ctrl, s_can_priv_used, CAN_VFS_PRIV_COUNT));
}

/**
 * @brief 解析 CAN Host DTS 属性, 填入 hal_can_bus_config
 * @param pdev 设备对象指针
 * @param cfg 配置结构指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_can_priv_parse_dts(struct device* pdev, struct hal_can_bus_config* cfg)
{
    int can_base = 0, can_clk = 0;
    int tx_port = 0, tx_pin = 0, tx_clk = 0, tx_af = 0;
    int rx_port = 0, rx_pin = 0, rx_clk = 0, rx_af = 0;
    int tx_output_type = 0, tx_speed = 0, tx_mode = 0, tx_pull = 0;
    int rx_output_type = 0, rx_speed = 0, rx_mode = 0, rx_pull = 0;

    if (device_get_prop_int(pdev, "can-base", &can_base) != VFS_OK ||
        device_get_prop_int(pdev, "can-clk", &can_clk) != VFS_OK ||
        device_get_prop_int(pdev, "tx-port", &tx_port) != VFS_OK ||
        device_get_prop_int(pdev, "tx-pin", &tx_pin) != VFS_OK ||
        device_get_prop_int(pdev, "tx-clk", &tx_clk) != VFS_OK ||
        device_get_prop_int(pdev, "tx-af", &tx_af) != VFS_OK ||
        device_get_prop_int(pdev, "rx-port", &rx_port) != VFS_OK ||
        device_get_prop_int(pdev, "rx-pin", &rx_pin) != VFS_OK ||
        device_get_prop_int(pdev, "rx-clk", &rx_clk) != VFS_OK ||
        device_get_prop_int(pdev, "rx-af", &rx_af) != VFS_OK)
        return VFS_ERR_INVAL;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-output-type", &tx_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-speed", &tx_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-mode", &tx_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-pull", &tx_pull));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-output-type", &rx_output_type));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-speed", &rx_speed));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-mode", &rx_mode));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-pull", &rx_pull));

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    {
        int irqn = -1, irq_priority = 0, it_enable = 0;
        int prescaler = 16, mode = 0, sjw = 0, bs1 = 0, bs2 = 0;
        int auto_bus_off = 0, auto_wakeup = 0, auto_retransmit = 0;
        int rx_fifo_locked = 0, tx_fifo_prio = 0, tt_mode = 0;

        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irqn", &irqn));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "irq-priority", &irq_priority));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "it-enable", &it_enable));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "prescaler", &prescaler));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "mode", &mode));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "sjw", &sjw));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "bs1", &bs1));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "bs2", &bs2));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "auto-bus-off", &auto_bus_off));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "auto-wakeup", &auto_wakeup));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "auto-retransmit", &auto_retransmit));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "rx-fifo-locked", &rx_fifo_locked));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tx-fifo-prio", &tx_fifo_prio));
        COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "tt-mode", &tt_mode));

        cfg->irqn = (int32_t)irqn;
        cfg->irq_priority = (uint32_t)irq_priority;
        cfg->it_enable = (uint32_t)it_enable;
        cfg->prescaler = (uint32_t)prescaler;
        cfg->mode = (uint32_t)mode;
        cfg->sjw = (uint32_t)sjw;
        cfg->bs1 = (uint32_t)bs1;
        cfg->bs2 = (uint32_t)bs2;
        cfg->auto_bus_off = (uint32_t)auto_bus_off;
        cfg->auto_wakeup = (uint32_t)auto_wakeup;
        cfg->auto_retransmit = (uint32_t)auto_retransmit;
        cfg->rx_fifo_locked = (uint32_t)rx_fifo_locked;
        cfg->tx_fifo_prio = (uint32_t)tx_fifo_prio;
        cfg->tt_mode = (uint32_t)tt_mode;
    }

    cfg->can = (uintptr_t)can_base;
    cfg->can_clk_periph = (uint32_t)can_clk;
    cfg->tx = (struct hal_can_pin_cfg){
        .port = (uintptr_t)tx_port,
        .pin = (uint16_t)tx_pin,
        .clk_bus = (uint32_t)tx_clk,
        .af = (uint32_t)tx_af,
        .output_type = (uint32_t)tx_output_type,
        .speed = (uint32_t)tx_speed,
        .mode = (uint32_t)tx_mode,
        .pull = (uint32_t)tx_pull,
    };
    cfg->rx = (struct hal_can_pin_cfg){
        .port = (uintptr_t)rx_port,
        .pin = (uint16_t)rx_pin,
        .clk_bus = (uint32_t)rx_clk,
        .af = (uint32_t)rx_af,
        .output_type = (uint32_t)rx_output_type,
        .speed = (uint32_t)rx_speed,
        .mode = (uint32_t)rx_mode,
        .pull = (uint32_t)rx_pull,
    };

    return VFS_OK;
}

/**
 * @brief CAN Host 探测: 分配私有池, 解析 DTS, 初始化总线
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_can_priv_probe(struct device* pdev)
{
    struct vfs_can_priv* priv;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_can_priv_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_can_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = vfs_can_priv_parse_dts(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    ret = can_bus_host_init(pdev, &priv->cfg);
    if (ret != VFS_OK)
        goto err_pool;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        ret = VFS_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(k_host_tag, "probe OK: %s", device_get_name(pdev));
    return VFS_OK;

err_bus:
    COMPAT_IGNORE_RESULT(can_bus_host_deinit(pdev));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_can_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief CAN Host 移除: remove_start → 排空 IO → host_deinit → 释放私有池
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int vfs_can_priv_remove(struct device* pdev)
{
    struct vfs_can_priv* priv;
    struct dev_lifecycle* lc;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    priv = (struct vfs_can_priv*)device_get_priv(pdev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    ret = can_bus_host_deinit(pdev);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_host_tag, "host remove busy: %s (ret=%d)", device_get_name(pdev), ret);
        dev_lc_remove_finish(lc);
        return ret;
    }

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_can_priv_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

/*===========================================================================================================================================================*/
/*Client VFS*/
/*===========================================================================================================================================================*/
/* client 池宏见 board_define_can.h */

/** @brief CAN Client 运行时对象 (静态池, 含 fops + 池索引) */
struct can_vfs_client
{
    struct file_operations ops; /**< VFS 操作表 */
    int pool_idx; /**< 池索引 */
};

static struct can_vfs_client s_client_pool[CAN_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_client_used[CAN_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_client_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_client_tag = "can_vfs_client";

/**
 * @brief CAN Client 私有数据池启动初始化
 */
pre_execution(PRE_EXEC_PRIO_DRIVER_POOL) static void can_vfs_client_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, CAN_VFS_CLIENT_COUNT));
}

/**
 * @brief CAN Client 打开: bus_open → can_hook_on_open (弱钩子, 无覆盖即透传)
 */
static int can_vfs_open(struct device* pdev, void* arg)
{
    struct dev_lifecycle* lc;
    int first;
    int ret;

    COMPAT_IGNORE_RESULT(arg);
    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    first = dev_lc_open_begin(lc);
    if (first < 0)
        return first;

    ret = VFS_OK;
    if (first == 1)
    {
        ret = can_bus_open(pdev);
        if (ret != VFS_OK)
        {
            COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));
            dev_lc_open_abort(lc);
        }
        else
        {
            ret = can_hook_on_open(pdev);
            if (ret != VFS_OK)
            {
                COMPAT_IGNORE_RESULT(can_bus_close(pdev));
                COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));
                dev_lc_open_abort(lc);
            }
        }
    }
    if (ret == VFS_OK)
        dev_lc_open_end(lc);
    return ret;
}

/**
 * @brief CAN Client 关闭: can_hook_on_close → bus_close (hook 失败只记账, 仍关硬件)
 */
static int can_vfs_close(struct device* pdev)
{
    struct dev_lifecycle* lc;
    int last;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    last = dev_lc_close_begin(lc);
    if (last < 0)
        return last;

    if (last)
    {
        int fn_ret = can_hook_on_close(pdev);
        if (fn_ret != VFS_OK)
            COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, fn_ret));
        COMPAT_IGNORE_RESULT(can_bus_close(pdev));
    }
    dev_lc_close_end(lc);
    return VFS_OK;
}

/** RX 钩子: filter_match 拒绝 → AGAIN; 通过后 on_rx */
static int can_vfs_apply_rx_hooks(struct device* pdev, struct can_frame* frame)
{
    int ret;

    ret = can_hook_filter_match(pdev, frame);
    if (ret != VFS_OK)
        return VFS_ERR_AGAIN;

    return can_hook_on_rx(pdev, frame);
}

/** TX: pre_tx → bus_transmit → post_tx */
static int can_vfs_do_tx(struct device* pdev, struct can_frame* frame, uint32_t timeout_ms)
{
    int ret;

    ret = can_hook_pre_tx(pdev, frame);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));
        return ret;
    }

    ret = can_bus_transmit(pdev, frame, timeout_ms);
    if (ret != VFS_OK)
        COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));

    return can_hook_post_tx(pdev, frame, ret);
}

/** RX: bus_receive → filter_match → on_rx */
static int can_vfs_do_rx(struct device* pdev, struct can_frame* frame, uint32_t fifo,
                         uint32_t timeout_ms)
{
    int ret;

    ret = can_bus_receive(pdev, frame, fifo, timeout_ms);
    if (ret != VFS_OK)
    {
        COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));
        return ret;
    }

    ret = can_vfs_apply_rx_hooks(pdev, frame);
    if (ret != VFS_OK && ret != VFS_ERR_AGAIN)
        COMPAT_IGNORE_RESULT(can_hook_on_err(pdev, ret));
    return ret;
}

/**
 * @brief CAN Client 写: 发送单帧 (struct can_frame)
 */
static int can_vfs_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    struct can_frame local;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    if (len < sizeof(struct can_frame) || !buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    local = *(const struct can_frame*)buffer;
    ret = can_vfs_do_tx(pdev, &local, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

/**
 * @brief CAN Client 读: FIFO0 收一帧 + fn 钩子
 */
static int can_vfs_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    struct can_frame* frame;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    if (len < sizeof(struct can_frame) || !buffer)
    {
        dev_lc_io_end(lc);
        return VFS_ERR_INVAL;
    }

    frame = (struct can_frame*)buffer;
    ret = can_vfs_do_rx(pdev, frame, 0U, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

typedef int (*can_ioctl_fn_t)(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms);

struct can_ioctl_map
{
    can_ioctl_fn_t handler;
};

static int can_cmd_transfer(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct can_transfer_arg* ta = (struct can_transfer_arg*)arg;
    int ret;

    if (!pdev || !pdev->ops || !ta || arg_len != sizeof(*ta))
        return VFS_ERR_INVAL;

    ret = can_vfs_do_tx(pdev, &ta->tx, timeout_ms);
    if (ret != VFS_OK)
        return ret;

    if (ta->do_rx)
        ret = can_vfs_do_rx(pdev, &ta->rx, ta->rx_fifo, timeout_ms);

    return ret;
}

static int can_cmd_set_filter(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct can_filter_arg* fa = (struct can_filter_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !fa || arg_len != sizeof(*fa))
        return VFS_ERR_INVAL;

    return can_bus_filter_config(pdev, &fa->filter);
}

static int can_cmd_get_state(struct device* pdev, void* arg, size_t arg_len, uint32_t timeout_ms)
{
    struct can_state_arg* sa = (struct can_state_arg*)arg;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!pdev || !pdev->ops || !sa || arg_len != sizeof(*sa))
        return VFS_ERR_INVAL;

    return can_bus_get_state(pdev, &sa->state);
}

static const struct can_ioctl_map s_can_ioctl_map[CAN_CMD_COUNT] = {
    [CAN_CMD_TRANSFER - CAN_CMD_BASE - 1] = {can_cmd_transfer},
    [CAN_CMD_SET_FILTER - CAN_CMD_BASE - 1] = {can_cmd_set_filter},
    [CAN_CMD_GET_STATE - CAN_CMD_BASE - 1] = {can_cmd_get_state},
};

/**
 * @brief CAN Client ioctl 派发入口
 */
static int can_vfs_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
                         uint32_t timeout_ms)
{
    struct dev_lifecycle* lc;
    int32_t offset;
    int ret;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    ret = dev_lc_io_begin(lc);
    if (ret != VFS_OK)
        return ret;

    offset = (int32_t)cmd - (int32_t)CAN_CMD_BASE;
    if (offset < 1 || offset > CAN_CMD_COUNT || !s_can_ioctl_map[offset - 1].handler)
        ret = VFS_ERR_INVAL;
    else
        ret = s_can_ioctl_map[offset - 1].handler(pdev, arg, arg_len, timeout_ms);

    dev_lc_io_end(lc);
    return ret;
}

static const struct file_operations can_vfs_fops = {
    .open = can_vfs_open,
    .close = can_vfs_close,
    .write = can_vfs_write,
    .read = can_vfs_read,
    .ioctl = can_vfs_ioctl,
};

/**
 * @brief CAN Client 探测: 注册 fops 并绑定总线客户端
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int can_vfs_probe(struct device* pdev)
{
    struct can_vfs_client* priv;
    struct can_bus_client* bus_cli;
    int pool_idx;
    int ret;

    if (!pdev)
        return VFS_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_client_pool_ctrl);
    if (pool_idx < 0)
        return VFS_ERR_NOMEM;

    priv = &s_client_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = can_bus_client_register(pdev, &bus_cli);
    if (ret != VFS_OK)
        goto err_pool;

    priv->ops = can_vfs_fops;
    pdev->ops = &priv->ops;

    if (device_set_priv(pdev, priv) != VFS_OK)
    {
        can_bus_client_unregister(pdev);
        ret = VFS_ERR_IO;
        goto err_pool;
    }

    SYS_LOGI(k_client_tag, "probe OK: %s", device_get_name(pdev));
    return VFS_OK;

err_pool:
    pdev->ops = NULL;
    dev_lc_reset(device_lc(pdev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief CAN Client 移除: remove_start → 排空 IO → unregister → 释放私有池
 * @param pdev 设备对象指针
 * @return 成功返回 VFS_OK, 失败返回负数错误码
 */
static int can_vfs_remove(struct device* pdev)
{
    struct can_vfs_client* priv;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return VFS_ERR_INVAL;

    priv = container_of(pdev->ops, struct can_vfs_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);

    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != VFS_OK)
    {
        dev_lc_remove_finish(lc);
        return VFS_ERR_IO;
    }

    can_bus_client_unregister(pdev);
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return VFS_OK;
}

DRIVER_REGISTER(can_host, "can-host", vfs_can_priv_probe, vfs_can_priv_remove)
DRIVER_REGISTER(can_vfs_client, "heterogeneous,can-client", can_vfs_probe, can_vfs_remove)
