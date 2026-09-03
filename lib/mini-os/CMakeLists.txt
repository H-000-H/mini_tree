#mini-os static library cmake file
#author: H-OOO-H
#copyright: SPDX-License-Identifier: Apache-2.0
#file: CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

set(CMAKE_PROJECT_NAME mini-os)

project(${CMAKE_PROJECT_NAME} C ASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT MINI_OS_MCU)
    set(MINI_OS_MCU cortex-m3 CACHE STRING "ARM Cortex-M core (cortex-m0/m0+/m3/m4/m7)")
endif()

set(MINI_OS_PORT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/arch/arm/cortex-m/port)

set(MINI_OS_INC
  ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

set(MINI_OS_SRC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/memory.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/queue.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/schedule.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/thread.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mutex.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/semaphore.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/timer.c
  ${MINI_OS_PORT_DIR}/port.c
  ${MINI_OS_PORT_DIR}/port.S
)


if(NOT DEFINED MINI_TREE_CONFIG)
    set(MINI_TREE_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../../.config")
endif()
set(MINI_OS_BUILD_EVENT ON)
if(EXISTS "${MINI_TREE_CONFIG}")
    # CONFIG_OSAL_EVENT 是兜底触发条件: OSAL_EVENT 会 select MINI_OS_EVENT,
    # 而根 CMakeLists 已在配置期跑 genconfig.py 把 Kconfig 求值结果 (含 select
    # 出来的符号) 回写 .config, 所以正常路径下 CONFIG_MINI_OS_EVENT=y 本来就在。
    # 额外认 CONFIG_OSAL_EVENT 只为覆盖两种绕过回写的情况: 单独 cmake 构建
    # lib/mini-os (不经过根 CMakeLists, 没有配置期回写)、以及用户手改 .config
    # 后尚未重配。漏判的后果是 src/event.c 不编入, 而 osal_mini_os.c 已经在调
    # mini_os_event_* -> 静态库阶段不报错, 链接 ELF 时才缺符号。
    file(STRINGS "${MINI_TREE_CONFIG}" _mini_os_event REGEX "^(CONFIG_MINI_OS_EVENT|CONFIG_OSAL_EVENT)=y$")
    if(NOT _mini_os_event)
        set(MINI_OS_BUILD_EVENT OFF)
    endif()
endif()
if(MINI_OS_BUILD_EVENT)
    list(APPEND MINI_OS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/event.c)
    message(STATUS "mini-os: event group ON (CONFIG_MINI_OS_EVENT / CONFIG_OSAL_EVENT)")
else()
    message(STATUS "mini-os: event group OFF (src/event.c not compiled)")
endif()

add_library(${CMAKE_PROJECT_NAME}
    STATIC ${MINI_OS_SRC}
)

target_include_directories(
  ${CMAKE_PROJECT_NAME} PUBLIC ${MINI_OS_INC}
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    target_compile_options(
        ${CMAKE_PROJECT_NAME} PRIVATE
        -mcpu=${MINI_OS_MCU}
        -mthumb
    )
endif()

target_compile_definitions(
  ${CMAKE_PROJECT_NAME} PRIVATE
)
