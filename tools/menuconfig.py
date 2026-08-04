#!/usr/bin/env python3
"""mini_tree Kconfig 图形化配置工具 — 依赖 kconfiglib。

用法:
  python tools/menuconfig.py
"""

from __future__ import annotations

import os
import sys
from typing import Optional

import _vendor_loader  # tools/ 下的共享加载器 (sys.path[0] 指向 tools/)


KCONFIG_DIR: str = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KCONFIG_PATH: str = os.path.join(KCONFIG_DIR, "Kconfig")
KCONFIG_TOOLS_DIR: str = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    _vendor_loader.prepend_kconfig_vendor()

    try:
        from kconfiglib import Kconfig
    except ImportError:
        print("[menuconfig] 错误: 请先安装 kconfiglib: pip install kconfiglib")
        return 1

    if not os.path.exists(KCONFIG_PATH):
        print(f"[menuconfig] 错误: 未找到 Kconfig 文件: {KCONFIG_PATH}")
        return 1

    os.chdir(KCONFIG_DIR)

    kconf: Kconfig = Kconfig(filename=KCONFIG_PATH, warn=False)
    # 将本脚本所在目录移出 sys.path, 防止 tools/menuconfig.py 遮蔽
    # kconfiglib 自带的 menuconfig 顶层模块 (python3 tools/menuconfig.py 时
    # sys.path[0] 指向 tools/, 同名模块会先被找到).
    sys.path = [p for p in sys.path if os.path.abspath(p or os.getcwd()) != KCONFIG_TOOLS_DIR]
    from menuconfig import menuconfig  # type: ignore[import-untyped]
    menuconfig(kconf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
