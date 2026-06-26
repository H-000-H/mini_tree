"""dtc-lite — MCU 编译期 DeviceTree 编译器 (PLY 词法/语法分析)."""

from .ast import DtsNode, DtsProperty
from .compiler import DTSCompiler
from .generator import CGenerator
from .parser import parse_dts

__all__ = [
    'DtsNode',
    'DtsProperty',
    'DTSCompiler',
    'CGenerator',
    'parse_dts',
]
