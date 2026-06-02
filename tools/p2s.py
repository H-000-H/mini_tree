#!/usr/bin/env python3
"""mini_tree 一键构建脚本 — 替代手动 make/cmake 参数组合。

用法:
  python tools/p2s.py --help
  python tools/p2s.py --platform arm_cm3 --toolchain gcc --osal freertos
  python tools/p2s.py --platform arm_cm4f --toolchain keil5 --osal rtthread
  python tools/p2s.py -p arm_cm7 -t gcc -o null
  python tools/p2s.py --menuconfig                          # 先配置再构建
  python tools/p2s.py --clean                                # 清理
  python tools/p2s.py -l                                     # 列出可用组合
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from typing import Dict, List


MAKEFILE_PLATFORMS: Dict[str, str] = {
    "arm_cm3": "arm_cm3",
    "arm_cm4f": "arm_cm4f",
    "arm_cm7": "arm_cm7",
    "riscv": "riscv",
    "posix": "posix",
}

TOOLCHAINS: Dict[str, str] = {
    "gcc": "gcc",
    "clang": "clang",
    "keil5": "keil5",
    "keil6": "keil6",
}

OSALS: Dict[str, str] = {
    "freertos": "FREERTOS",
    "rtthread": "RTTHREAD",
    "null": "NULL",
}


def run_make(platform: str, toolchain: str, osal: str, freertos_heap: int = 4) -> int:
    cmd: List[str] = [
        "mingw32-make",
        f"PLATFORM={platform}",
        f"TOOLCHAIN={toolchain}",
        f"OSAL_BACKEND={osal}",
        f"FREERTOS_HEAP={freertos_heap}",
    ]
    print(f"[p2s] {' '.join(cmd)}")
    project_root: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    result: subprocess.CompletedProcess[str] = subprocess.run(
        cmd, cwd=project_root
    )
    return result.returncode


def run_menuconfig() -> int:
    print("[p2s] 启动 menuconfig ...")
    script_dir: str = os.path.dirname(os.path.abspath(__file__))
    script: str = os.path.join(script_dir, "menuconfig.py")
    result: subprocess.CompletedProcess[str] = subprocess.run(
        [sys.executable, script]
    )
    return result.returncode


def show_combinations() -> None:
    print("可用构建组合:")
    print()
    for plat in ["arm_cm3", "arm_cm4f", "arm_cm7", "riscv", "posix"]:
        print(f"  {plat}:")
        for tc in ["gcc", "clang"]:
            for osal in ["freertos", "rtthread", "null"]:
                print(f"    python tools/p2s.py -p {plat} -t {tc} -o {osal}")
        for tc in ["keil5", "keil6"]:
            for osal in ["freertos", "rtthread", "null"]:
                print(f"    python tools/p2s.py -p {plat} -t {tc} -o {osal}")
        print()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="mini_tree 一键构建脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  python tools/p2s.py -p arm_cm3 -t gcc -o freertos\n"
            "  python tools/p2s.py -p arm_cm4f -t keil5 -o rtthread\n"
            "  python tools/p2s.py --menuconfig\n"
            "  python tools/p2s.py --clean\n"
        ),
    )
    parser.add_argument("-p", "--platform", choices=list(MAKEFILE_PLATFORMS), help="目标平台")
    parser.add_argument("-t", "--toolchain", choices=list(TOOLCHAINS), help="编译器工具链")
    parser.add_argument("-o", "--osal", choices=list(OSALS), help="OSAL 后端")
    parser.add_argument("--heap", type=int, default=4, help="FreeRTOS heap allocator (1-5)")
    parser.add_argument("-m", "--menuconfig", action="store_true", help="运行 menuconfig 配置")
    parser.add_argument("-c", "--clean", action="store_true", help="清理构建产物")
    parser.add_argument("-l", "--list", action="store_true", help="列出所有可用构建组合")

    args = parser.parse_args()

    if args.list:
        show_combinations()
        return 0

    if args.menuconfig:
        return run_menuconfig()

    if args.clean:
        print("[p2s] 清理 ...")
        project_root: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        subprocess.run(["mingw32-make", "clean"], cwd=project_root)
        return 0

    if not all([args.platform, args.toolchain, args.osal]):
        parser.print_help()
        print("\n使用 -l 查看所有可用组合")
        return 1

    if args.toolchain in ("keil5", "keil6") and args.platform not in ("arm_cm3", "arm_cm4f", "arm_cm7"):
        print(f"[p2s] 错误: {args.toolchain} 仅支持 ARM 平台")
        return 1

    return run_make(args.platform, args.toolchain, OSALS[args.osal], args.heap)


if __name__ == "__main__":
    sys.exit(main())
