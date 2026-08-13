"""dtc-lite — MCU 编译期 DeviceTree 编译器 (Lark + Transformer 解析)."""

# 显式 re-export (X as X): pyright 对 from-import 默认按"导入后改名"处理,
# 同名别名才被识别为公共 re-export; 避免分析器在循环导入时把符号绑定成模块。
from .dts_ast import DtsNode as DtsNode
from .dts_ast import DtsProperty as DtsProperty
from .compiler import DTSCompiler as DTSCompiler
from .generator import CGenerator as CGenerator
from .parser import parse_dts as parse_dts

__all__ = [
    'DtsNode',
    'DtsProperty',
    'DTSCompiler',
    'CGenerator',
    'parse_dts',
]
