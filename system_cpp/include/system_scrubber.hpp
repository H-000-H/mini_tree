#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash 固件位腐烂巡检 (IEC 62304 §5.7 / FDA Class III 运行时自诊断)
 *
 * SPI Flash 在 X 光/高温环境长期运行后电荷流失, 代码段指令翻转.
 * 后台超低优先级任务以 ~1KB/s 速度遍历整个 app 分区,
 * 计算 CRC32 并与出厂基线比对. 失配 → Safe State, 强制返厂.
 *
 * 用法:
 *   system_scrubber_init();     // 注册低优任务
 *   system_scrubber_start();    // 在系统就绪后启动
 */

bool system_scrubber_init(void);
bool system_scrubber_start(void);
bool system_scrubber_is_running(void);

#ifdef __cplusplus
}
#endif