#!/usr/bin/env python3
"""将 ESP32 mini_tree 目录结构/共享文件同步到 STM32 / CH32 目标树。

用法:
  python sync_from_esp_baseline.py stm32
  python sync_from_esp_baseline.py ch32
  python sync_from_esp_baseline.py all

平台特有文件 (GPIO 实现、DTS 等) 不会被覆盖。
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
ESP_ROOT = REPO / "ESP32-S3" / "components" / "mini_tree"

TARGETS = {
    "stm32": REPO / "STM32F407ZGT6" / "mini_tree",
    "ch32": REPO / "CH32V307" / "mini_tree",
}

# 从 ESP 整目录复制 (相对 mini_tree 根)
# 注意: vfs/spi/bus/spi_bus.c 为 ST/CH 主机专用，勿覆盖
COPY_DIRS = [
    "osal/include",
    "osal/src",
    "hal_if/analog",
    "hal_if/cpu",
    "hal_if/pwm",
    "hal_if/storage",
    "hal_if/system",
    "hal_if/uart",
    "hal_bus/bus",
    "hal_bus/can",
    "hal_bus/i2c",
    "hal_bus/i2s",
    "hal_bus/pcie",
    "hal_bus/spi",
    "hal_bus/usb",
    "vfs/spi/master",
    "vfs/spi/slave",
    "vfs/spi/include",
    "vfs/gpio",
    "vfs/uart",
    "drivers/flash",
    "board/dt-bindings/uart",
    "board/docs",
]

# 单文件复制 (src 相对 ESP mini_tree, dst 相对目标 mini_tree)
COPY_FILES = [
    ("hal_if/cpu/hal_cpu.h", "hal_if/cpu/hal_cpu.h"),
    ("hal_if/cpu/hal_cpu_delay.h", "hal_if/cpu/hal_cpu_delay.h"),
    ("hal_if/paths.cmake", "hal_if/paths.cmake"),
    ("hal_if/CMakeLists.txt", "hal_if/CMakeLists.txt"),
    ("hal_if/hal_if_dummy.c", "hal_if/hal_if_dummy.c"),
    ("hal_bus/paths.cmake", "hal_bus/paths.cmake"),
    ("hal_bus/hal_bus_dummy.c", "hal_bus/hal_bus_dummy.c"),
    ("vfs/spi/paths.cmake", "vfs/spi/paths.cmake"),
    ("tools/convert_struct_typedef.py", "tools/convert_struct_typedef.py"),
]

# 复制时跳过的文件名 (平台 HAL 实现 / ESP 专用)
SKIP_NAMES = {
    "hal_gpio_esp32.c",
    "hal_esp32s3.c",
    "hal_pulse_engine.h",
    "hal_pulse_engine_esp32s3.c",
    "ws2812_drv.c",
    "ws2812_drv.h",
    "ws2812-timing.h",
    "hal_spi.c",
    "hal_spi_stub.c",
    "hal_uart.c",
    "hal_gpio.h",
    "spi_bus.c",
}

# 同步后删除的旧布局路径 (相对目标 mini_tree)
REMOVE_PATHS = [
    "hal_inst",
    "hal_if/include",
    "hal_if/pulse",
    "drivers/ws2812",
    "board/dt-bindings/led",
    "hal_if/src",
    "hal_if/gpio/hal_pin.h",
    "hal_if/cpu/hal_cpu_fast.h",
    "hal_if/pwm/hal_pwm_fast.h",
    "hal_if/system/hal_platform_safety.c",
    "hal_bus/include",
    "hal_bus/src",
    "hal_bus/spi/hal_spi_stub.c",
    "vfs/spi/spi_bus.c",
    "vfs/spi/spi_client.c",
    "vfs/spi/spi_client.h",
    "vfs/spi/spi_vfs.h",
]

PLATFORM_GPIO = {
    "stm32": "hal_if/gpio/hal_gpio_stm32.c",
    "ch32": "hal_if/gpio/hal_gpio_ch32.c",
}

PLATFORM_SOC = {
    "stm32": ("hal/src/hal_stm32f407.c", "hal_if/soc/stm32f407/hal_stm32f407.c"),
    "ch32": ("hal_if/src/hal_ch32v307.c", "hal_if/soc/ch32v307/hal_ch32v307.c"),
}

PLATFORM_SAFETY = {}

PLATFORM_ONLY = {
    "stm32": [
        "hal_if/soc/stm32f407/hal_stm32f407.c",
        "hal_if/gpio/hal_gpio_stm32.c",
        "hal_if/cpu/hal_cpu_delay_stm32.c",
        "hal_if/system/hal_dma_stm32.c",
        "hal_if/system/hal_dma_stm32.h",
        "hal_if/uart/hal_uart_stm32.c",
        "hal_bus/spi/spi_core.c",
        "hal_bus/spi/spi_controller_stm32.c",
    ],
    "ch32": [
        "board/include/vfs_adc.h",
        "board/include/vfs_pwm.h",
        "board/include/vfs_storage.h",
        "hal_if/soc/ch32v307/hal_ch32v307.c",
        "hal_if/gpio/hal_gpio_ch32.c",
        "hal_if/cpu/hal_cpu_delay_ch32.c",
        "hal_if/system/hal_dma_ch32.c",
        "hal_if/system/hal_dma_ch32.h",
        "hal_if/uart/hal_uart_ch32.c",
        "hal_bus/spi/spi_core.c",
        "hal_bus/spi/spi_controller_ch32.c",
    ],
}


def copy_tree(src: Path, dst: Path) -> int:
    count = 0
    if not src.exists():
        return 0
    if src.is_file():
        if src.name in SKIP_NAMES:
            return 0
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        return 1
    for item in src.rglob("*"):
        if item.is_dir():
            continue
        if item.name in SKIP_NAMES:
            continue
        rel = item.relative_to(src)
        out = dst / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(item, out)
        count += 1
    return count


def migrate_file(root: Path, src_rel: str, dst_rel: str) -> bool:
    src = root / src_rel
    dst = root / dst_rel
    if not src.exists():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)
    return True


def sync_platform(name: str) -> None:
    esp = ESP_ROOT
    target = TARGETS[name]
    if not target.is_dir():
        print(f"[{name}] 目标不存在: {target}")
        return

    print(f"=== 同步 {name}: {target} <= {esp} ===")
    copied = 0

    for rel in COPY_DIRS:
        copied += copy_tree(esp / rel, target / rel)

    for src_rel, dst_rel in COPY_FILES:
        src = esp / src_rel
        if src.is_file() and src.name not in SKIP_NAMES:
            dst = target / dst_rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
            copied += 1

    # hal_cpu_amp.c: 优先保留目标已有，否则从 ESP 或旧 src/ 迁移
    amp_dst = target / "hal_if/cpu/hal_cpu_amp.c"
    if not amp_dst.exists():
        if migrate_file(target, "hal_if/src/hal_cpu_amp.c", "hal_if/cpu/hal_cpu_amp.c"):
            copied += 1
        elif (esp / "hal_if/cpu/hal_cpu_amp.c").exists():
            shutil.copy2(esp / "hal_if/cpu/hal_cpu_amp.c", amp_dst)
            copied += 1

    # 平台 SoC / safety 文件迁移
    if name in PLATFORM_SOC:
        src_rel, dst_rel = PLATFORM_SOC[name]
        if migrate_file(target, src_rel, dst_rel):
            copied += 1

    if name in PLATFORM_SAFETY:
        src_rel, dst_rel = PLATFORM_SAFETY[name]
        if migrate_file(target, src_rel, dst_rel):
            copied += 1

    # 删除旧布局
    for rel in REMOVE_PATHS:
        path = target / rel
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)
            print(f"  删除目录: {rel}")
        elif path.is_file():
            path.unlink(missing_ok=True)
            print(f"  删除文件: {rel}")

    gpio = PLATFORM_GPIO.get(name)
    if gpio and not (target / gpio).exists():
        print(f"  警告: 缺少平台 GPIO 实现 {gpio}")

    print(f"[{name}] 复制/迁移 {copied} 个文件")
    for rel in PLATFORM_ONLY.get(name, []):
        if not (target / rel).exists():
            print(f"  警告: 缺少平台文件 {rel}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Sync mini_tree from ESP32 baseline")
    parser.add_argument("target", choices=["stm32", "ch32", "all"])
    args = parser.parse_args()

    if args.target == "all":
        for name in TARGETS:
            sync_platform(name)
    else:
        sync_platform(args.target)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
