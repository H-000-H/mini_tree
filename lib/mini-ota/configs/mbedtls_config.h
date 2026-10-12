/* SPDX-License-Identifier: Apache-2.0 */
/* mini_tree_link_mbedtls(<target> <port_dir>) 约定端口目录内文件名必须是
 * mbedtls_config.h。这里转发到 mini-ota 自有的 OTA mbedtls 配置
 * (mbedtls_config_boot.h)，使编入 mini_tree 时复用工程级 mbedtls 依赖。 */
#include "mbedtls_config_boot.h"
