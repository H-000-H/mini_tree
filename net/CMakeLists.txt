file(GLOB MINI_TREE_NET_PORT_SRCS 
    "${CMAKE_CURRENT_SOURCE_DIR}/port/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/pppif/*.c"
)

add_library(mini_tree_net STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/sys/sys_arch.c"
    ${MINI_TREE_NET_PORT_SRCS}
    port/pppif/pppif.h
)

target_include_directories(mini_tree_net PUBLIC
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/port"
    "${CMAKE_SOURCE_DIR}/lib/lwip/src/include"
    "${CMAKE_SOURCE_DIR}/osal/include"
    "${CMAKE_SOURCE_DIR}/core/include"
    "${CMAKE_SOURCE_DIR}/board/include"
    "${CMAKE_SOURCE_DIR}/hal/gpio"
    "${CMAKE_SOURCE_DIR}/system_c/include"
    "${CMAKE_SOURCE_DIR}/time_slice/task"
    "${CMAKE_BINARY_DIR}/generated/kconfig/mini_tree"
    "${CMAKE_BINARY_DIR}/generated/board/mini_tree"
)
mini_tree_link_lwip(mini_tree_net "${CMAKE_CURRENT_SOURCE_DIR}")