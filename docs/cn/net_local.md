# 本地网络模块扩展 (不入库)

本文档描述本地新增的网络模块，这些模块不提交到 GitHub，仅在本机使用。
主文档 `net.md` 只描述入库的最小构建集（tcp/ppp/usb/mqtt_client）。

---

## TLS 客户端通道 (net/port/tls/tls_client.c)

- 加密通道底座，直接调用 lwIP 自带的 `altcp_tls` API（mbedTLS 后端）。
- 加密、握手、数据包拆分全部由 lwIP 完成，本层只负责建连和收发数据。
- 结构：RX/TX 静态缓冲 + sent 回调续发；`connected` 回调在 TLS 握手完成后才触发。
- 随机数：`mbedtls_hardware_poll()` 提供弱符号默认实现（占位用），板级有硬件随机数发生器时须以同名强符号覆盖。
- 时钟：`MBEDTLS_PLATFORM_TIME_ALT` 接 `mini_time_ms()/1000`（裸机无 RTC）。
- mbedtls 堆经 `altcp_tls_mbedtls_mem.c` 自动接到 lwIP mem 静态池，不依赖系统堆。
- https/mqtts 共用此底座。

---

## HTTP 客户端 (net/port/http/http_client.c)

- coreHTTP v3 薄包装（明文）。
- 请求组装/响应解析/分块解码全部由 coreHTTP 负责；传输走 transport_glue（tcp_client FIFO 通道）。
- 连接采用 keep-alive：`do_connect` 建连后可连续多次 `request`。
- 响应体指针指向上下文内部静态缓冲，有效至下一次 `request` 或断连。
- 驱动模型：应用先调用 `http_client_process()` 等待底层建连完成，再发起 `request`（请求/响应为同步流程）。

---

## HTTPS 客户端 (net/port/https/https_client.c)

- coreHTTP v3 薄包装（加密）。
- 功能与 HTTP 客户端一致，差别仅在传输通道：加密通道按项目设计不走 transport_glue，由本包装层直接基于 `tls_client`（lwIP altcp_tls 直连封装）提供 coreHTTP 需要的 send/recv 适配。
- 连接采用 keep-alive：`do_connect`（TCP + TLS 握手）完成后可连续多次 `request`。
- 响应体指针指向上下文内部静态缓冲，有效至下一次 `request` 或断连。
- 驱动模型：应用先调用 `https_client_process()` 等待握手完成，再发起 `request`。

---

## MQTT-S 客户端 (net/port/mqtts/mqtts_client.c)

- coreMQTT v5 薄包装（加密）。
- 功能与 API 对照 mqtt_client（connect/publish/subscribe/unsubscribe/process）。
- 传输内嵌 `tls_client_context` 直连 altcp_tls，不经 transport_glue；复用 `port/mqtt/core_mqtt_config.h`。
- 协议引擎复用 coreMQTT（与 MQTT 客户端共享其开关与缓冲配置）。
- 本层不维护订阅表：下行 PUBLISH 原样（主题指针 + 长度）交给唯一消息回调。
- 驱动模型：应用周期调用 `mqtts_client_process()`（内部即 `MQTT_ProcessLoop`）。

---

## MQTT Broker (net/port/mqtt_broker/mqtt_broker.c)

- 基于 tcp_server 会话表的轻量 MQTT Broker（服务端），明文不上 TLS。
- 支持 MQTT 3.1.1 与 5.0 客户端接入，QoS 0/1（下行转发统一降为 QoS 0）。
- 全局订阅表按 `MQTT_MatchTopic` 匹配转发（含通配符 `+` / `#`）。
- coreMQTT v5 公开 serializer 无 CONNECT/SUBSCRIBE/UNSUBSCRIBE 反序列化，该部分手写解析；其余复用 `MQTT_ProcessIncomingPacketTypeAndLength` / `MQTT_DeserializePublish` / `MQTT_SerializeAck` / `MQTT_SerializePublish`。
- 支持 CONNACK/SUBACK/PUBACK/PINGRESP、keep-alive 超时检测。
- 每会话入站报文累积缓冲（`CONFIG_MQTT_BROKER_PACKET_BUFFER_SIZE`），须为 2 的幂且 ≥ 单条最大报文。

---

## 配置说明

这些模块的配置项定义在 `Kconfig.local`（不入库），需手动在 `Kconfig.non_esp` 末尾加 `source "Kconfig.local"` 才能在 menuconfig 中看到。
不 source 时 `.config` 中已有的配置值仍然生效，构建不受影响。

CMake 链接逻辑在 `net/local.cmake`（不入库），由 `net/CMakeLists.txt` 末尾的 `include(local.cmake OPTIONAL)` 加载。

---

## 上板须知

- **随机数**：`mbedtls_hardware_poll()` 默认实现是占位用的伪随机，板级有硬件随机数发生器时必须用同名强符号覆盖，否则握手密钥可预测。
- **证书校验**：传 CA 时会做有效期检查，时间基准是自开机秒数（`mini_time_ms()/1000`），对公网证书会判"过期"——调试期可传 `NULL` CA（不校验），量产需接 RTC 或跳过有效期检查。
- **握手缓冲**：`CONFIG_TLS_CONTENT_LEN` 默认 2048，连公网服务器握手失败时调大本项（证书链通常 2~6KB），且需保持 `TCP_WND >= 本值`（altcp_tls 要求），否则握手可能停滞。
