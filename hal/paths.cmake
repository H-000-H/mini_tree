# HAL 子目录 include 路径 (供 core/system 等待 PRIVATE 引用)
# 纯中间件：HAL 实现由具体项目提供，此处仅暴露接口头文件目录
get_filename_component(_HAL_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_INCLUDE_DIRS
    "${_HAL_ROOT}/gpio"
    "${_HAL_ROOT}/cpu"
    "${_HAL_ROOT}/pwm"
    "${_HAL_ROOT}/analog"
    "${_HAL_ROOT}/storage"
    "${_HAL_ROOT}/system"
)
