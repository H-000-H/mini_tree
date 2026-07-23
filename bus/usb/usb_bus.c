/* SPDX-License-Identifier: Apache-2.0 */
/*@=========================================================================================================================*
 * USB BUS 实现 — host/client 池 + TinyUSB 粘合 (usb_tusb_port)
 *
 * 静态池: s_usb_hosts[HOST_MAX] + s_usb_clients[DEV_ID_COUNT]
 * 数据流: VFS → usb_bus_* → hal_usb_* / usb_tusb_* / usb_net_frame_*
 *@=========================================================================================================================*/
#define USB_BUS_IMPL
#define HAL_USB_IMPL
#include "usb_bus.h"
#include "bus.h"
#include "hal_usb.h"
#include "device.h"
#include "board_devtable.h"
#include "status.h"
#include "compiler_compat.h"
#include "system_log.h"
#include "osal.h"
#include "usb_tusb_port.h"

#define USB_BUS_HOST_MAX 1

struct usb_bus_host
{
    struct device*          dev;
    struct hal_usb_bus_host hal_host;
    COMPAT_ATOMIC_INT       ref_count;
    int                     tusb_inited;
    uint8_t                 rhport;
};

struct usb_bus_client
{
    struct device*        dev;
    struct usb_bus_host*  host;
    enum usb_client_class cls;
    int                   hw_open;
};

static struct usb_bus_host   s_usb_hosts[USB_BUS_HOST_MAX];
static uint8_t               s_usb_host_used[USB_BUS_HOST_MAX];
static osal_pool_t           s_usb_host_pool_ctrl;
static struct usb_bus_client s_usb_clients[DEV_ID_COUNT];
static struct usb_bus_host*  s_irq_host;
static const char* const     kTag = "usb_bus";

pre_execution(150)
static void usb_bus_pool_init(void)
{
    COMPAT_IGNORE_RESULT(osal_pool_init(&s_usb_host_pool_ctrl, s_usb_host_used, USB_BUS_HOST_MAX));
}

static struct usb_bus_host* usb_host_from_device(struct device* dev)
{
    for (int i = 0; i < USB_BUS_HOST_MAX; i++)
    {
        if (osal_pool_is_used(&s_usb_host_pool_ctrl, i) && s_usb_hosts[i].dev == dev)
            return &s_usb_hosts[i];
    }
    return NULL;
}

static struct usb_bus_client* usb_client_from_device(struct device* dev)
{
    int id = (int)board_dev_find(device_get_name(dev));
    if (id < 0 || id >= DEV_ID_COUNT || !s_usb_clients[id].dev)
        return NULL;
    return &s_usb_clients[id];
}

static int  usb_host_init_impl(struct device* dev, const void* cfg);
static int  usb_host_deinit_impl(struct device* dev);
static int  usb_host_role_impl(struct device* dev);
static int  usb_client_register_impl(struct device* dev, const void* cfg, void** out);
static void usb_client_unregister_impl(struct device* dev);

static const struct bus_controller_ops s_usb_controller_ops = {
    .init              = usb_host_init_impl,
    .deinit            = usb_host_deinit_impl,
    .role              = usb_host_role_impl,
    .client_register   = usb_client_register_impl,
    .client_unregister = usb_client_unregister_impl,
};

static int usb_host_init_impl(struct device* pdev, const void* cfg)
{
    const struct hal_usb_bus_config* host_cfg;
    struct usb_bus_host* host;
    int idx, ret;

    if (!pdev || !cfg)
        return VFS_ERR_INVAL;

    host_cfg = (const struct hal_usb_bus_config*)cfg;
    if (usb_host_from_device(pdev))
        return VFS_OK;

    idx = osal_pool_claim(&s_usb_host_pool_ctrl);
    if (idx < 0)
        return VFS_ERR_NOMEM;

    host = &s_usb_hosts[idx];
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    host->dev    = pdev;
    host->rhport = (uint8_t)host_cfg->rhport;
    COMPAT_ATOMIC_RUNTIME_INIT(&host->ref_count, 0);

    ret = hal_usb_bus_host_init(&host->hal_host, host_cfg);
    if (ret != VFS_OK)
        goto fail_pool;

    if (!usb_tusb_init(host->rhport))
    {
        COMPAT_IGNORE_RESULT(hal_usb_bus_host_deinit(&host->hal_host));
        ret = VFS_ERR_IO;
        goto fail_pool;
    }
    host->tusb_inited = 1;
    s_irq_host = host;
    hal_usb_irq_enable(&host->hal_host);

    ret = bus_controller_bind_full(pdev, BUS_TYPE_USB, &s_usb_controller_ops, host);
    if (ret != VFS_OK)
    {
        hal_usb_irq_disable(&host->hal_host);
        s_irq_host = NULL;
        COMPAT_IGNORE_RESULT(hal_usb_bus_host_deinit(&host->hal_host));
        goto fail_pool;
    }

    SYS_LOGI(kTag, "host init OK rhport=%u", (unsigned)host->rhport);
    return VFS_OK;

fail_pool:
    COMPAT_MEM_SET(host, 0, sizeof(*host));
    COMPAT_IGNORE_RESULT(osal_pool_release(&s_usb_host_pool_ctrl, idx));
    return ret;
}

int usb_bus_host_init(struct device* dev, const struct hal_usb_bus_config* cfg)
{
    return usb_host_init_impl(dev, cfg);
}

static int usb_host_deinit_impl(struct device* pdev)
{
    struct usb_bus_host* host;
    int idx, ret;

    if (!pdev)
        return VFS_ERR_INVAL;
    host = usb_host_from_device(pdev);
    if (!host)
        return VFS_ERR_NODEV;
    if (COMPAT_ATOMIC_LOAD(&host->ref_count, COMPAT_MO_SEQ_CST) != 0)
        return VFS_ERR_BUSY;

    idx = (int)(host - s_usb_hosts);
    bus_controller_unbind(pdev);
    if (s_irq_host == host)
        s_irq_host = NULL;
    ret = hal_usb_bus_host_deinit(&host->hal_host);
    if (ret == VFS_OK)
    {
        COMPAT_MEM_SET(host, 0, sizeof(*host));
        COMPAT_IGNORE_RESULT(osal_pool_release(&s_usb_host_pool_ctrl, idx));
    }
    return ret;
}

int usb_bus_host_deinit(struct device* pdev)
{
    return usb_host_deinit_impl(pdev);
}

static int usb_host_role_impl(struct device* dev)
{
    COMPAT_IGNORE_RESULT(dev);
    return 0;
}

static int usb_client_register_impl(struct device* pdev, const void* cfg, void** out)
{
    struct bus_controller* ctlr;
    struct usb_bus_host* host;
    struct usb_bus_client* client;
    const enum usb_client_class* pcls;
    int id;

    if (!pdev || !out || !cfg)
        return VFS_ERR_INVAL;
    pcls = (const enum usb_client_class*)cfg;

    if (bus_controller_of(pdev, &ctlr) != VFS_OK)
        return VFS_ERR_NODEV;
    host = (struct usb_bus_host*)ctlr->hw_ctx;
    if (!host)
        return VFS_ERR_IO;

    id = (int)board_dev_find(device_get_name(pdev));
    if (id < 0 || id >= DEV_ID_COUNT)
        return VFS_ERR_INVAL;

    client = &s_usb_clients[id];
    if (client->dev)
    {
        if (client->dev != pdev)
            return VFS_ERR_BUSY;
        *out = client;
        return VFS_OK;
    }

    COMPAT_MEM_SET(client, 0, sizeof(*client));
    client->dev  = pdev;
    client->host = host;
    client->cls  = *pcls;
    (void)COMPAT_ATOMIC_FETCH_ADD(&host->ref_count, 1, COMPAT_MO_SEQ_CST);
    *out = client;
    return VFS_OK;
}

int usb_bus_client_register(struct device* pdev, enum usb_client_class cls,
                            struct usb_bus_client** out)
{
    return usb_client_register_impl(pdev, &cls, (void**)out);
}

static void usb_client_unregister_impl(struct device* pdev)
{
    struct usb_bus_client* client = usb_client_from_device(pdev);
    struct usb_bus_host* host;

    if (!client)
        return;
    if (client->hw_open)
    {
        COMPAT_IGNORE_RESULT(usb_bus_close(pdev));
        client->hw_open = 0;
    }
    host = client->host;
    if (host)
        (void)COMPAT_ATOMIC_FETCH_SUB(&host->ref_count, 1, COMPAT_MO_SEQ_CST);
    COMPAT_MEM_SET(client, 0, sizeof(*client));
}

void usb_bus_client_unregister(struct device* pdev)
{
    usb_client_unregister_impl(pdev);
}

int usb_bus_open(struct device* dev)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    if (!c)
        return VFS_ERR_NODEV;
    c->hw_open = 1;
    return VFS_OK;
}

int usb_bus_close(struct device* dev)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    if (!c)
        return VFS_ERR_NODEV;
    c->hw_open = 0;
    return VFS_OK;
}

int usb_bus_resolve_xfer_mode(struct device* client_or_host, uint32_t xfer_mode)
{
    struct usb_bus_host* host = usb_host_from_device(client_or_host);
    struct usb_bus_client* client;

    if (!host)
    {
        client = usb_client_from_device(client_or_host);
        if (!client || !client->host)
            return VFS_ERR_NODEV;
        host = client->host;
    }
    return hal_usb_resolve_xfer_mode(&host->hal_host, xfer_mode);
}

void usb_bus_task(void)
{
    if (s_irq_host && s_irq_host->tusb_inited)
        usb_tusb_task();
}

int usb_bus_cdc_write(struct device* dev, const void* buf, size_t len,
                      uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    uint32_t start;
    size_t done = 0;
    int mode;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!c || c->cls != USB_CLIENT_CDC || !buf)
        return VFS_ERR_INVAL;

    mode = usb_bus_resolve_xfer_mode(dev, xfer_mode);
    if (mode < 0)
        return mode;

    if (!usb_tusb_cdc_connected())
        return VFS_ERR_IO;

    start = osal_time_ms();
    while (done < len)
    {
        uint32_t n = usb_tusb_cdc_write((const uint8_t*)buf + done, (uint32_t)(len - done));
        usb_tusb_cdc_write_flush();
        done += n;
        if (done >= len)
            break;
        usb_tusb_task();
        if (timeout_ms && (osal_time_ms() - start) >= timeout_ms)
            break;
        if (n == 0)
            break;
    }
    return (int)done;
}

int usb_bus_cdc_read(struct device* dev, void* buf, size_t len,
                     uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    uint32_t start;
    size_t done = 0;
    int mode;

    if (!c || c->cls != USB_CLIENT_CDC || !buf)
        return VFS_ERR_INVAL;

    mode = usb_bus_resolve_xfer_mode(dev, xfer_mode);
    if (mode < 0)
        return mode;

    start = osal_time_ms();
    while (done < len)
    {
        if (usb_tusb_cdc_available())
        {
            uint32_t n = usb_tusb_cdc_read((uint8_t*)buf + done, (uint32_t)(len - done));
            done += n;
            if (done >= len)
                break;
        }
        else
        {
            usb_tusb_task();
            if (timeout_ms == 0)
                break;
            if ((osal_time_ms() - start) >= timeout_ms)
                break;
        }
    }
    return (int)done;
}

extern int usb_net_frame_push_tx(const void* frame, size_t len);
extern int usb_net_frame_pop_rx(void* frame, size_t len);

int usb_bus_ecm_write(struct device* dev, const void* frame, size_t len,
                      uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    int mode;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!c || c->cls != USB_CLIENT_ECM || !frame || !len)
        return VFS_ERR_INVAL;

    mode = usb_bus_resolve_xfer_mode(dev, xfer_mode);
    if (mode < 0)
        return mode;

    return usb_net_frame_push_tx(frame, len);
}

int usb_bus_ecm_read(struct device* dev, void* frame, size_t len,
                     uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    uint32_t start;
    int n, mode;

    if (!c || c->cls != USB_CLIENT_ECM || !frame || !len)
        return VFS_ERR_INVAL;

    mode = usb_bus_resolve_xfer_mode(dev, xfer_mode);
    if (mode < 0)
        return mode;

    start = osal_time_ms();
    for (;;)
    {
        n = usb_net_frame_pop_rx(frame, len);
        if (n > 0)
            return n;
        usb_tusb_task();
        if (timeout_ms == 0)
            return VFS_ERR_TIMEOUT;
        if ((osal_time_ms() - start) >= timeout_ms)
            return VFS_ERR_TIMEOUT;
    }
}

int usb_bus_hid_write(struct device* dev, const void* report, size_t len,
                      uint32_t timeout_ms, uint32_t xfer_mode)
{
    struct usb_bus_client* c = usb_client_from_device(dev);
    int mode;

    COMPAT_IGNORE_RESULT(timeout_ms);
    if (!c || c->cls != USB_CLIENT_HID || !report || !len)
        return VFS_ERR_INVAL;

    mode = usb_bus_resolve_xfer_mode(dev, xfer_mode);
    if (mode < 0)
        return mode;

    if (!usb_tusb_hid_ready())
        return VFS_ERR_IO;
    if (!usb_tusb_hid_report(0, report, (uint16_t)len))
        return VFS_ERR_IO;
    return (int)len;
}

void usb_otg_fs_irq_handler(void)
{
    if (s_irq_host)
        usb_tusb_int_handler(s_irq_host->rhport);
}
