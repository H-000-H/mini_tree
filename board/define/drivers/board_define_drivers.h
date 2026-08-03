/* SPDX-License-Identifier: Apache-2.0 */
/*
 * 产品驱动板级配置 — 说明 (drivers)
 *
 * 当前 37 个产品驱动**没有板级参数宏**：
 *   - 实例数量自动 = DTC_GEN_COUNT_*（dtc-lite 数 DTS 同名 compatible 节点，
 *     缺省 1）——DTS 声明几个节点就有几个实例，与 vfs 池同一套逻辑
 *   - 器件参数走 DTS 属性（模板见 board/dtsi/drivers/ 下的 *.dtsi 文件），如
 *     max98357a 的 active-level、sg90 的 channel、st7789 的 width/height 等
 *   - 板级只需在 DTS 声明节点，无需任何 C 配置
 *
 * 预留扩展：未来某驱动需要板级编译期参数时，在本目录建
 * board_define_<chip>.h（#ifndef 兜底模式），由驱动 .c include。
 *
 * Product-driver board config — notes
 * The 37 product drivers currently expose **no board-level macros**:
 *   - instance counts are automatic via DTC_GEN_COUNT_* (dtc-lite counts
 *     DTS nodes with the same compatible; default 1) — same logic as VFS pools
 *   - device parameters come from DTS properties (see the *.dtsi templates under
 *     board/dtsi/drivers/),
 *     e.g. max98357a active-level, sg90 channel, st7789 width/height
 * Reserved: when a driver needs compile-time board knobs, add
 * board_define_<chip>.h here (#ifndef fallback pattern) and include it
 * from the driver.
 */
#ifndef BOARD_DEFINE_DRIVERS_H
#define BOARD_DEFINE_DRIVERS_H

/* （暂无板级宏；预留区 / no board macros yet — reserved） */

#endif /* BOARD_DEFINE_DRIVERS_H */
