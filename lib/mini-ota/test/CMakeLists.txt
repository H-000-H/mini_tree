# PC 端 OTA 测试台（宿主机编译运行，与固件工程解耦，不参与根 CMakeLists 的 MCU 构建）
# 两个目标:
#   main     运行时菜单选择测试项（布局/保真/镜像校验/下载/全流程/掉电）
#   pc_check 镜像文件校验工具（命令行: pc_check.exe <image.bin> <plain.bin>）
# 用法:
#   cmake -S test -B test/build -G Ninja
#   cmake --build test/build
#   运行(文件类测试按相对路径读 bin, 建议在 test 目录下运行):
#   .\build\main.exe
#   .\build\pc_check.exe image.bin test.bin
# 需要 SHA/GCM/CBC 解密支持时:
#   cmake -S test -B test/build -G Ninja -DTOOL_ENABLE_CRYPTO=ON
cmake_minimum_required(VERSION 3.16)

project(pc_test C CXX)

# 板级布局参数：boot_config.h 不再提供默认值，主机测试用一组 stub 值
add_compile_definitions(
    CONFIG_SRAM_START_ADDR=0x20000000
    CONFIG_SRAM_SIZE=0x00020000
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 默认只支持 CRC 模式: 不用编 mbedtls
option(TOOL_ENABLE_CRYPTO "编入 SHA/HMAC/AES 解密支持(需编译 mbedtls, 会慢一些)" OFF)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

# 单入口测试台：main.cpp 运行时菜单选择测试项（布局/保真/镜像/下载/全流程/掉电）
# 跑真实 start.c / read.c / flash.c / ota_state*.c 代码路径，不是复刻逻辑
add_executable(main
    main.cpp
    ${ROOT}/bootutil/src/start.c
    ${ROOT}/bootutil/src/read.c
    ${ROOT}/bootutil/src/flash.c
    ${ROOT}/bootutil/src/ota_state.c
    ${ROOT}/bootutil/src/ota_state_flash.c
    ${ROOT}/bootutil/src/boot_keys.c
    ${ROOT}/bootutil/src/err.c
    ${ROOT}/algorithm/src/crc.c
)

target_include_directories(main PRIVATE
    ${ROOT}/bootutil/inc
    ${ROOT}/algorithm/inc
)

# 双分区（回滚机制需要）；加密默认关，纯 CRC 免编 mbedtls
target_compile_definitions(main PRIVATE
    IMAGE_CRYPTO_ENABLE=$<IF:$<BOOL:${TOOL_ENABLE_CRYPTO}>,1,0>
    CONFIG_OTA_DUAL_PARTITION=1
)

# 镜像文件校验工具（独立命令行）：解析/校验走 read.c，本文件只读文件与打印
add_executable(pc_check
    check.cpp
    ${ROOT}/bootutil/src/read.c
    ${ROOT}/bootutil/src/err.c
    ${ROOT}/algorithm/src/crc.c
)

target_include_directories(pc_check PRIVATE
    ${ROOT}/bootutil/inc
    ${ROOT}/algorithm/inc
)

target_compile_definitions(pc_check PRIVATE
    IMAGE_CRYPTO_ENABLE=$<IF:$<BOOL:${TOOL_ENABLE_CRYPTO}>,1,0>
)

if(TOOL_ENABLE_CRYPTO)
    add_library(mbedcrypto STATIC
        ${ROOT}/lib/mbedtls/library/aes.c
        ${ROOT}/lib/mbedtls/library/cipher.c
        ${ROOT}/lib/mbedtls/library/cipher_wrap.c
        ${ROOT}/lib/mbedtls/library/constant_time.c
        ${ROOT}/lib/mbedtls/library/gcm.c
        ${ROOT}/lib/mbedtls/library/md.c
        ${ROOT}/lib/mbedtls/library/sha256.c
        ${ROOT}/lib/mbedtls/library/platform_util.c
    )

    target_include_directories(mbedcrypto PUBLIC
        ${ROOT}/lib/mbedtls/include
        ${ROOT}/configs
    )

    target_compile_definitions(mbedcrypto PUBLIC
        MBEDTLS_CONFIG_FILE="mbedtls_config_boot.h"
    )

    target_sources(main PRIVATE
        ${ROOT}/algorithm/src/aes.c
        ${ROOT}/algorithm/src/hmac.c
        ${ROOT}/algorithm/src/sha.c
    )
    target_link_libraries(main PRIVATE mbedcrypto)

    target_sources(pc_check PRIVATE
        ${ROOT}/algorithm/src/aes.c
        ${ROOT}/algorithm/src/hmac.c
        ${ROOT}/algorithm/src/sha.c
    )
    target_link_libraries(pc_check PRIVATE mbedcrypto)
endif()
