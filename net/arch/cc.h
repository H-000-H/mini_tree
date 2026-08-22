/**
 *@copyright SPDX-License-Identifier: Apache-2.0
 *@file cc.h
 *@brief cc 头文件
 *@author H-000-H
 *@details
 *   net/arch/cc.h
 *   lwIP 移植层编译器/平台抽象头 (mini_tree 适配)
 *   职责 (对应 lwip/arch.h 约定, 不得放进 lwipopts.h):
 *   - 平台字节序 BYTE_ORDER
 *   - 诊断输出 LWIP_PLATFORM_DIAG / 断言 LWIP_PLATFORM_ASSERT
 *   - 结构体紧凑打包 (网络协议头)
 *   - 随机数 LWIP_RAND()
 */

#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include "system_log.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* -------------------------------------------------------------------------- */
    /* sys_prot_t: lwIP 轻量级临界区保护 (SYS_ARCH_PROTECT) 的状态类型。           */
    /* uint32_t 足以容纳各后端关中断前的 primask 状态。加上我也不喜欢u16这种写法     */
    /* -------------------------------------------------------------------------- */
    typedef uint32_t sys_prot_t;

/* -------------------------------------------------------------------------- */
/* 格式化输出占位符宏 (lwIP 内部 LWIP_PLATFORM_DIAG 用)                        */
/* -------------------------------------------------------------------------- */
#define U16_F PRIu16
#define S16_F PRId16
#define X16_F PRIx16
#define U32_F PRIu32
#define S32_F PRId32
#define X32_F PRIx32
#define SZT_F "zu"

/* -------------------------------------------------------------------------- */
/* 目标机字节序 (ARM / Xtensa 等均为小端)                                     */
/* -------------------------------------------------------------------------- */
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

/* -------------------------------------------------------------------------- */
/* 结构体紧凑打包宏 (网络协议头禁止对齐填充)                                   */
/* -------------------------------------------------------------------------- */
#include "compiler_compat.h"
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT COMPAT_PACKED
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

    /* -------------------------------------------------------------------------- */
    /* 诊断输出 / 断言 (走 OSAL, 轻量且后端统一)                                  */
    /*   LWIP_PLATFORM_DIAG(x) 的 x 是 "(fmt, args...)" 双括号形式;                */
    /* -------------------------------------------------------------------------- */
    /**
     * @brief lwIP 诊断输出
     * @param[in] x 双括号形式的参数列表 (fmt, args...)
     */
    void lwip_diag(const char* fmt, ...);

#define LWIP_PLATFORM_DIAG(x)                                                                                                                                                                          \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        lwip_diag x;                                                                                                                                                                                   \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x)                                                                                                                                                                        \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        SYS_LOGE("lwIP", "Assertion \"%s\" failed at line %d in %s", x, __LINE__, __FILE__);                                                                                                           \
        while (1)                                                                                                                                                                                      \
            ;                                                                                                                                                                                          \
    } while (0)

/* -------------------------------------------------------------------------- */
/* 随机数生成宏 (TCP 初始序号 / DHCP XID)                                     */
/*   裸机用标准库 rand(); 需 srand 播种, 否则序列固定。                        */
/* -------------------------------------------------------------------------- */
#ifndef LWIP_RAND
#define LWIP_RAND() ((u32_t)rand())
#endif

#ifdef __cplusplus
}
#endif

#endif /* LWIP_ARCH_CC_H */
