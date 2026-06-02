#!/usr/bin/env python3
"""mini_tree Kconfig 图形化配置工具 — 依赖 kconfiglib。

用法:
  python tools/menuconfig.py
"""

import sys
import os

KCONFIG_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KCONFIG_PATH = os.path.join(KCONFIG_DIR, "Kconfig")


def main():
    try:
        from kconfiglib import Kconfig, standard_kconfig
    except ImportError:
        print("[menuconfig] 错误: 请先安装 kconfiglib: pip install kconfiglib")
        return 1

    if not os.path.exists(KCONFIG_PATH):
        print(f"[menuconfig] 错误: 未找到 Kconfig 文件: {KCONFIG_PATH}")
        return 1

    os.chdir(KCONFIG_DIR)

    kconf = Kconfig(filename=KCONFIG_PATH, warn=False)
    from menuconfig import menuconfig
    menuconfig(kconf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
