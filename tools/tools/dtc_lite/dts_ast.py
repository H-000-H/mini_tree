"""DTS 抽象语法树节点."""

from __future__ import annotations

from typing import List, Optional


class DtsProperty:
    """DTS 属性 (key = value)."""

    __slots__ = ('name', 'strings', 'ints', 'phandles', 'line')

    def __init__(self, name: str, line: int = 0) -> None:
        self.name: str = name
        self.strings: List[str] = []
        self.ints: List[int] = []
        self.phandles: List[str] = []
        self.line: int = line

    def is_empty(self) -> bool:
        return not self.strings and not self.ints and not self.phandles

    def __repr__(self) -> str:
        parts: List[str] = []
        if self.strings:
            parts.append(f'"{self.strings[0]}"')
        if self.ints:
            parts.append(f'<{" ".join(str(i) for i in self.ints)}>')
        if self.phandles:
            parts.append(f'<&{" &".join(self.phandles)}>')
        return f"Prop({self.name} = {' '.join(parts)})"


class DtsNode:
    """DTS 节点."""

    __slots__ = ('name', 'label', 'parent', 'children', 'props', 'line')

    def __init__(
        self,
        name: str,
        label: Optional[str] = None,
        parent: Optional['DtsNode'] = None,
        line: int = 0,
    ) -> None:
        self.name: str = name
        self.label: Optional[str] = label
        self.parent: Optional[DtsNode] = parent
        self.children: List[DtsNode] = []
        self.props: List[DtsProperty] = []
        self.line: int = line

    @property
    def path(self) -> str:
        parts: List[str] = []
        node: Optional[DtsNode] = self
        while node:
            if node.name:
                parts.insert(0, node.name)
            node = node.parent
        return '/' + '/'.join(parts)

    def find_child(self, name: str) -> Optional['DtsNode']:
        for child in self.children:
            if child.name == name:
                return child
        return None

    def find_node_by_path(self, path: str) -> Optional['DtsNode']:
        if path.startswith('/'):
            parts: List[str] = [p for p in path.split('/') if p]
            node: Optional[DtsNode] = self
            while node and node.parent:
                node = node.parent
            for part in parts:
                if node is None:
                    return None
                node = node.find_child(part)
            return node

        parts = path.split('/')
        node = self
        for part in parts:
            if node is None:
                return None
            node = node.find_child(part)
        return node

    def get_prop(self, name: str) -> Optional[DtsProperty]:
        for prop in self.props:
            if prop.name == name:
                return prop
        return None

    def collect_all_devices(self) -> List['DtsNode']:
        devices: List[DtsNode] = []
        if any(p.name == 'compatible' for p in self.props):
            devices.append(self)
        for child in self.children:
            devices.extend(child.collect_all_devices())
        return devices

    def __repr__(self) -> str:
        label = f"{self.label}: " if self.label else ""
        return f"Node({label}{self.name})"
