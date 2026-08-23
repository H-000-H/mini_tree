/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file vfs-usb.c
 *@brief vfs-usb 实现
 *@author H-000-H
 *@details
 *   @=========================================================================================================================*
 *   USB VFS 实现 — host + CDC/ECM/HID clients
 *   Host: 解析 DTSI → usb_bus_host_init
 *   Client: 注册 fops; write/read 带 xfer_mode; ioctl 切换 AUTO/POLL/DMA
 *   @=========================================================================================================================
 */

#define USB_VFS_IMPL
#define USB_BUS_IMPL
#include "vfs-usb.h"

#include "board_define_usb.h"
#include "compiler_compat.h"
#include "dev_lifecycle.h"
#include "device.h"
#include "driver.h"
#include "dt_config_gen.h"
#include "osal.h"
#include "status.h"
#include "system_log.h"
#include "usb_bus.h"
#include <string.h>

/*============================================================================*/
/* Host VFS */
/*============================================================================*/
/* 池大小宏见 board_define_usb.h (数量由 DTS 节点数自动生成) */

struct vfs_usb_priv
{
    struct hal_usb_bus_config cfg;
    int pool_idx;
};

static struct vfs_usb_priv s_usb_priv_pool[USB_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_usb_priv_used[USB_VFS_PRIV_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_usb_priv_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_host_tag = "usb_host_vfs";

/** 资源池初始化 (pre_execution 阶段, 供 device 池复用) */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void vfs_usb_priv_pool_init(void)
{
    COMPAT_IGNORE_RESULT(
        osal_pool_init(&s_usb_priv_pool_ctrl, s_usb_priv_used, USB_VFS_PRIV_COUNT));
}

/**
 * @brief 从 device 树解析 USB 主机硬件配置 (DTSI 直投到 hal_usb_bus_config)
 * @param[in] pdev device 指针
 * @param[out] cfg 回传解析后的 USB 主机配置
 * @return 成功返回 MINI_OK, 关键属性缺失返回 MINI_ERR_INVAL
 */
static int vfs_usb_priv_parse_dts(struct device* pdev, struct hal_usb_bus_config* cfg)
{
    int usb_base = 0, rhport = 0, irqn = 0, vbus = 0, dma_en = 0;
    int dp_port = 0, dp_pin = 0, dp_af = 0;
    int dm_port = 0, dm_pin = 0, dm_af = 0;

    if (device_get_prop_int(pdev, "usb-base", &usb_base) != MINI_OK ||
        device_get_prop_int(pdev, "rhport", &rhport) != MINI_OK ||
        device_get_prop_int(pdev, "irqn", &irqn) != MINI_OK ||
        device_get_prop_int(pdev, "dp-port", &dp_port) != MINI_OK ||
        device_get_prop_int(pdev, "dp-pin", &dp_pin) != MINI_OK ||
        device_get_prop_int(pdev, "dp-af", &dp_af) != MINI_OK ||
        device_get_prop_int(pdev, "dm-port", &dm_port) != MINI_OK ||
        device_get_prop_int(pdev, "dm-pin", &dm_pin) != MINI_OK ||
        device_get_prop_int(pdev, "dm-af", &dm_af) != MINI_OK)
        return MINI_ERR_INVAL;

    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "vbus-sense", &vbus));
    COMPAT_IGNORE_RESULT(device_get_prop_int(pdev, "dma-enable", &dma_en));

    COMPAT_MEM_SET(cfg, 0, sizeof(*cfg));
    cfg->usb_base = (uintptr_t)usb_base;
    cfg->rhport = rhport;
    cfg->irqn = irqn;
    cfg->vbus_sense = vbus;
    cfg->dma_cfg.dma_enable = (uint32_t)(dma_en ? 1 : 0);
    cfg->dp.port = (uintptr_t)dp_port;
    cfg->dp.pin = (uint32_t)dp_pin;
    cfg->dp.af = (uint32_t)dp_af;
    cfg->dm.port = (uintptr_t)dm_port;
    cfg->dm.pin = (uint32_t)dm_pin;
    cfg->dm.af = (uint32_t)dm_af;
    return MINI_OK;
}

/**
 * @brief USB 主机 device probe: 解析 DTS + 初始化总线 + 绑定私有数据
 * @param[in] pdev device 指针
 * @return 成功返回 MINI_OK, 资源不足返回 MINI_ERR_NOMEM, 失败返回负数错误码
 */
static int vfs_usb_priv_probe(struct device* pdev)
{
    struct vfs_usb_priv* priv;
    int pool_idx, ret;

    if (!pdev)
        return MINI_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_usb_priv_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;

    priv = &s_usb_priv_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;

    ret = vfs_usb_priv_parse_dts(pdev, &priv->cfg);
    if (ret != MINI_OK)
        goto err_pool;

    ret = usb_bus_host_init(pdev, &priv->cfg);
    if (ret != MINI_OK)
        goto err_pool;

    if (device_set_priv(pdev, priv) != MINI_OK)
    {
        ret = MINI_ERR_IO;
        goto err_bus;
    }

    SYS_LOGI(k_host_tag, "probe OK: %s", device_get_name(pdev));
    return MINI_OK;

err_bus:
    COMPAT_IGNORE_RESULT(usb_bus_host_deinit(pdev));
err_pool:
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_usb_priv_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief USB 主机 device remove: 排空生命周期后反初始化总线并释放池槽
 * @param[in] pdev device 指针
 * @return 成功返回 MINI_OK, 排空/反初始化失败返回负数错误码
 */
static int vfs_usb_priv_remove(struct device* pdev)
{
    struct vfs_usb_priv* priv;
    struct dev_lifecycle* lc;
    int pool_idx, ret;

    if (!pdev)
        return MINI_ERR_INVAL;
    priv = (struct vfs_usb_priv*)device_get_priv(pdev);
    if (IS_ERR(priv))
        return PTR_ERR(priv);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }

    ret = usb_bus_host_deinit(pdev);
    if (ret != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return ret;
    }

    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_usb_priv_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

/*============================================================================*/
/* Client VFS */
/*============================================================================*/
/* client 池宏见 board_define_usb.h */

struct usb_vfs_client
{
    struct file_operations ops;
    enum usb_client_class cls;
    uint32_t xfer_mode; /**< USB_XFER_*; write/read 默认 AUTO */
    int pool_idx;
};

static struct usb_vfs_client s_client_pool[USB_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static uint8_t s_client_used[USB_VFS_CLIENT_COUNT] COMPAT_ALIGNED(4);
static osal_pool_t s_client_pool_ctrl COMPAT_ALIGNED(4);
static const char* const k_client_tag = "usb_client_vfs";

/** 客户端池初始化 (pre_execution 阶段) */
pre_execution(PRE_EXEC_PRIO_RES_POOL) static void vfs_usb_client_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_client_pool_ctrl, s_client_used, USB_VFS_CLIENT_COUNT));
}

/**
 * @brief VFS open 回调: 打开 USB 总线 (引用计数 +1)
 * @param[in] pdev device 指针
 * @param[in] arg 打开参数 (未用)
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
static int usb_vfs_open(struct device* pdev, void* arg)
{
    COMPAT_IGNORE_RESULT(arg);
    return usb_bus_open(pdev);
}

/**
 * @brief VFS close 回调: 关闭 USB 总线 (引用计数 -1)
 * @param[in] pdev device 指针
 * @return 成功返回 MINI_OK, 失败返回负数错误码
 */
static int usb_vfs_close(struct device* pdev) { return usb_bus_close(pdev); }

/**
 * @brief VFS write 回调: 按客户端类型分发 CDC/ECM/HID 写
 * @param[in] pdev device 指针
 * @param[in] buffer 发送缓冲区
 * @param[in] len 发送长度
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 MINI_OK, 客户端类型非法返回 MINI_ERR_INVAL
 */
static int usb_vfs_write(struct device* pdev, const void* buffer, size_t len, uint32_t timeout_ms)
{
    struct usb_vfs_client* priv;

    if (!pdev || !pdev->ops || !buffer)
        return MINI_ERR_INVAL;
    priv = container_of(pdev->ops, struct usb_vfs_client, ops);

    switch (priv->cls)
    {
    case USB_CLIENT_CDC:
        return usb_bus_cdc_write(pdev, buffer, len, timeout_ms, priv->xfer_mode);
    case USB_CLIENT_ECM:
        return usb_bus_ecm_write(pdev, buffer, len, timeout_ms, priv->xfer_mode);
    case USB_CLIENT_HID:
        return usb_bus_hid_write(pdev, buffer, len, timeout_ms, priv->xfer_mode);
    default:
        return MINI_ERR_INVAL;
    }
}

/**
 * @brief VFS read 回调: 按客户端类型分发 CDC/ECM 读 (HID 不支持读)
 * @param[in] pdev device 指针
 * @param[out] buffer 接收缓冲区
 * @param[in] len 接收长度
 * @param[in] timeout_ms 超时毫秒数
 * @return 成功返回 MINI_OK, 客户端类型非法返回 MINI_ERR_INVAL, HID 返回 MINI_ERR_NOTSUPP
 */
static int usb_vfs_read(struct device* pdev, void* buffer, size_t len, uint32_t timeout_ms)
{
    struct usb_vfs_client* priv;

    if (!pdev || !pdev->ops || !buffer)
        return MINI_ERR_INVAL;
    priv = container_of(pdev->ops, struct usb_vfs_client, ops);

    switch (priv->cls)
    {
    case USB_CLIENT_CDC:
        return usb_bus_cdc_read(pdev, buffer, len, timeout_ms, priv->xfer_mode);
    case USB_CLIENT_ECM:
        return usb_bus_ecm_read(pdev, buffer, len, timeout_ms, priv->xfer_mode);
    case USB_CLIENT_HID:
        return MINI_ERR_NOTSUPP;
    default:
        return MINI_ERR_INVAL;
    }
}

/**
 * @brief ioctl USB_SET_XFER_MODE: 设置客户端传输模式 (含 DMA 能力校验)
 * @param[in] pdev device 指针
 * @param[in] arg usb_xfer_mode_arg 参数包
 * @param[in] arg_len 参数长度
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
static int usb_cmd_set_xfer_mode(struct device* pdev, void* arg, size_t arg_len)
{
    struct usb_vfs_client* priv;
    const struct usb_xfer_mode_arg* ma = (const struct usb_xfer_mode_arg*)arg;
    int mode;

    if (!pdev || !pdev->ops || !ma || arg_len < sizeof(*ma))
        return MINI_ERR_INVAL;
    if (ma->xfer_mode > USB_XFER_DMA)
        return MINI_ERR_INVAL;

    /* 强制 DMA 时校验 host dma-enable */
    mode = usb_bus_resolve_xfer_mode(pdev, ma->xfer_mode);
    if (mode < 0)
        return mode;

    priv = container_of(pdev->ops, struct usb_vfs_client, ops);
    priv->xfer_mode = ma->xfer_mode;
    return MINI_OK;
}

/**
 * @brief ioctl USB_GET_XFER_MODE: 读取客户端当前传输模式
 * @param[in] pdev device 指针
 * @param[out] arg usb_xfer_mode_arg 参数包 (回传 xfer_mode)
 * @param[in] arg_len 参数长度
 * @return 成功返回 MINI_OK, 参数非法返回 MINI_ERR_INVAL
 */
static int usb_cmd_get_xfer_mode(struct device* pdev, void* arg, size_t arg_len)
{
    struct usb_vfs_client* priv;
    struct usb_xfer_mode_arg* ma = (struct usb_xfer_mode_arg*)arg;

    if (!pdev || !pdev->ops || !ma || arg_len < sizeof(*ma))
        return MINI_ERR_INVAL;
    priv = container_of(pdev->ops, struct usb_vfs_client, ops);
    ma->xfer_mode = priv->xfer_mode;
    return MINI_OK;
}

/**
 * @brief VFS ioctl 回调: 分派 USB_CMD_SET/GET_XFER_MODE
 * @param[in] pdev device 指针
 * @param[in] cmd ioctl 命令
 * @param[in] arg 命令参数
 * @param[in] arg_len 参数长度
 * @param[in] timeout_ms 超时毫秒数 (未用)
 * @return 成功返回 MINI_OK, 未知命令返回 MINI_ERR_NOTSUPP
 */
static int usb_vfs_ioctl(struct device* pdev, int cmd, void* arg, size_t arg_len,
                         uint32_t timeout_ms)
{
    COMPAT_IGNORE_RESULT(timeout_ms);
    if (cmd == USB_CMD_SET_XFER_MODE)
        return usb_cmd_set_xfer_mode(pdev, arg, arg_len);
    if (cmd == USB_CMD_GET_XFER_MODE)
        return usb_cmd_get_xfer_mode(pdev, arg, arg_len);
    return MINI_ERR_NOTSUPP;
}

static const struct file_operations s_usb_fops_template = {
    .open = usb_vfs_open,
    .close = usb_vfs_close,
    .write = usb_vfs_write,
    .read = usb_vfs_read,
    .ioctl = usb_vfs_ioctl,
};

/**
 * @brief 注册指定 USB 客户端类 (CDC/ECM/HID): 申请池槽 + 绑定 bus client
 * @param[in] pdev device 指针
 * @param[in] cls 客户端类 (USB_CLIENT_*)
 * @return 成功返回 MINI_OK, 资源不足返回 MINI_ERR_NOMEM, 失败返回负数错误码
 */
static int usb_vfs_client_probe_cls(struct device* pdev, enum usb_client_class cls)
{
    struct usb_vfs_client* priv;
    struct usb_bus_client* bus_cli;
    int pool_idx, ret;

    if (!pdev)
        return MINI_ERR_INVAL;

    pool_idx = osal_pool_claim(&s_client_pool_ctrl);
    if (pool_idx < 0)
        return MINI_ERR_NOMEM;

    priv = &s_client_pool[pool_idx];
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    priv->pool_idx = pool_idx;
    priv->cls = cls;
    priv->xfer_mode = USB_XFER_AUTO; /* write/read 默认 AUTO */
    priv->ops = s_usb_fops_template;

    ret = usb_bus_client_register(pdev, cls, &bus_cli);
    if (ret != MINI_OK)
        goto err_pool;

    pdev->ops = &priv->ops;
    if (device_set_priv(pdev, priv) != MINI_OK)
    {
        usb_bus_client_unregister(pdev);
        ret = MINI_ERR_IO;
        goto err_pool;
    }

    SYS_LOGI(k_client_tag, "probe OK: %s cls=%d", device_get_name(pdev), (int)cls);
    return MINI_OK;

err_pool:
    pdev->ops = NULL;
    dev_lc_reset(device_lc(pdev));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    return ret;
}

/**
 * @brief USB 客户端 device remove: 注销 bus client + 排空生命周期 + 释放池槽
 * @param[in] pdev device 指针
 * @return 成功返回 MINI_OK, 排空失败返回负数错误码
 */
static int usb_vfs_client_remove(struct device* pdev)
{
    struct usb_vfs_client* priv;
    struct dev_lifecycle* lc;
    int pool_idx;

    if (!pdev || !pdev->ops)
        return MINI_ERR_INVAL;

    priv = container_of(pdev->ops, struct usb_vfs_client, ops);
    lc = device_lc(pdev);
    if (IS_ERR(lc))
        return PTR_ERR(lc);

    pool_idx = priv->pool_idx;
    dev_lc_remove_start(lc);
    device_ops_unregister(pdev);
    if (dev_lc_remove_drain(lc, OSAL_WAIT_FOREVER) != MINI_OK)
    {
        dev_lc_remove_finish(lc);
        return MINI_ERR_IO;
    }

    usb_bus_client_unregister(pdev);
    COMPAT_MEM_SET(priv, 0, sizeof(*priv));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_client_pool_ctrl, pool_idx));
    dev_lc_remove_finish(lc);
    return MINI_OK;
}

/** CDC ACM 客户端 probe (DRIVER_REGISTER) */
static int usb_cdc_probe(struct device* pdev)
{
    return usb_vfs_client_probe_cls(pdev, USB_CLIENT_CDC);
}
/** CDC-ECM 客户端 probe (DRIVER_REGISTER) */
static int usb_ecm_probe(struct device* pdev)
{
    return usb_vfs_client_probe_cls(pdev, USB_CLIENT_ECM);
}
/** HID 客户端 probe (DRIVER_REGISTER) */
static int usb_hid_probe(struct device* pdev)
{
    return usb_vfs_client_probe_cls(pdev, USB_CLIENT_HID);
}

DRIVER_REGISTER(usb_otg_host, "usb-otg-host", vfs_usb_priv_probe, vfs_usb_priv_remove)
DRIVER_REGISTER(usb_cdc_acm, "heterogeneous,usb-cdc-acm", usb_cdc_probe, usb_vfs_client_remove)
DRIVER_REGISTER(usb_cdc_ecm, "heterogeneous,usb-cdc-ecm", usb_ecm_probe, usb_vfs_client_remove)
DRIVER_REGISTER(usb_hid, "heterogeneous,usb-hid", usb_hid_probe, usb_vfs_client_remove)
