#!/usr/bin/env python3
"""
dtc-lite.py — MCU 编译期 DeviceTree 编译器 (PLY 词法分析版)

编译期 DTS → C 代码生成器。
词法分析由 PLY (Python Lex-Yacc) 驱动，语法分析为递归下降，语义合并与 C 生成逻辑不变。

用法与 CMake 集成保持不变:
  python dtc-lite.py board/dts/esp32-s3-devkitc-1.dts <output_dir> [driver_dirs...]
"""

from __future__ import annotations

import os
import sys

_TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
if _TOOLS_DIR not in sys.path:
    sys.path.insert(0, _TOOLS_DIR)

from dtc_lite.main import main  # noqa: E402

if __name__ == '__main__':
    main()
