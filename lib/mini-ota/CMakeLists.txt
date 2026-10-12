cmake_minimum_required(VERSION 3.16)

set(CMAKE_PROJECT_NAME mini_ota)

#clangd要吃长路径
# 仅独立构建时探测并锁定工具链；作为子目录被 mini_tree (add_subdirectory) 编入时
# 必须沿用父工程工具链，不能 FORCE 覆盖 CMAKE_C_COMPILER / CMAKE_ASM_COMPILER。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(MINI_BOOT_GCC_JUNCTION "${CMAKE_CURRENT_SOURCE_DIR}/.arm-gcc/bin/arm-none-eabi-gcc.exe")
    if(EXISTS "${MINI_BOOT_GCC_JUNCTION}")
        set(CMAKE_C_COMPILER   "${MINI_BOOT_GCC_JUNCTION}" CACHE FILEPATH "C compiler (space-free path)" FORCE)
        set(CMAKE_ASM_COMPILER "${MINI_BOOT_GCC_JUNCTION}" CACHE FILEPATH "ASM compiler (space-free path)" FORCE)
    else()
        find_program(MINI_BOOT_GCC_PATH arm-none-eabi-gcc)
        if(MINI_BOOT_GCC_PATH)
            set(CMAKE_C_COMPILER   "${MINI_BOOT_GCC_PATH}" CACHE FILEPATH "C compiler" FORCE)
            set(CMAKE_ASM_COMPILER "${MINI_BOOT_GCC_PATH}" CACHE FILEPATH "ASM compiler" FORCE)
        endif()
    endif()
endif()

project(${CMAKE_PROJECT_NAME} C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 目标核由构建方提供：本仓库不代管 -mcpu，也不注入 MINI_BOOT_Mx。
# 板级工程/父工程自行传 -mcpu=cortex-mX -mthumb 并定义 MINI_BOOT_M0/M0P/M3/M4/M7 之一
# （arch/arm/cortex-m/cortex_m.S 靠该宏选择核差异；漏定义会被汇编里的 #error 拦住）。

# 仅为 IDE/clangd：把工具链的 sysroot 头路径写进 compile_commands.json 不要碰这个这个是使用clangd必须有的
get_filename_component(ARM_TOOLCHAIN_ROOT "${CMAKE_C_COMPILER}/../.." ABSOLUTE)
file(GLOB ARM_GCC_INTERNAL_INC "${ARM_TOOLCHAIN_ROOT}/lib/gcc/arm-none-eabi/*/include")
set(ARM_TOOLCHAIN_SYS_INC
    ${ARM_GCC_INTERNAL_INC}
    ${ARM_TOOLCHAIN_ROOT}/arm-none-eabi/include
)
foreach(_sys_inc IN LISTS ARM_TOOLCHAIN_SYS_INC)
    if(EXISTS "${_sys_inc}")
        add_compile_options("-I${_sys_inc}")
    endif()
endforeach()

# 镜像加密 (SHA/GCM/CBC/CBC_SHA) 由 mini_tree 的 CONFIG_IMAGE_CRYPTO 决定;
# 独立构建时默认开 (与 boot_config.h 的 IMAGE_CRYPTO_ENABLE 默认一致)。
if(NOT DEFINED MINI_TREE_OTA_CRYPTO)
    set(MINI_TREE_OTA_CRYPTO ON)
endif()

file(GLOB MINI_BOOT_SRC CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/bootutil/src/*.c
)
# CRC 始终需要; SHA/HMAC/AES 仅在加密开启时编入 (read.c / boot_keys.c 有 #if 守卫)
list(APPEND MINI_BOOT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/src/crc.c)
if(MINI_TREE_OTA_CRYPTO)
    list(APPEND MINI_BOOT_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/src/aes.c
        ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/src/hmac.c
        ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/src/sha.c
    )
endif()

set(MINI_BOOT_INC
    ${CMAKE_CURRENT_SOURCE_DIR}/bootutil/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/algorithm/inc
)

# TODO: 此处预留自定义命令（当前为空调用会导致 CMake 报错，暂注释）
# add_custom_command(
#
# )

add_library(${CMAKE_PROJECT_NAME}
STATIC  ${MINI_BOOT_SRC}
bootutil/inc/read.h
bootutil/src/read.c
bootutil/inc/start.h
bootutil/inc/boot_keys.h
)

target_include_directories(${CMAKE_PROJECT_NAME}
PUBLIC  ${MINI_BOOT_INC}
)

# ==== 体系结构层 arch/arm/cortex-m：核相关原语（汇编实现，与芯片厂商无关）====
set(MINI_BOOT_ARCH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/cortex-m)
file(GLOB MINI_BOOT_ARCH_SRC CONFIGURE_DEPENDS
    ${MINI_BOOT_ARCH_DIR}/*.S
)
add_library(arch STATIC ${MINI_BOOT_ARCH_SRC})
target_include_directories(arch
PUBLIC  ${MINI_BOOT_ARCH_DIR}
)
target_link_libraries(${CMAKE_PROJECT_NAME}
PUBLIC arch
)

# ==== 镜像加密依赖 (仅 IMAGE_CRYPTO 开启时) ====
# 自带 lib/mbedtls 有源码则用自带的精简 8 文件集; 否则在 mini_tree 构建中复用
# 工程级 mbedtls 依赖 (本地 lib/mbedtls 或 FetchContent), 使用 OTA 的 mbedtls 配置。
if(MINI_TREE_OTA_CRYPTO)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls/library/aes.c")
        set(MBEDTLS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/mbedtls)
        add_library(mbedcrypto STATIC
            ${MBEDTLS_DIR}/library/aes.c
            ${MBEDTLS_DIR}/library/cipher.c
            ${MBEDTLS_DIR}/library/cipher_wrap.c
            ${MBEDTLS_DIR}/library/constant_time.c
            ${MBEDTLS_DIR}/library/gcm.c
            ${MBEDTLS_DIR}/library/md.c
            ${MBEDTLS_DIR}/library/sha256.c
            ${MBEDTLS_DIR}/library/platform_util.c
        )
        set(MBEDTLS_CFG_FILE "\"mbedtls_config_boot.h\"")
        set(MBEDTLS_BOOT_INC ${MBEDTLS_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/configs)
        target_include_directories(mbedcrypto PRIVATE ${MBEDTLS_BOOT_INC})
        target_compile_definitions(mbedcrypto PRIVATE MBEDTLS_CONFIG_FILE=${MBEDTLS_CFG_FILE})
        # read.c / boot_keys.c 会 include mbedtls 头：同样 PRIVATE 注入，不传播给消费者
        target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${MBEDTLS_BOOT_INC})
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MBEDTLS_CONFIG_FILE=${MBEDTLS_CFG_FILE})
        # 链接保持 PUBLIC：静态库，消费者需一起链 mbedcrypto
        target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC mbedcrypto)
    elseif(COMMAND mini_tree_link_mbedtls)
        mini_tree_link_mbedtls(${CMAKE_PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/configs")
    else()
        message(FATAL_ERROR
            "mini-ota: 镜像加密已开启, 但既无自带 lib/mbedtls/library/aes.c, "
            "也不在 mini_tree 构建中 (无 mini_tree_link_mbedtls)")
    endif()
endif()
# SRAM 区间：boot 用它校验 app 向量表的初始 SP 是否落在合法 RAM（boot.c），
# 必须与板级链接脚本的 RAM 区间一致。独立构建时由 CMake 以 -DCONFIG_SRAM_* 注入;
if(NOT DEFINED MINI_TREE_OTA)
    set(SRAM_START_ADDR 0x20000000 CACHE STRING "Start address of SRAM" FORCE)
    set(SRAM_SIZE 0x00020000 CACHE STRING "Size of SRAM in bytes" FORCE)
    add_compile_definitions(
        CONFIG_SRAM_START_ADDR=${SRAM_START_ADDR}
        CONFIG_SRAM_SIZE=${SRAM_SIZE}
    )
endif()

# 链接脚本不在这里生成：由板级工程提供（各厂商不同），本仓库不产出也不注入 .ld

# 平台层（ports/<core>、启动文件、厂商寄存器定义）由板级工程自行构建并链接，
# 本仓库只提供与厂商无关的核心层 + 体系结构层（arch/arm/cortex-m）
