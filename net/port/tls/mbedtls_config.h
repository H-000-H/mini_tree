/**
 * @file mbedtls_config.h
 * @author H-000-H
 * @brief lwIP altcp_tls 专用 mbedTLS 2.28 配置 (mini_tree_link_mbedtls 强制要求)
 * @note  只打开 TLS 1.2 客户端功能: 内存走 lwIP 内存池 (不用系统 malloc); 随机数走自定义硬件接口; 时间用系统运行时长代替 (板上无时钟芯片)
 * @copyright SPDX-License-Identifier: Apache-2.0
 */
#ifndef MBEDTLS_MINI_TREE_ALTCP_TLS_CONFIG_H
#define MBEDTLS_MINI_TREE_ALTCP_TLS_CONFIG_H

/* -------------------------------------------------------------------------- */
/* 平台与内存                                                                 */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_HAVE_TIME         /**< 不开这个, lwIP 里会话复用的代码编译不过 */
#define MBEDTLS_PLATFORM_TIME_ALT /**< 时间用系统开机以来的秒数代替, 实现在 tls_client.c */
#define MBEDTLS_PLATFORM_C
#define MBEDTLS_PLATFORM_MEMORY /**< mbedtls 的内存申请改走 lwIP 内存池 */
#include <stdio.h>
#define MBEDTLS_PLATFORM_FPRINTF_MACRO (void)/**< mbedtls 内部的打印直接丢弃, 出错信息走系统日志 */

/* -------------------------------------------------------------------------- */
/* 随机数                                                                     */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_ENTROPY_C
#define MBEDTLS_NO_PLATFORM_ENTROPY  /**< 系统没有现成的随机源 (如 /dev/urandom) */
#define MBEDTLS_ENTROPY_HARDWARE_ALT /**< 改用自定义函数取随机数: mbedtls_hardware_poll(), 见 tls_client.c */
#define MBEDTLS_CTR_DRBG_C           /**< 从上面收集的随机数生成加密用的随机流 */

/* -------------------------------------------------------------------------- */
/* TLS 协议 (仅客户端, 仅 1.2)                                                */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_SSL_TLS_C
#define MBEDTLS_SSL_CLI_C
#define MBEDTLS_SSL_PROTO_TLS1_2
#define MBEDTLS_SSL_ALPN /**< 握手时顺带协商应用层协议名 (如 http/1.1), 开销很小 */

/* 单个加密数据包的最大长度, 收发各一份缓冲; CMake 会从 Kconfig 填入实际值 */
#ifndef MBEDTLS_SSL_MAX_CONTENT_LEN
#define MBEDTLS_SSL_MAX_CONTENT_LEN 2048
#endif

/* -------------------------------------------------------------------------- */
/* 密钥交换与密码套件 (覆盖公网服务器常见组合)                                 */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_KEY_EXCHANGE_ECDHE_RSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_ECDHE_ECDSA_ENABLED
#define MBEDTLS_KEY_EXCHANGE_RSA_ENABLED

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_C /**< 对称加密的基础模块, 密码套件必需 */
#define MBEDTLS_MD_C     /**< 哈希摘要的基础模块, 握手签名必需 */
#define MBEDTLS_CIPHER_MODE_CBC
#define MBEDTLS_CIPHER_MODE_CTR /**< GCM 模式需要 */
#define MBEDTLS_GCM_C
#define MBEDTLS_SHA256_C /**< 主流密码套件都用 SHA-256 */
#define MBEDTLS_SHA512_C /**< 随机数模块需要 */

#define MBEDTLS_ECP_C
#define MBEDTLS_ECDH_C
#define MBEDTLS_ECDSA_C
#define MBEDTLS_ECP_DP_SECP256R1_ENABLED
#define MBEDTLS_ECP_DP_SECP384R1_ENABLED
#define MBEDTLS_RSA_C
#define MBEDTLS_PKCS1_V15
#define MBEDTLS_BIGNUM_C

/* -------------------------------------------------------------------------- */
/* 证书解析                                                                   */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_X509_USE_C
#define MBEDTLS_X509_CRT_PARSE_C
#define MBEDTLS_PK_C
#define MBEDTLS_PK_PARSE_C
#define MBEDTLS_ASN1_PARSE_C
#define MBEDTLS_ASN1_WRITE_C /**< ECDSA 签名需要 */
#define MBEDTLS_OID_C
#define MBEDTLS_BASE64_C
#define MBEDTLS_PEM_PARSE_C

/* -------------------------------------------------------------------------- */
/* 其他                                                                       */
/* -------------------------------------------------------------------------- */
#define MBEDTLS_SSL_SERVER_NAME_INDICATION /**< 握手时带上域名, 一个 IP 挂多个网站的服务器必需 */

/* 让 mbedtls 自己检查上面的开关有没有配错或漏配 */
#include "mbedtls/check_config.h"

#endif /* MBEDTLS_MINI_TREE_ALTCP_TLS_CONFIG_H */
