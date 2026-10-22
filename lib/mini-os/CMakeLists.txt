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
  ${MINI_OS_PORT_DIR}/port.c
  ${MINI_OS_PORT_DIR}/port.S
)


if(NOT DEFINED MINI_TREE_CONFIG)
    set(MINI_TREE_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/../../.config")
endif()
set(MINI_OS_BUILD_EVENT ON)
if(EXISTS "${MINI_TREE_CONFIG}")
    # The event-group switch honours CONFIG_MINI_OS_EVENT only: Kconfig defaults it to
    # off and no symbol selects it (event groups are not part of the unified backend
    # interface; enable it manually when needed). The top-level CMakeLists runs
    # genconfig.py at configure time and writes the evaluated Kconfig result back to
    # .config, so the value read here is the effective one. If this check is missed,
    # src/event.c is not compiled in and a direct mini_os_event_* call only surfaces
    # as an undefined symbol at ELF link time, not when building the static library.
    file(STRINGS "${MINI_TREE_CONFIG}" _mini_os_event REGEX "^CONFIG_MINI_OS_EVENT=y$")
    if(NOT _mini_os_event)
        set(MINI_OS_BUILD_EVENT OFF)
    endif()
endif()
if(MINI_OS_BUILD_EVENT)
    list(APPEND MINI_OS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/event.c)
    message(STATUS "mini-os: event group ON (CONFIG_MINI_OS_EVENT)")
else()
    message(STATUS "mini-os: event group OFF (src/event.c not compiled)")
endif()

set(MINI_OS_BUILD_TIMER ON)
if(EXISTS "${MINI_TREE_CONFIG}")
    # The software-timer switch defaults to off; enable it manually
    # (CONFIG_MINI_OS_TIMER=y). When off, src/timer.c is not compiled in and
    # SysTick never calls mini_os_timer_tick().
    file(STRINGS "${MINI_TREE_CONFIG}" _mini_os_timer REGEX "^CONFIG_MINI_OS_TIMER=y$")
    if(NOT _mini_os_timer)
        set(MINI_OS_BUILD_TIMER OFF)
    endif()
endif()
if(MINI_OS_BUILD_TIMER)
    list(APPEND MINI_OS_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/timer.c)
    message(STATUS "mini-os: software timer ON (CONFIG_MINI_OS_TIMER)")
else()
    message(STATUS "mini-os: software timer OFF (src/timer.c not compiled)")
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
