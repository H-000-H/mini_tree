# net/ — 网络协议栈胶水

> 基于 lwIP + coreMQTT 的薄包装网络层，只经 device/VFS 模型触硬件。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 需要接入 MQTT / TCP 通信或网卡适配的工程师 |
| **前置** | 已读 [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |
| **相关** | [file_index.md](file_index.md) · [usb_tusb_port.md](usb_tusb_port.md) |

---

## 目录

1. [分层架构](#1-分层架构)
2. [文件清单](#2-文件清单)
3. [错误码体系](#3-错误码体系)
4. [模块说明](#4-模块说明)
5. [Kconfig 集成](#5-kconfig-集成)
6. [设计决策](#6-设计决策)

---

## 1. 分层架构

```
┌──────────────────────────────────────────┐
│ 应用层                                    │
│   mqtt_client_init / _connect / _publish │
└──────────────────┬───────────────────────┘
                   │
┌──────────────────▼───────────────────────┐
│ mqtt_client（coreMQTT v5 薄包装）          │
│   报文编解码 / 状态机 / 心跳 → coreMQTT    │
└──────────────────┬───────────────────────┘
                   │ TransportInterface_t
┌──────────────────▼───────────────────────┐
│ transport_glue                            │
│   network_transport_write / read          │
│   桥接 core 库 int32_t 签名 ↔ tcp_client │
└──────────────────┬───────────────────────┘
                   │ FIFO（环形缓冲）
┌──────────────────▼───────────────────────┐
│ tcp_client / tcp_server（lwIP TCP 包装）   │
│   异步发送 + 连接状态机 + RX FIFO          │
└──────────────────┬───────────────────────┘
                   │ lwIP pcb
┌──────────────────▼───────────────────────┐
│ 网卡层                                    │
│   pppif（PPP）/ usbethif（TinyUSB CDC）   │
└──────────────────────────────────────────┘
```

调用规则：上层协议（mqtt_client）→ 传输适配（transport_glue）→ 底层传输（tcp_client）→ lwIP → 网卡。加密通道（mqtts / https）不走 transport_glue，由各自包装层直接基于 lwIP `altcp_tls` 封装。

---

## 2. 文件清单

> 大部分子目录已 gitignore，仅提交以下文件。

| 路径 | 说明 |
| :--- | :--- |
| `port/mqtt/mqtt_client.{c,h}` | coreMQTT v5 薄包装，`NET_*` 错误码 |
| `port/mqtt/core_mqtt_config.h` | coreMQTT 配置头 |
| `port/tcp/tcp_client.{c,h}` | TCP 客户端（FIFO + 异步发送 + 连接状态机） |
| `port/tcp/tcp_server.{c,h}` | TCP 服务端 |
| `port/transport_glue/transport_glue.{c,h}` | `network_transport_*` 适配层，桥接 tcp_client 与 coreMQTT `int32_t` 签名 |
| `port/net_error.h` | `NET_OK` / `NET_ERR_*` 错误码体系 |
| `port/pppif/pppif.{c,h}` | PPP 网卡 lwIP netif 适配 |
| `port/usb/usbethif.{c,h}` | TinyUSB CDC-NCM/RNDIS 网卡 lwIP netif 适配 |

---

## 3. 错误码体系

上层协议模块（transport_glue、mqtt_client）统一使用 `NET_*` 错误码，与底层 lwIP `err_t` 及内核错误码解耦。

| 符号 | 值 | 含义 |
| :--- | :--- | :--- |
| `NET_OK` | `0` | 成功 |
| `NET_ERR_INVAL` | `-EINVAL` | 入参非法（指针为 NULL、长度为 0、枚举越界） |
| `NET_ERR_CONN` | `-ENOTCONN` | 链路错误（未连接即收发 / 对端断开 / 建连失败） |
| `NET_ERR_TIMEOUT` | `-ETIMEDOUT` | 等待超时（TCP 建连 / 响应接收 / TX FIFO 腾挪） |
| `NET_ERR_NOSPC` | `-ENOSPC` | 缓冲不足（请求头组装 / 报文序列化放不下） |
| `NET_ERR_STATE` | `-EISCONN` | 状态冲突（已连接 / 建连中重复发起连接） |

底层错误在包装层边界翻译为 `NET_*`，业务侧无需感知 lwIP 内部错误。

---

## 4. 模块说明

### 4.1 mqtt_client — coreMQTT v5 薄包装

- 报文编解码、状态机、心跳、重发、订阅确认全部由 coreMQTT 负责。
- 传输走 transport_glue（tcp_client FIFO 通道）。
- **不维护订阅表**：下行 PUBLISH 原样（主题指针 + 长度）交给唯一消息回调，业务侧自行按主题分流。
- 驱动模型：应用周期调用 `mqtt_client_process()`（内部即 `MQTT_ProcessLoop`）。
- 单缓冲设计：`CONFIG_MQTT_NET_BUFFER_SIZE`（默认 2048），收发共用。

### 4.2 tcp_client / tcp_server — lwIP TCP 包装

- **tcp_client**：异步发送需 TX FIFO + `poll_send` 回调；连接状态机维护 `pcb` / `is_connected` / `link_failed`。
- **tcp_server**：监听端口，接受连接后回调业务处理。
- RX 缓冲走环形 FIFO（`CONFIG_TCP_CLIENT_RX_BUFFER_SIZE`，默认 1024）。

### 4.3 transport_glue — core 库传输适配

- 实现 FreeRTOS core 库（coreMQTT / coreHTTP）的 `TransportInterface_t`。
- 把 `send` / `recv` 函数指针映射到 tcp_client 的 FIFO 读写。
- MQTT 与 HTTP 包装层共用此传输层。

### 4.4 pppif — PPP 网卡

- 实现 lwIP `netif` 适配，对接 PPP 链路。

### 4.5 usbethif — TinyUSB USB 网卡

- 实现 lwIP `netif` 适配，对接 TinyUSB CDC-NCM / RNDIS 虚拟网卡。
- MAC 地址由 `CONFIG_USB_NET_MAC*` Kconfig 配置。

---

## 5. Kconfig 集成

| 配置项 | 说明 |
| :--- | :--- |
| `CONFIG_LWIP` | lwIP 总开关（默认 `n`） |
| `CONFIG_NET_MQTT_USE_COREMQTT` | MQTT 使用 coreMQTT 后端（默认 `y`） |
| `CONFIG_MQTT_NET_BUFFER_SIZE` | MQTT 收发共享缓冲大小（默认 2048） |
| `CONFIG_TCP_CLIENT_RX_BUFFER_SIZE` | TCP 客户端 RX FIFO 大小（默认 1024） |
| `CONFIG_TCP_CLIENT_TX_BUFFER_SIZE` | TCP 客户端 TX FIFO 大小 |
| `CONFIG_USB_NET_MAC0` ~ `MAC5` | USB 网卡 MAC 地址字节 |

---

## 6. 设计决策

| 决策 | 理由 |
| :--- | :--- |
| MQTT 走 coreMQTT v5 薄包装而非自研 | 成熟库，减少维护成本；自研 1057 行状态机替换为薄包装 |
| 传输层统一 transport_glue | coreMQTT / coreHTTP 共用 `TransportInterface_t`，一份适配零重复 |
| 单缓冲设计（MQTT） | coreMQTT v5 `MQTT_Init` 只接受一块缓冲，无法拆分 TX/RX |
| `NET_*` 错误码独立于 lwIP `err_t` | 上层协议与底层网络栈解耦，换后端（如 altcp_tls）不影响业务 |
| 加密通道不走 transport_glue | mqtts / https 由各自包装层直接基于 lwIP `altcp_tls` 封装，保持传输层纯粹 |
| 标识符全名化 | `mqtt_context`、`network_context`、`body_length`，禁止缩写（对齐项目规范） |

---

## 相关文档

- [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) · [file_index.md](file_index.md)
- [usb_tusb_port.md](usb_tusb_port.md) · [driver_guide.md](driver_guide.md)
