# Bus Framework 层
# 提供统一的总线控制器/客户端抽象

add_library(bus_framework STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/bus_controller.c"
    "${CMAKE_CURRENT_LIST_DIR}/src/bus_client.c"
)

target_include_directories(bus_framework PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../core/include"
    "${CMAKE_CURRENT_LIST_DIR}/../board/include"
)

target_link_libraries(bus_framework PUBLIC board core)
