/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file usbethif.c
 *@brief USB CDC-ECM / RNDIS 网络接口 (lwIP netif 驱动)
 *@author H-000-H
 *@details
 *   net/port/usbethif.c
 *   lwIP 以太网接口对接 USB CDC-ECM 网卡。遵循分层:
 *   net → VFS(device_*) → bus/usb → TinyUSB, 不直接触碰 bus 层内部符号。
 *   - init:  usb_ethif_init_dev(dev_name) 用 device_find_by_label 拿网卡 device
 *            + device_open, 把 device 存进 netif->state, 再 netif_add 注册。
 *   - output: link_output 从 netif->state 取 device → device_write
 *   - input:  usb_ethif_input(netif, frame, len) 封装 pbuf 上交协议栈
 *   无全局 device 句柄: device 随 netif 走, 每个网卡独立 netif/device。
 *   裸机 (NO_SYS=1) 下 usb_ethif_poll(netif) 需在主循环周期调用。
 */

#include "arch/sys_arch.h"
#include "board_define_usb.h"
#include "device.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "netif/ethernet.h"
#include "status.h"
#include "system_log.h"

#define USB_ETHIF_IMPL
#include "usbethif.h"

static const char* const k_tag = "usb_ethif";

/* 数量由 DTS ECM 节点数决定 (dtc-lite 生成, 缺省 1)。 */
#define USBETHIF_NETIF_MAX DTC_GEN_COUNT_HETEROGENEOUS_USB_CDC_ECM
static struct netif s_usb_netif[USBETHIF_NETIF_MAX];
static uint8_t s_usb_netif_used[USBETHIF_NETIF_MAX];

/* lwIP netif->mtu 为 IP MTU (不含 14 字节 ethhdr)*/
#define USBETHIF_MTU (CONFIG_USB_NET_MTU - 14)
#define USBETHIF_FRAME_MAX CONFIG_USB_NET_MTU
#define USBETHIF_TX_TIMEOUT_MS CONFIG_USB_NET_TX_TIMEOUT_MS
#define USBETHIF_RX_TIMEOUT_MS CONFIG_USB_NET_RX_TIMEOUT_MS /* 非阻塞尝试 */

static uint8_t s_mac[6] = {CONFIG_USB_NET_MAC0, CONFIG_USB_NET_MAC1, CONFIG_USB_NET_MAC2, CONFIG_USB_NET_MAC3, CONFIG_USB_NET_MAC4, CONFIG_USB_NET_MAC5};

/**
 * @brief 从 netif->state 取回 USB 网卡 device 句柄
 * @param[in] netif lwIP 网络接口
 * @return USB 网卡 device 或 NULL
 */
static struct device* usb_ethif_dev(struct netif* netif) { return (netif) ? (struct device*)netif->state : NULL; }

/**
 * @brief lwIP → USB 发送 (从 netif->state 取 device, 走 VFS device_write)
 * @param[in] netif lwIP 网络接口
 * @param[in] pbuf  lwIP 数据包
 * @return ERR_OK 成功，其他值 失败
 */
static err_t usb_ethif_link_output(struct netif* netif, struct pbuf* pbuf)
{
    struct device* dev = usb_ethif_dev(netif);
    uint8_t* frame = NULL;
    int sent = 0;

    if (!dev || !pbuf)
        return ERR_ARG;

    if (pbuf->tot_len != pbuf->len)
    {
        SYS_LOGE(k_tag, "usb_ethif_link_output: pbuf is not contiguous");
        return ERR_BUF;
    }
    frame = (uint8_t*)pbuf->payload;

    /* 走 VFS: device_write → usb_vfs_write → usb_bus_ecm_write → usb_net_frame_push_tx */
    sent = device_write(dev, frame, pbuf->tot_len, USBETHIF_TX_TIMEOUT_MS);
    if (sent < 0)
    {
        SYS_LOGE(k_tag, "usb_ethif_link_output: device_write failed %d", sent);
        return ERR_IF;
    }
    return ERR_OK;
}

int usb_ethif_input(struct netif* netif, const uint8_t* frame, size_t len)
{
    struct pbuf* temp_buf;

    if (!netif || !frame || len == 0U)
        return ERR_ARG;

    temp_buf = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!temp_buf)
    {
        SYS_LOGE(k_tag, "usb_ethif_input: pbuf_alloc failed (%u)", (unsigned)len);
        return ERR_BUF;
    }
    COMPAT_IGNORE_RESULT(pbuf_take(temp_buf, frame, (u16_t)len));

    if (netif->input(temp_buf, netif) != ERR_OK)
    {
        SYS_LOGW(k_tag, "usb_ethif_input: netif->input drop frame");
        pbuf_free(temp_buf);
        return ERR_IF;
    }
    return ERR_OK;
}

err_t usb_ethif_init(struct netif* netif)
{
    if (!netif || !usb_ethif_dev(netif))
        return ERR_ARG;

    netif->name[0] = 'u';
    netif->name[1] = 's';
    netif->output = etharp_output;
    netif->linkoutput = usb_ethif_link_output;
    netif->input = ethernet_input;
    netif->mtu = USBETHIF_MTU;
    netif->hwaddr_len = 6;
    COMPAT_MEM_COPY(netif->hwaddr, s_mac, 6);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

int usb_ethif_init_dev(const char* dev_name)
{
    struct device* dev;
    struct netif* nif;
    int ret;
    int idx = -1;
    int i;

    if (!dev_name)
        return -1;

    /* 按 label 找到这张 USB 网卡的 device */
    dev = device_find_by_label(dev_name);
    if (IS_ERR(dev))
    {
        ret = PTR_ERR(dev);
        SYS_LOGE(k_tag, "USB eth '%s' not found: %d", dev_name, ret);
        return -1;
    }
    ret = device_open(dev, NULL);
    if (ret != VFS_OK)
    {
        SYS_LOGE(k_tag, "Open USB eth '%s' failed: %d", dev_name, ret);
        return -1;
    }

    /* 分配一个空闲静态 netif 槽位 */
    for (i = 0; i < USBETHIF_NETIF_MAX; i++)
    {
        if (!s_usb_netif_used[i])
        {
            idx = i;
            break;
        }
    }
    if (idx < 0)
    {
        SYS_LOGE(k_tag, "usb_ethif_init_dev: no free netif slot");
        COMPAT_IGNORE_RESULT(device_close(dev));
        return -1;
    }
    s_usb_netif_used[idx] = 1;
    COMPAT_MEM_SET(&s_usb_netif[idx], 0, sizeof(s_usb_netif[idx]));

    /* state 传 device, netif_add 会回调 usb_ethif_init 完成初始化 */
    nif = netif_add(&s_usb_netif[idx], NULL, NULL, NULL, dev, usb_ethif_init, ethernet_input);
    if (!nif)
    {
        SYS_LOGE(k_tag, "usb_ethif_init_dev: netif_add failed");
        s_usb_netif_used[idx] = 0;
        COMPAT_IGNORE_RESULT(device_close(dev));
        return -1;
    }

    netif_set_default(nif);
    netif_set_up(nif);
    return 0;
}

#if NO_SYS == 1
int usb_ethif_poll(struct netif* netif)
{
    struct device* dev = usb_ethif_dev(netif);
    uint8_t frame[USBETHIF_FRAME_MAX];
    int ret;

    if (!dev || !netif)
        return 0;

    /* 走 VFS: device_read → usb_vfs_read → usb_bus_ecm_read → usb_net_frame_pop_rx */
    ret = device_read(dev, frame, sizeof(frame), USBETHIF_RX_TIMEOUT_MS);
    if (ret > 0)
    {
        usb_ethif_input(netif, frame, (size_t)ret);
        return 1;
    }
    return 0;
}
#endif /* NO_SYS */
