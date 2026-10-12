#ifndef BOOT_CONFIG_H
#define BOOT_CONFIG_H
#if __has_include(<config.h>) && __has_include(<compiler_compat.h>)
#include <config.h>
/*外部配置文件优先包含 */
#elif __has_include(<mini_boot_config.h>)
#include "mini_boot_config.h"
#else
#if defined(_MSC_VER)
#pragma message("no external configuration file found, using default configuration")
#else
#warning "no external configuration file found, using default configuration"
#endif
#endif

#if defined(CONFIG_FLASH_START_ADDR)
#define FLASH_START_ADDR CONFIG_FLASH_START_ADDR
#elif defined(FLASH_START_ADDR)
#else
#define FLASH_START_ADDR 0x08000000U
#endif

/* SRAM 区间：用于校验 app 向量表里的初始栈顶是否落在合法 RAM（跳转前的关键校验）
 * 板级布局参数，**不提供默认值**：给错默认会导致"编译通过但 boot 误判 app SP"，
 * 比编译失败难查得多。请由外部配置头（config.h / mini_boot_config.h）或 CMake 的
 * CONFIG_SRAM_* 注入提供。
 * STM32F407: SRAM 从 0x20000000 起，共 128KB(112K+16K) => 末尾 0x20020000
 */
#if defined(CONFIG_SRAM_START_ADDR)
#define SRAM_START_ADDR CONFIG_SRAM_START_ADDR
#elif defined(SRAM_START_ADDR)
#else
#error "SRAM_START_ADDR 未定义：请提供板级 RAM 起始地址（外部配置头或 -DCONFIG_SRAM_START_ADDR）"
#endif

#if defined(CONFIG_SRAM_SIZE)
#define SRAM_SIZE        CONFIG_SRAM_SIZE
#elif defined(SRAM_SIZE)
#else
#error "SRAM_SIZE 未定义：请提供板级 RAM 大小（外部配置头或 -DCONFIG_SRAM_SIZE）"
#endif

#define SRAM_END_ADDR    (SRAM_START_ADDR + SRAM_SIZE)

/* app 分区的地址/长度不在这里：分区布局属于板级工程（flash_area 表 / 厂商链接脚本），
 * boot 跳转用的 app 区域由平台通过 mini_boot_app_area_t 传入（见 boot.h）。
 */

/* ---------- OTA 能力开关（静态能力，构建期定死） ----------
 * 双分区（image_0/image_1）是 flash 布局能力，运行期不可能改变：统一由本宏决定，
 * 上层只读（ota_is_double()），不要再引入对应的运行期开关位（避免一个概念两个来源）。
 * 推荐在外部配置文件（config.h / mini_boot_config.h）里定义，或 -DOTA_DUAL_PARTITION=1；
 * 纯功能开关不要堆进 CMake：构建脚本会变重，且容易与配置头不一致。
 */
#if defined(CONFIG_OTA_DUAL_PARTITION)
#define OTA_DUAL_PARTITION  CONFIG_OTA_DUAL_PARTITION
#elif defined(OTA_DUAL_PARTITION)
#else
#define OTA_DUAL_PARTITION  0
#endif

#define CONFIG_CRC_ENABLE 1
#if CONFIG_CRC_ENABLE
#if CONFIG_CRC_TABLE_SIZE
#define CRC_TABLE_SIZE CONFIG_CRC_TABLE_SIZE
#else
#define CRC_TABLE_SIZE    256 /* 256 项表不允许改数值 这里这样只是默认以后可能会变成增大但是现在不允许改 */
#endif

#if defined(__cplusplus) || defined(_MSC_VER)
/* MSVC 的 C 模式默认不带 _Static_assert(需 /std:c11), PC 端直编时跳过 */
#else
_Static_assert(CRC_TABLE_SIZE == 256, "CRC_TABLE_SIZE must be 256");
#endif

#define CRC_MODE_BITWISE 0
#define CRC_MODE_TABLE 1
#if CONFIG_CRC_MODE
#define CRC_MODE CONFIG_CRC_MODE
#else
#define CRC_MODE    CRC_MODE_TABLE
#endif

#endif /* CONFIG_CRC_ENABLE */

/* ---------- 镜像读取能力开关 ----------
 * IMAGE_CRYPTO_ENABLE: 1 编入 SHA/HMAC/AES 分支, 支持 SHA/GCM/CBC/CBC_SHA 镜像(需链接 mbedcrypto);
 *                      0 只支持 CRC 模式, read.c 里的解密代码整段不编。
 * 注意: 关闭后还要 -ffunction-sections -fdata-sections + -Wl,--gc-sections 才能剔除 mbedtls。
 */
#if defined(CONFIG_IMAGE_CRYPTO)
#define IMAGE_CRYPTO_ENABLE     CONFIG_IMAGE_CRYPTO
#elif defined(IMAGE_CRYPTO_ENABLE)
#else
#define IMAGE_CRYPTO_ENABLE     1
#endif

/* ---------- 镜像 CRC 模型参数 ----------
 * 镜像校验使用的 CRC 模型，必须与 tools/main.py 打包时的参数一致
 * (--crc_init/--crc_refin/--crc_refout/--crc_xor_out/--crc_poly)，默认标准 CRC-32。
 * 改模型: 直接改下面 #else 分支的默认值, 或在别处定义 CRC_MODEL_XXX / CONFIG_CRC_XXX。
 */
#if defined(CONFIG_CRC_INIT)
#define CRC_MODEL_INIT      CONFIG_CRC_INIT
#elif defined(CRC_MODEL_INIT)
#else
#define CRC_MODEL_INIT      0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_REFIN)
#define CRC_MODEL_REFIN     CONFIG_CRC_REFIN
#elif defined(CRC_MODEL_REFIN)
#else
#define CRC_MODEL_REFIN     1
#endif

#if defined(CONFIG_CRC_REFOUT)
#define CRC_MODEL_REFOUT    CONFIG_CRC_REFOUT
#elif defined(CRC_MODEL_REFOUT)
#else
#define CRC_MODEL_REFOUT    1
#endif

#if defined(CONFIG_CRC_XOR_OUT)
#define CRC_MODEL_XOR_OUT   CONFIG_CRC_XOR_OUT
#elif defined(CRC_MODEL_XOR_OUT)
#else
#define CRC_MODEL_XOR_OUT   0xFFFFFFFFu
#endif

#if defined(CONFIG_CRC_POLY)
#define CRC_MODEL_POLY      CONFIG_CRC_POLY
#elif defined(CRC_MODEL_POLY)
#else
#define CRC_MODEL_POLY      0x04C11DB7u
#endif

#if defined(CONFIG_CRC_WIDTH)
#define CRC_MODEL_WIDTH     CONFIG_CRC_WIDTH
#elif defined(CRC_MODEL_WIDTH)
#else
#define CRC_MODEL_WIDTH     32u
#endif

#if defined (CONFIG_MINI_BOOT_LOAD_MAX)
#define MINI_BOOT_LOAD_MAX CONFIG_MINI_BOOT_LOAD_MAX
#elif defined (MINI_BOOT_LOAD_MAX)
#else
#define MINI_BOOT_LOAD_MAX 512/*默认页大小 */
#endif
#endif /* BOOT_CONFIG_H */
