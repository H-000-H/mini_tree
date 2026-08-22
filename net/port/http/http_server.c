/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file http_server.c
 *@brief HTTP Server (lwIP netconn 驱动)
 *@details
 *@author H-000-H
 */
#include "http_server.h"

static struct tcp_pcb s_tcp_pcb; /*tcp控制块*/

static err_t http_receive(void* arg, struct tcp_pcb* pcb, struct pbuf* buf, err_t err)
{
    if (!buf)
    {
        tcp_close(pcb);
        return ERR_OK;
    }
    return ERR_OK;
}