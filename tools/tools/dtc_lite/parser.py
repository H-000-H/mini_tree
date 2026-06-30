"""Lark + Transformer 解析 — 构建 DtsNode AST.

替代原 PLY 递归下降实现:
  * 文法定义在 ``grammar.py`` (Lark Earley 算法)
  * ``DtsTransformer`` 把 parse tree 转成中间 op 对象
  * ``parse_dts`` 主函数按顺序应用 op 到 root, 处理 overlay 合并 / delete 语义

对外接口 ``parse_dts(text, filename) -> DtsNode`` 与原 PLY 版完全一致,
``compiler.py`` 无需改动.
"""

from __future__ import annotations

import re
from functools import lru_cache
from typing import List, Optional

from lark import Lark, Token, Transformer

from .dts_ast import DtsNode, DtsProperty
from .grammar import DTS_GRAMMAR


# 在送入 Lark 前剥离 C 风格注释, 避免 '/' (SLASH) 与 '//' '/*' 在 Earley
# dynamic lexer 下产生歧义 (//SPDX ... 的首个 '/' 会被当成根节点 SLASH).
# 同时保护字符串字面量内的 // (如 URL "http://..."), 不被误删.
_COMMENT_RE = re.compile(
    r'"(?:[^"\\]|\\.)*"'        # 字符串字面量 (整体保留)
    r'|(//[^\n]*)'              # 行注释 (group 1)
    r'|(/\*[\s\S]*?\*/)',       # 块注释 (group 2)
    re.MULTILINE,
)


def _strip_comments(text: str) -> str:
    def _sub(m: re.Match) -> str:
        if m.group(1) is not None or m.group(2) is not None:
            return ''
        return m.group(0)
    return _COMMENT_RE.sub(_sub, text)


# ============================================================================
# 中间数据结构 (Transformer 内部传递)
# ============================================================================

class _Addr:
    """节点地址 token (name@addr 中的 addr)."""
    __slots__ = ('value',)
    def __init__(self, value: str) -> None:
        self.value = value


class _Val:
    """单个属性值 (string / int / phandle)."""
    __slots__ = ('kind', 'value')
    def __init__(self, kind: str, value) -> None:
        self.kind = kind      # 'str' | 'int' | 'phandle'
        self.value = value


class _ListVal:
    """``<...>`` 内的值列表."""
    __slots__ = ('vals',)
    def __init__(self, vals: List[_Val]) -> None:
        self.vals = vals


class _PropValue:
    """属性值聚合 (strings + ints + phandles)."""
    __slots__ = ('strings', 'ints', 'phandles')
    def __init__(self) -> None:
        self.strings: List[str] = []
        self.ints: List[int] = []
        self.phandles: List[str] = []


class _DelTarget:
    """delete-node 的目标."""
    __slots__ = ('kind', 'value')
    def __init__(self, kind: str, value) -> None:
        self.kind = kind    # 'label' | 'name' | 'path'
        self.value = value  # str (label/name) | List[str] (path)


class _DelNodeOp:
    __slots__ = ('target',)
    def __init__(self, target: _DelTarget) -> None:
        self.target = target


class _DelPropOp:
    __slots__ = ('name',)
    def __init__(self, name: str) -> None:
        self.name = name


class _RootOp:
    """顶层 ``/ { }`` 根节点 op."""
    __slots__ = ('node',)
    def __init__(self, node: DtsNode) -> None:
        self.node = node


class _OverlayOp:
    """顶层 ``&label { }`` overlay op."""
    __slots__ = ('label', 'node')
    def __init__(self, label: str, node: DtsNode) -> None:
        self.label = label
        self.node = node


class _NamedRootOp:
    """顶层 ``/name { }`` 路径节点 op (如 ``/chosen { }``)."""
    __slots__ = ('name', 'node')
    def __init__(self, name: str, node: DtsNode) -> None:
        self.name = name
        self.node = node


# ============================================================================
# 工具: C 字符串字面量解码
# ============================================================================

def _decode_string(text: str) -> str:
    """解码 DTS/C 字符串字面量内部的转义 (\\n \\t \\\\ \\\")."""
    out: List[str] = []
    idx = 0
    while idx < len(text):
        ch = text[idx]
        if ch != '\\':
            out.append(ch)
            idx += 1
            continue
        idx += 1
        if idx >= len(text):
            break
        esc = text[idx]
        mapping = {'n': '\n', 't': '\t', '\\': '\\', '"': '"'}
        out.append(mapping.get(esc, esc))
        idx += 1
    return ''.join(out)


def _idents(items) -> List[str]:
    """从 transformer items 中提取所有 IDENT token 的字符串值."""
    return [str(it) for it in items if isinstance(it, Token) and it.type == 'IDENT']


# ============================================================================
# Transformer
# ============================================================================

class DtsTransformer(Transformer):
    """Lark parse tree → op 列表 (供 ``parse_dts`` 主函数消费)."""

    def __init__(self, filename: str = '<dts>') -> None:
        super().__init__()
        self.filename = filename

    # ---- 顶层 ----
    def start(self, items):
        return list(items)

    def top_item(self, items):
        return items[0] if items else None

    def header(self, items):
        return None  # /dts-v1/; 忽略

    def SEMI(self, tok):
        return None  # 顶层孤立 SEMI

    # ---- 根节点 ----
    def _build_node_from_items(self, items) -> DtsNode:
        node = DtsNode('', line=0)
        for it in items:
            if isinstance(it, Token) or it is None:
                continue
            if isinstance(it, DtsProperty):
                node.props.append(it)
            elif isinstance(it, DtsNode):
                it.parent = node
                node.children.append(it)
            elif isinstance(it, _DelNodeOp):
                _apply_delete_node_local(node, it.target)
            elif isinstance(it, _DelPropOp):
                node.props = [p for p in node.props if p.name != it.name]
        return node

    def plain_root(self, items):
        return _RootOp(self._build_node_from_items(items))

    def named_root(self, items):
        idents_list = _idents(items)
        name = idents_list[0] if idents_list else ''
        node = self._build_node_from_items(items)
        node.name = name
        return _NamedRootOp(name, node)

    def overlay(self, items):
        label: Optional[str] = None
        node = DtsNode('', line=0)
        for it in items:
            if isinstance(it, Token) and it.type == 'IDENT' and label is None:
                label = str(it)
            elif isinstance(it, Token) or it is None:
                continue
            elif isinstance(it, DtsProperty):
                node.props.append(it)
            elif isinstance(it, DtsNode):
                it.parent = node
                node.children.append(it)
            elif isinstance(it, _DelNodeOp):
                _apply_delete_node_local(node, it.target)
            elif isinstance(it, _DelPropOp):
                node.props = [p for p in node.props if p.name != it.name]
        node.name = f'&{label}'
        return _OverlayOp(label or '', node)

    # ---- 子节点 ----
    def labeled_child(self, items):
        idents_list = _idents(items)
        label = idents_list[0] if len(idents_list) >= 1 else None
        name = idents_list[1] if len(idents_list) >= 2 else ''
        addr = None
        for it in items:
            if isinstance(it, _Addr):
                addr = it.value
                break
        if addr is not None:
            name = f'{name}@{addr}'
        node = DtsNode(name, label=label, line=0)
        for it in items:
            if isinstance(it, (Token, _Addr)) or it is None:
                continue
            if isinstance(it, DtsProperty):
                node.props.append(it)
            elif isinstance(it, DtsNode):
                it.parent = node
                node.children.append(it)
            elif isinstance(it, _DelNodeOp):
                _apply_delete_node_local(node, it.target)
            elif isinstance(it, _DelPropOp):
                node.props = [p for p in node.props if p.name != it.name]
        return node

    def simple_child(self, items):
        idents_list = _idents(items)
        name = idents_list[0] if idents_list else ''
        addr = None
        for it in items:
            if isinstance(it, _Addr):
                addr = it.value
                break
        if addr is not None:
            name = f'{name}@{addr}'
        node = DtsNode(name, line=0)
        for it in items:
            if isinstance(it, (Token, _Addr)) or it is None:
                continue
            if isinstance(it, DtsProperty):
                node.props.append(it)
            elif isinstance(it, DtsNode):
                it.parent = node
                node.children.append(it)
            elif isinstance(it, _DelNodeOp):
                _apply_delete_node_local(node, it.target)
            elif isinstance(it, _DelPropOp):
                node.props = [p for p in node.props if p.name != it.name]
        return node

    # ---- 属性 ----
    def hash_prop(self, items):
        idents_list = _idents(items)
        name = f'#{idents_list[0]}' if idents_list else '#'
        prop = DtsProperty(name, line=0)
        pv = next((it for it in items if isinstance(it, _PropValue)), None)
        if pv:
            prop.strings, prop.ints, prop.phandles = pv.strings, pv.ints, pv.phandles
        else:
            prop.strings = ['true']
        return prop

    def ident_prop(self, items):
        idents_list = _idents(items)
        name = idents_list[0] if idents_list else ''
        prop = DtsProperty(name, line=0)
        pv = next((it for it in items if isinstance(it, _PropValue)), None)
        if pv:
            prop.strings, prop.ints, prop.phandles = pv.strings, pv.ints, pv.phandles
        else:
            prop.strings = ['true']
        return prop

    def prop_value(self, items):
        pv = _PropValue()
        for it in items:
            if isinstance(it, Token):  # EQ / COMMA
                continue
            if isinstance(it, _Val):
                if it.kind == 'str':
                    pv.strings.append(it.value)
                elif it.kind == 'int':
                    pv.ints.append(it.value)
                elif it.kind == 'phandle':
                    pv.phandles.append(it.value)
            elif isinstance(it, _ListVal):
                for v in it.vals:
                    if v.kind == 'int':
                        pv.ints.append(v.value)
                    elif v.kind == 'phandle':
                        pv.phandles.append(v.value)
                    elif v.kind == 'str':
                        pv.strings.append(v.value)
        return pv

    # ---- 值 ----
    def str_val(self, items):
        raw = str(items[0])
        return _Val('str', _decode_string(raw[1:-1]))

    def int_val(self, items):
        return _Val('int', int(str(items[0]), 0))

    def phandle_val(self, items):
        idents_list = _idents(items)
        return _Val('phandle', idents_list[0] if idents_list else '')

    def list_val(self, items):
        vals = [it for it in items if isinstance(it, _Val)]
        return _ListVal(vals)

    def ai_int(self, items):
        return _Val('int', int(str(items[0]), 0))

    def ai_phandle(self, items):
        idents_list = _idents(items)
        return _Val('phandle', idents_list[0] if idents_list else '')

    def ai_ident(self, items):
        # <...> 内的裸 IDENT (宏展开后剩余的符号) — 跳过
        return None

    # ---- 地址 ----
    def addr_int(self, items):
        return _Addr(str(items[0]))

    def addr_ident(self, items):
        return _Addr(str(items[0]))

    # ---- delete 语句 ----
    def delete_node_stmt(self, items):
        for it in items:
            if isinstance(it, _DelTarget):
                return _DelNodeOp(it)
        return None

    def delete_prop_stmt(self, items):
        idents_list = _idents(items)
        return _DelPropOp(idents_list[0] if idents_list else '')

    def del_tgt_label(self, items):
        idents_list = _idents(items)
        return _DelTarget('label', idents_list[0] if idents_list else '')

    def del_tgt_name(self, items):
        idents_list = _idents(items)
        name = idents_list[0] if idents_list else ''
        addr = None
        for it in items:
            if isinstance(it, _Addr):
                addr = it.value
                break
        if addr is not None:
            name = f'{name}@{addr}'
        return _DelTarget('name', name)

    def del_tgt_path(self, items):
        return _DelTarget('path', _idents(items))


# ============================================================================
# delete 语义 (作用于当前层, 不递归整个树)
# ============================================================================

def _apply_delete_node_local(parent: DtsNode, target: _DelTarget) -> None:
    """把 delete-node 应用到 parent (只删 parent.children 一层)."""
    if target.kind == 'label':
        parent.children = [c for c in parent.children if c.label != target.value]
    elif target.kind == 'name':
        parent.children = [c for c in parent.children if c.name != target.value]
    elif target.kind == 'path':
        parts: List[str] = target.value
        node: Optional[DtsNode] = parent
        for i, part in enumerate(parts):
            if node is None:
                break
            if i == len(parts) - 1:
                node.children = [c for c in node.children if c.name != part]
            else:
                node = node.find_child(part)


def _apply_delete_node_recursive(root: DtsNode, target: _DelTarget) -> None:
    """顶层 delete-node (无父节点上下文): label/name 递归删, path 按路径删."""
    if target.kind == 'label':
        _delete_label_recursive(root, target.value)
    elif target.kind == 'name':
        _delete_name_recursive(root, target.value)
    elif target.kind == 'path':
        _apply_delete_node_local(root, target)


def _delete_label_recursive(node: DtsNode, label: str) -> None:
    node.children = [c for c in node.children if c.label != label]
    for c in node.children:
        _delete_label_recursive(c, label)


def _delete_name_recursive(node: DtsNode, name: str) -> None:
    node.children = [c for c in node.children if c.name != name]
    for c in node.children:
        _delete_name_recursive(c, name)


def _merge_node(target: DtsNode, src: DtsNode) -> None:
    """把 src 的 props/children 合并到 target (覆盖同名 prop, 递归合并同名 child)."""
    for prop in src.props:
        existing = target.get_prop(prop.name)
        if existing:
            existing.strings = prop.strings
            existing.ints = prop.ints
            existing.phandles = prop.phandles
        else:
            target.props.append(prop)
    for child in src.children:
        existing_child = target.find_child(child.name)
        if existing_child:
            _merge_node(existing_child, child)
        else:
            child.parent = target
            target.children.append(child)


# ============================================================================
# Parser 缓存 (Lark 实例化较重, 全局复用)
# ============================================================================

@lru_cache(maxsize=1)
def _get_parser() -> Lark:
    return Lark(DTS_GRAMMAR, parser='earley', start='start',
               propagate_positions=False, maybe_placeholders=False)


# ============================================================================
# 对外入口
# ============================================================================

def parse_dts(text: str, filename: str = '<dts>') -> DtsNode:
    """解析 DTS 文本 → DtsNode AST 根节点.

    与原 PLY 版行为一致:
      * 顶层 ``/ { }`` 内容合并到 root
      * 顶层 ``&label { }`` 作为 root 的子节点 (name=``&label``), 由 compiler 后处理
      * 顶层 ``/delete-node/`` / ``/delete-property/`` 作用于 root
      * body 内的 delete 作用于所在节点
    """
    parser = _get_parser()
    tree = parser.parse(_strip_comments(text))
    items = DtsTransformer(filename).transform(tree)

    root = DtsNode('', line=0)
    has_root = False

    for item in items:
        if item is None:
            continue
        if isinstance(item, _RootOp):
            has_root = True
            _merge_node(root, item.node)
        elif isinstance(item, _NamedRootOp):
            has_root = True
            existing = root.find_child(item.name)
            if existing:
                _merge_node(existing, item.node)
            else:
                item.node.parent = root
                root.children.append(item.node)
        elif isinstance(item, _OverlayOp):
            item.node.parent = root
            root.children.append(item.node)
        elif isinstance(item, _DelNodeOp):
            _apply_delete_node_recursive(root, item.target)
        elif isinstance(item, _DelPropOp):
            _delete_prop_recursive(root, item.name)

    if not has_root:
        raise SyntaxError(f'{filename}: missing root node "/"')
    return root


def _delete_prop_recursive(node: DtsNode, name: str) -> None:
    node.props = [p for p in node.props if p.name != name]
    for c in node.children:
        _delete_prop_recursive(c, name)
