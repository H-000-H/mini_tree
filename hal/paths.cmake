# HAL 子目录 include 路径 (供 core/system 等待 PRIVATE 引用)
get_filename_component(_HAL_ROOT "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(HAL_INCLUDE_DIRS
    "${_HAL_ROOT}/gpio"
    "${_HAL_ROOT}/amp"
    "${_HAL_ROOT}/tim"
    "${_HAL_ROOT}/adc"
    "${_HAL_ROOT}/dac"
    "${_HAL_ROOT}/storage"
    "${_HAL_ROOT}/system"
    "${_HAL_ROOT}/rtc"
    "${_HAL_ROOT}/i2s"
    "${_HAL_ROOT}/iwdg"
    "${_HAL_ROOT}/wwdg"
    "${_HAL_ROOT}/can"
    "${_HAL_ROOT}/usb"
)
