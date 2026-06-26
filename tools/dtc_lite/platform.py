"""设备树平台节点判定 — 无需在 compiler 中手工维护 compatible 白名单."""

from __future__ import annotations

from .ast import DtsNode

# 空属性标记：节点仅为 SoC/总线描述，不绑定 VFS 驱动
_PLATFORM_FLAG_PROPS = frozenset({
    'gpio-controller',
    'interrupt-controller',
    'mini-tree,platform',
})

# #*-cells 类属性：Linux 标准控制器描述
_PLATFORM_CELL_PREFIX = '#'
_PLATFORM_CELL_SUFFIX = '-cells'


def is_platform_node(dev: DtsNode) -> bool:
    """Return True if *dev* is infrastructure-only and needs no mini_tree driver."""
    for prop in dev.props:
        name = prop.name
        if name in _PLATFORM_FLAG_PROPS:
            return True
        if name.startswith(_PLATFORM_CELL_PREFIX) and name.endswith(_PLATFORM_CELL_SUFFIX):
            return True

    device_type = dev.get_prop('device_type')
    if device_type and device_type.strings and device_type.strings[0] == 'cpu':
        return True

    compat = dev.get_prop('compatible')
    if compat and compat.strings and compat.strings[0] == 'simple-bus':
        return True

    return False
