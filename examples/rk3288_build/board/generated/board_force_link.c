#include "device.h"

/* ===== 自动生成 — 由 dtc-lite.py 扫描 DRIVER_REGISTER 宏生成 ===== */
/* 每新增驱动, 重新构建即可自动更新此文件, 无需手动编辑.          */

extern int __attribute__((weak)) board_driver_probe_board_safety_hw(device_t*);

static volatile void* s_fake_ref;

static void __attribute__((constructor, used)) _force_probe_link(void)
{
    s_fake_ref = (void*)board_driver_probe_board_safety_hw;
}
