#!/usr/bin/env python3
"""mini_tree Kconfig 图形化配置工具 — 依赖 kconfiglib。

用法:
  python tools/menuconfig.py
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

import _vendor_loader  # tools/ 下的共享加载器 (sys.path[0] 指向 tools/)


KCONFIG_DIR: Path = Path(__file__).resolve().parent.parent  # mini_tree 根
KCONFIG_PATH: Path = KCONFIG_DIR / "Kconfig.non_esp"
KCONFIG_TOOLS_DIR: Path = Path(__file__).resolve().parent  # tools/


def main() -> int:
    _vendor_loader.prepend_kconfig_vendor()

    try:
        from kconfiglib import Kconfig
    except ImportError:
        print("[menuconfig] 错误: 请先安装 kconfiglib: pip install kconfiglib")
        return 1

    if not KCONFIG_PATH.exists():
        print(f"[menuconfig] 错误: 未找到 Kconfig 文件: {KCONFIG_PATH}")
        return 1

    # kconfiglib 的 menuconfig 通过 KCONFIG_CONFIG 环境变量决定 .config 读写路径
    # (rsource 已让 source 解析脱离 CWD) → 无需 chdir, 任意目录启动都指向项目 .config
    os.environ.setdefault("KCONFIG_CONFIG", str(KCONFIG_DIR / ".config"))

    kconf: Kconfig = Kconfig(filename=str(KCONFIG_PATH), warn=False)
    # 将本脚本所在目录移出 sys.path, 防止 tools/menuconfig.py 遮蔽
    # kconfiglib 自带的 menuconfig 顶层模块 (python3 tools/menuconfig.py 时
    # sys.path[0] 指向 tools/, 同名模块会先被找到).
    sys.path = [
        p for p in sys.path
        if Path(p or Path.cwd()).resolve() != KCONFIG_TOOLS_DIR
    ]
    from menuconfig import menuconfig  # pyright: ignore[reportMissingImports]
    menuconfig(kconf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
