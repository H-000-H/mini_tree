"""把 tools/_vendor 里的官方 kconfiglib 前置到 sys.path。

mini_tree 日常是纯 CMake 构建, 不依赖系统级 kconfig 包。启动器与构建脚本在
import kconfiglib 之前调用 prepend_kconfig_vendor(), 保证仓库内置的官方
kconfiglib (kconfiglib.py / menuconfig.py / guiconfig.py, 见 _vendor/README.md)
永远优先于机器上任何已装的包 (包括 ESP-IDF 的 esp_idf_kconfig)。
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Optional


def prepend_kconfig_vendor() -> Optional[str]:
    """将 tools/_vendor 插到 sys.path 最前并返回其路径; 目录不存在返回 None。"""
    vendor = Path(__file__).resolve().parent / "_vendor"
    if vendor.is_dir():
        sys.path.insert(0, str(vendor))
        return str(vendor)
    return None
