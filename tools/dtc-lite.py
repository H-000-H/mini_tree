#!/usr/bin/env python3
"""
dtc-lite.py — MCU 编译期 DeviceTree 编译器 (Lark + Transformer 解析)

编译期 DTS → C 代码生成器。
文法由 Lark (Earley 算法) 定义, Transformer 把 parse tree 转 AST, 语义合并与 C 生成逻辑不变.

用法与 CMake 集成保持不变:
  python dtc-lite.py board/dts/esp32-s3-devkitc-1.dts <output_dir> [driver_dirs...]
"""

from __future__ import annotations

import sys
from pathlib import Path

_TOOLS_DIR = Path(__file__).resolve().parent
if str(_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(_TOOLS_DIR))

from dtc_lite.main import main  # noqa: E402

if __name__ == '__main__':
    main()
