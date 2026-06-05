#ifndef BOARD_HANDLES_H
#define BOARD_HANDLES_H

#include "board_nodes.h"

/*
 * board_handles.h — 编译期确定的句柄宏
 *
 * 用于依赖注入: app 通过 chosen/alias 宏获取设备 ID,
 * 再通过 board_dev_get(id) 获取 device_t*。
 *
 * 此文件取代 getInstance()/Service Locator 模式。
 */

#endif /* BOARD_HANDLES_H */
