/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file http_server.h
 *@brief HTTP Server (lwIP netconn 驱动)
 *@author H-000-H
 *@details
 *   基于 lwIP netconn 的 HTTP 服务端, 提供静态资源与简单 REST 应答。
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H
#ifdef __cplusplus
extern "C"
{
#endif
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include <string.h>
#ifdef __cplusplus
}
#endif
#endif