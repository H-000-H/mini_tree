# net/ — Network Protocol Stack Glue

> Thin wrapper network layer based on lwIP + coreMQTT, hardware access only through the device/VFS model.

| Item | Content |
| :--- | :--- |
| **Audience** | Engineers integrating MQTT / TCP communication or network interface adapters |
| **Prerequisites** | Read [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) |
| **Related** | [file_index.md](file_index.md) · [usb_tusb_port.md](usb_tusb_port.md) |

---

## Table of Contents

1. [Layered Architecture](#1-layered-architecture)
2. [File Listing](#2-file-listing)
3. [Error Code System](#3-error-code-system)
4. [Module Descriptions](#4-module-descriptions)
5. [Kconfig Integration](#5-kconfig-integration)
6. [Design Decisions](#6-design-decisions)

---

## 1. Layered Architecture

```
┌──────────────────────────────────────────┐
│ Application                              │
│   mqtt_client_init / _connect / _publish │
└──────────────────┬───────────────────────┘
                   │
┌──────────────────▼───────────────────────┐
│ mqtt_client (coreMQTT v5 thin wrapper)   │
│   encode/decode / state machine / keep-  │
│   alive → coreMQTT                       │
└──────────────────┬───────────────────────┘
                   │ TransportInterface_t
┌──────────────────▼───────────────────────┐
│ transport_glue                            │
│   network_transport_write / read          │
│   bridges core lib int32_t ↔ tcp_client  │
└──────────────────┬───────────────────────┘
                   │ FIFO (ring buffer)
┌──────────────────▼───────────────────────┐
│ tcp_client / tcp_server (lwIP TCP wrap)  │
│   async send + conn state machine + RX   │
└──────────────────┬───────────────────────┘
                   │ lwIP pcb
┌──────────────────▼───────────────────────┐
│ NIC layer                                │
│   pppif (PPP) / usbethif (TinyUSB CDC)  │
└──────────────────────────────────────────┘
```

Call convention: upper protocol (mqtt_client) → transport adapter (transport_glue) → lower transport (tcp_client) → lwIP → NIC. Encrypted channels (mqtts / https) do **not** go through transport_glue; they are wrapped directly on top of lwIP `altcp_tls`.

---

## 2. File Listing

> Most subdirectories are gitignored; only the following files are committed.

| Path | Description |
| :--- | :--- |
| `port/mqtt/mqtt_client.{c,h}` | coreMQTT v5 thin wrapper, `NET_*` error codes |
| `port/mqtt/core_mqtt_config.h` | coreMQTT configuration header |
| `port/tcp/tcp_client.{c,h}` | TCP client (FIFO + async send + connection state machine) |
| `port/tcp/tcp_server.{c,h}` | TCP server |
| `port/transport_glue/transport_glue.{c,h}` | `network_transport_*` adapter, bridges tcp_client to coreMQTT `int32_t` signatures |
| `port/net_error.h` | `NET_OK` / `NET_ERR_*` error code system |
| `port/pppif/pppif.{c,h}` | PPP netif adapter for lwIP |
| `port/usb/usbethif.{c,h}` | TinyUSB CDC-NCM/RNDIS netif adapter for lwIP |

---

## 3. Error Code System

Upper protocol modules (transport_glue, mqtt_client) uniformly use `NET_*` error codes, decoupled from lwIP `err_t` and kernel error codes.

| Symbol | Value | Meaning |
| :--- | :--- | :--- |
| `NET_OK` | `0` | Success |
| `NET_ERR_INVAL` | `-EINVAL` | Invalid argument (NULL pointer, zero length, enum out of range) |
| `NET_ERR_CONN` | `-ENOTCONN` | Link error (send/recv while disconnected / peer closed / connect failed) |
| `NET_ERR_TIMEOUT` | `-ETIMEDOUT` | Timeout (TCP connect / response receive / TX FIFO space) |
| `NET_ERR_NOSPC` | `-ENOSPC` | Buffer too small (header assembly / message serialization overflow) |
| `NET_ERR_STATE` | `-EISCONN` | State conflict (connect while already connected / connecting) |

Lower-level errors are translated to `NET_*` at the wrapper boundary; application code never sees lwIP internals.

---

## 4. Module Descriptions

### 4.1 mqtt_client — coreMQTT v5 Thin Wrapper

- Encoding/decoding, state machine, keep-alive, resend, and subscription acknowledgement are all handled by coreMQTT.
- Transport goes through transport_glue (tcp_client FIFO channel).
- **No subscription table**: incoming PUBLISH messages are passed as-is (topic pointer + length) to a single message callback; the application routes by topic.
- Drive model: application periodically calls `mqtt_client_process()` (internally `MQTT_ProcessLoop`).
- Single-buffer design: `CONFIG_MQTT_NET_BUFFER_SIZE` (default 2048), shared for TX/RX.

### 4.2 tcp_client / tcp_server — lwIP TCP Wrapper

- **tcp_client**: async send requires TX FIFO + `poll_send` callback; connection state machine maintains `pcb` / `is_connected` / `link_failed`.
- **tcp_server**: listens on a port, dispatches accepted connections via callback.
- RX buffering uses a ring FIFO (`CONFIG_TCP_CLIENT_RX_BUFFER_SIZE`, default 1024).

### 4.3 transport_glue — Core Library Transport Adapter

- Implements FreeRTOS core library (coreMQTT / coreHTTP) `TransportInterface_t`.
- Maps `send` / `recv` function pointers to tcp_client FIFO read/write.
- Shared by both MQTT and HTTP wrapper layers.

### 4.4 pppif — PPP Network Interface

- Implements lwIP `netif` adapter, interfacing with PPP link layer.

### 4.5 usbethif — TinyUSB USB Network Interface

- Implements lwIP `netif` adapter, interfacing with TinyUSB CDC-NCM / RNDIS virtual NIC.
- MAC address configured via `CONFIG_USB_NET_MAC*` Kconfig options.

---

## 5. Kconfig Integration

| Config | Description |
| :--- | :--- |
| `CONFIG_LWIP` | lwIP master switch (default `n`) |
| `CONFIG_NET_MQTT_USE_COREMQTT` | MQTT uses coreMQTT backend (default `y`) |
| `CONFIG_MQTT_NET_BUFFER_SIZE` | MQTT shared TX/RX buffer size (default 2048) |
| `CONFIG_TCP_CLIENT_RX_BUFFER_SIZE` | TCP client RX FIFO size (default 1024) |
| `CONFIG_TCP_CLIENT_TX_BUFFER_SIZE` | TCP client TX FIFO size |
| `CONFIG_USB_NET_MAC0` ~ `MAC5` | USB NIC MAC address bytes |

---

## 6. Design Decisions

| Decision | Rationale |
| :--- | :--- |
| MQTT via coreMQTT v5 thin wrapper, not self-written | Mature library, less maintenance; replaces 1057-line state machine with thin wrapper |
| Unified transport_glue | coreMQTT / coreHTTP share `TransportInterface_t`; one adapter, zero duplication |
| Single-buffer design (MQTT) | coreMQTT v5 `MQTT_Init` accepts only one buffer; cannot split TX/RX |
| `NET_*` error codes independent of lwIP `err_t` | Decouples upper protocols from lower stack; swapping backends (e.g. altcp_tls) doesn't affect application |
| Encrypted channels bypass transport_glue | mqtts / https wrap directly on lwIP `altcp_tls`, keeping the transport layer pure |
| Full-name identifiers | `mqtt_context`, `network_context`, `body_length`; no abbreviations (project convention) |

---

## Related Documents

- [architecture.md](architecture.md) · [ecosystem.md](ecosystem.md) · [file_index.md](file_index.md)
- [usb_tusb_port.md](usb_tusb_port.md) · [driver_guide.md](driver_guide.md)
