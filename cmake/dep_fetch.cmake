# mini_tree_dep_get — local lib/<name> first, else FetchContent download.
# lib/ 仅保留 RTOS（freeRTOS / rtthread），其余基础设施均通过本函数按需拉取。
include_guard(GLOBAL)
include(FetchContent)

# ── 离线模式 ──
# ON 时只认本地 lib/<name>，缺失立即报错并给出放置路径，绝不联网。
# FetchContent 是在 configure 阶段同步联网的，一次 SSL 抖动/断流就会让整个 configure
# 失败，且 `_deps` 里留下的半截克隆会让后续 clone 反复失败。离线模式下把这类故障
# 降级成一条可直接执行的提示。
#
# 优先级: 显式 -D > 环境变量 > 默认 OFF
#   单次放行: -DMINI_TREE_DEPS_OFFLINE=OFF  (盖过环境变量)
#   强制离线: -DMINI_TREE_DEPS_OFFLINE=ON
#   跟随环境: 不传，或 -DMINI_TREE_DEPS_OFFLINE=AUTO（默认）
# 用 AUTO 作默认值，是为了能区分"用户显式写了 OFF"与"没写所以是 OFF"，
# 否则 OFF 既是默认值又是指令，无法实现参数优先（CMAKE_ARGV 只在 -P 脚本模式可用）。
set(MINI_TREE_DEPS_OFFLINE AUTO CACHE STRING
    "Offline dependency fetching: AUTO (follow env) | ON (never fetch) | OFF (allow fetch)")

set(_mini_tree_offline_raw "${MINI_TREE_DEPS_OFFLINE}")
if("${MINI_TREE_DEPS_OFFLINE}" STREQUAL "AUTO")
    if(DEFINED ENV{MINI_TREE_DEPS_OFFLINE} AND NOT "$ENV{MINI_TREE_DEPS_OFFLINE}" STREQUAL "")
        set(_mini_tree_offline_raw "$ENV{MINI_TREE_DEPS_OFFLINE}")
        set(_mini_tree_offline_src "environment")
    else()
        set(_mini_tree_offline_raw "OFF")
        set(_mini_tree_offline_src "default")
    endif()
else()
    set(_mini_tree_offline_src "cache/-D")
endif()

if(_mini_tree_offline_raw)
    set(MINI_TREE_DEPS_OFFLINE_EFFECTIVE ON)
else()
    set(MINI_TREE_DEPS_OFFLINE_EFFECTIVE OFF)
endif()

# mini_tree_dep_get(<out_source_dir>
#   NAME <id>
#   LOCAL_DIR <path>
#   MARKER <relative-path-under-LOCAL_DIR-or-fetched-root>
#   GIT_REPOSITORY <url>
#   GIT_TAG <tag-or-commit>
#   [GIT_SUBMODULES_RECURSE]
# )
function(mini_tree_dep_get out_source_dir)
    set(options GIT_SUBMODULES_RECURSE)
    set(oneValueArgs NAME LOCAL_DIR MARKER GIT_REPOSITORY GIT_TAG)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "" ${ARGN})

    if(NOT ARG_NAME OR NOT ARG_LOCAL_DIR OR NOT ARG_MARKER OR NOT ARG_GIT_REPOSITORY OR NOT ARG_GIT_TAG)
        message(FATAL_ERROR "mini_tree_dep_get: NAME LOCAL_DIR MARKER GIT_REPOSITORY GIT_TAG required")
    endif()

    if(EXISTS "${ARG_LOCAL_DIR}/${ARG_MARKER}")
        message(STATUS "mini_tree ${ARG_NAME}: local ${ARG_LOCAL_DIR}")
        set(${out_source_dir} "${ARG_LOCAL_DIR}" PARENT_SCOPE)
        return()
    endif()

    set(_fc_name "mini_tree_fc_${ARG_NAME}")
    string(TOLOWER "${_fc_name}" _fc_name_lower)

    # 离线模式下绝不联网，直接把"缺什么、放哪里"讲清楚
    if(MINI_TREE_DEPS_OFFLINE_EFFECTIVE)
        message(FATAL_ERROR
            "mini_tree ${ARG_NAME}: offline mode is ON and no local copy was found.\n"
            "  looked for : ${ARG_LOCAL_DIR}/${ARG_MARKER}\n"
            "  populate it: git clone --depth 1 --branch ${ARG_GIT_TAG} ${ARG_GIT_REPOSITORY} \"${ARG_LOCAL_DIR}\"\n"
            "               (or a symlink pointing at an existing checkout)\n"
            "  or allow fetching for this configure run: -DMINI_TREE_DEPS_OFFLINE=OFF\n"
            "  (effective value \"${_mini_tree_offline_raw}\", source: ${_mini_tree_offline_src})")
    endif()

    # 半截克隆检测: 上一次 FetchContent 中断会留下 _deps/<name>-src 但没有 MARKER，
    # 此时继续 clone 往往报 "unexpected disconnect" 或 "Had to git clone more than once"。
    set(_stale_dir "${FETCHCONTENT_BASE_DIR}/${_fc_name_lower}-src")
    if(EXISTS "${_stale_dir}" AND NOT EXISTS "${_stale_dir}/${ARG_MARKER}")
        message(WARNING
            "mini_tree ${ARG_NAME}: stale/incomplete fetched copy detected at ${_stale_dir}\n"
            "  (marker '${ARG_MARKER}' missing). Removing it before re-fetch.\n"
            "  If this repeats, set MINI_TREE_DEPS_OFFLINE=ON and provide ${ARG_LOCAL_DIR} manually.")
        file(REMOVE_RECURSE "${_stale_dir}")
    endif()

    set(_declare_args
        GIT_REPOSITORY "${ARG_GIT_REPOSITORY}"
        GIT_TAG        "${ARG_GIT_TAG}"
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    if(ARG_GIT_SUBMODULES_RECURSE)
        list(APPEND _declare_args GIT_SUBMODULES_RECURSE TRUE)
    endif()

    FetchContent_Declare(${_fc_name} ${_declare_args})
    FetchContent_GetProperties(${_fc_name})
    if(NOT ${_fc_name_lower}_POPULATED)
        message(STATUS "mini_tree ${ARG_NAME}: FetchContent ${ARG_GIT_REPOSITORY} @ ${ARG_GIT_TAG}")
        # 本工程刻意只用 FetchContent_Populate 取 SOURCE_DIR，不调用 add_subdirectory
        # (TinyUSB/lwIP 自带 CMakeLists 在父工程 project() 后执行会触发 CMP0000 硬错误)。
        # CMake 4.x 将 "FetchContent_Populate(name)" 标记弃用并建议改用 MakeAvailable，
        # 但 MakeAvailable 必然 add_subdirectory，与本设计冲突。按 CMP0169 文档允许的
        # 过渡方式，在此作用域显式回退到 OLD 语义以消除 dev 警告，待后续迁移 ExternalProject。
        if(POLICY CMP0169)
            cmake_policy(SET CMP0169 OLD)
        endif()
        FetchContent_Populate(${_fc_name})
    endif()

    set(_src "${${_fc_name_lower}_SOURCE_DIR}")
    if(NOT EXISTS "${_src}/${ARG_MARKER}")
        message(FATAL_ERROR
            "mini_tree ${ARG_NAME}: marker '${ARG_MARKER}' missing after fetch at ${_src}\n"
            "  the clone may be incomplete — delete that directory and re-configure,\n"
            "  or provide a local copy at ${ARG_LOCAL_DIR} and build with MINI_TREE_DEPS_OFFLINE=ON")
    endif()
    set(${out_source_dir} "${_src}" PARENT_SCOPE)
endfunction()
