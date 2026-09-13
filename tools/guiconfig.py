#!/usr/bin/env python3
"""mini_tree Kconfig 图形化配置工具 (Tk GUI 版) — 依赖 kconfiglib + tkinter。

与 menuconfig.py 的区别
-----------------------
- `menuconfig.py` 是**文本 TUI**，依赖标准库 `curses`；Windows 上需额外安装
用法:
  py -3 tools/guiconfig.py

保存路径同样由 KCONFIG_CONFIG 决定，默认写到仓库根的 .config。
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
        print("[guiconfig] 错误: 未找到 kconfiglib (tools/_vendor 缺失?)")
        return 1

    if not KCONFIG_PATH.exists():
        print(f"[guiconfig] 错误: 未找到 Kconfig 文件: {KCONFIG_PATH}")
        return 1

    # 与 menuconfig.py 一致: kconfiglib 读 KCONFIG_CONFIG 决定 .config 路径
    os.environ.setdefault("KCONFIG_CONFIG", str(KCONFIG_DIR / ".config"))

    kconf: Kconfig = Kconfig(filename=str(KCONFIG_PATH), warn=False)
    # 把 tools/ 移出 sys.path, 防止本文件遮蔽 _vendor/guiconfig.py
    sys.path = [
        p for p in sys.path
        if Path(p or Path.cwd()).resolve() != KCONFIG_TOOLS_DIR
    ]
    try:
        from guiconfig import menuconfig  # pyright: ignore[reportMissingImports]
    except ImportError as exc:
        print(f"[guiconfig] 错误: 无法加载 Tk 配置器: {exc}")
        print("[guiconfig] 提示: 需要 tkinter，可用 `py -3 -m tkinter` 自检。")
        return 1
    menuconfig(kconf)
    return 0


if __name__ == "__main__":
    sys.exit(main())
