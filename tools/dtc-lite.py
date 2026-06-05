#!/usr/bin/env python3
"""
dtc-lite.py — MCU 编译期 DeviceTree 编译器

编译期 DTS → C 代码生成器。
解析 MCU Lite DTS 格式，生成静态 C 结构体 + probe 表。

用法:
    python dtc-lite.py board.dts output_dir [driver_dirs ...]

DTS 预处理:
    支持 #include "file" / #include <file> 展开, #define 宏替换,
    以及 #ifndef/#endif include guard。
    可配合 dt-bindings/ 头文件使用常量宏 (如 GPIO_ACTIVE_HIGH)。

Output (in output_dir/):
    board_nodes.h       — DEV_ID_xxx 枚举 + chosen/alias 宏
    board_devtable.h    — 设备表访问 API
    board_devtable.c    — 静态 device_t 数组
    board_probe.c       — probe 函数指针表 + 拓扑排序顺序
    board_handles.h     — chosen/alias 注入句柄
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional, Set, Tuple


# =========================================================================
#  Tokenizer（分词器）
# =========================================================================

TOKEN_EOF: int = 0
TOKEN_LBRACE: int = 1   # {
TOKEN_RBRACE: int = 2   # }
TOKEN_SEMI: int = 3     # ;
TOKEN_EQ: int = 4       # =
TOKEN_LANGLE: int = 5   # <
TOKEN_RANGLE: int = 6   # >
TOKEN_SLASH: int = 7    # /
TOKEN_AMPERS: int = 8   # &
TOKEN_COLON: int = 9    # :
TOKEN_COMMA: int = 10   # ,
TOKEN_STRING: int = 11  # "string"
TOKEN_INT: int = 12     # 123
TOKEN_IDENT: int = 13   # identifier-with-hy-phens
TOKEN_DTSV1: int = 14   # /dts-v1/
TOKEN_POUND: int = 15   # # (for #include, etc.)
TOKEN_AT: int = 16      # @ (unit-address separator)
TOKEN_DELETE_NODE: int = 17  # /delete-node/
TOKEN_DELETE_PROP: int = 18  # /delete-property/

token_names: Dict[int, str] = {

    0: "EOF", 1: "{", 2: "}", 3: ";", 4: "=",
    5: "<", 6: ">", 7: "/", 8: "&", 9: ":",
    10: ",", 11: "STRING", 12: "INT", 13: "IDENT",
    14: "/dts-v1/", 15: "#", 16: "@",
    17: "/delete-node/", 18: "/delete-property/",
}


class Token:
    __slots__ = ('type', 'value', 'line', 'col')
    def __init__(self, type: int, value: Any = None, line: int = 0, col: int = 0) -> None:
        self.type: int = type
        self.value: Any = value
        self.line: int = line
        self.col: int = col

    def __repr__(self) -> str:
        if self.value is not None:
            return f"Token({token_names.get(self.type, self.type)}, '{self.value}')"
        return f"Token({token_names.get(self.type, self.type)})"


class Tokenizer:
    """DTS 分词器 — 有限状态机。"""

    def __init__(self, text: str, filename: str = "<dts>") -> None:
        self.text: str = text
        self.filename: str = filename
        self.pos: int = 0
        self.line: int = 1
        self.col: int = 1

    def peek(self) -> str:
        return self.text[self.pos] if self.pos < len(self.text) else '\0'

    def advance(self) -> str:
        ch: str = self.text[self.pos] if self.pos < len(self.text) else '\0'
        self.pos += 1
        if ch == '\n':
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def skip_ws(self) -> None:
        while self.pos < len(self.text):
            ch: str = self.peek()
            if ch in ' \t\n\r':
                self.advance()
            else:
                break

    def skip_line_comment(self) -> None:
        while self.pos < len(self.text):
            ch: str = self.advance()
            if ch == '\n':
                return

    def skip_block_comment(self) -> None:
        while self.pos + 1 < len(self.text):
            if self.peek() == '*' and self.text[self.pos + 1] == '/':
                self.advance()
                self.advance()
                return
            self.advance()
        raise SyntaxError(f"{self.filename}:{self.line}: unterminated block comment")

    def error(self, msg: str) -> None:
        raise SyntaxError(f"{self.filename}:{self.line}:{self.col}: {msg}")

    def tokenize(self) -> List[Token]:
        tokens: List[Token] = []
        while self.pos < len(self.text):
            ch: str = self.peek()
            line: int = self.line
            col: int = self.col

            if ch in ' \t\n\r':
                self.skip_ws()
                continue

            if ch == '/':
                if self.pos + 1 < len(self.text):
                    if self.text[self.pos + 1] == '/':
                        self.advance()
                        self.skip_line_comment()
                        continue
                    elif self.text[self.pos + 1] == '*':
                        self.advance()
                        self.skip_block_comment()
                        continue
                if self.pos + 7 < len(self.text) and self.text[self.pos:self.pos+8] == '/dts-v1/':
                    for _ in range(8):
                        self.advance()
                    tokens.append(Token(TOKEN_DTSV1, None, line, col))
                    continue
                if (self.pos + 12 < len(self.text) and
                    self.text[self.pos:self.pos+13] == '/delete-node/'):
                    for _ in range(13):
                        self.advance()
                    tokens.append(Token(TOKEN_DELETE_NODE, None, line, col))
                    continue
                if (self.pos + 16 < len(self.text) and
                    self.text[self.pos:self.pos+17] == '/delete-property/'):
                    for _ in range(17):
                        self.advance()
                    tokens.append(Token(TOKEN_DELETE_PROP, None, line, col))
                    continue
                tokens.append(Token(TOKEN_SLASH, None, line, col))
                self.advance()
                continue

            if ch == '"':
                self.advance()
                s: List[str] = []
                while self.pos < len(self.text):
                    c: str = self.advance()
                    if c == '"':
                        break
                    if c == '\\':
                        c2: str = self.advance()
                        if c2 == 'n':
                            s.append('\n')
                        elif c2 == 't':
                            s.append('\t')
                        elif c2 == '\\':
                            s.append('\\')
                        elif c2 == '"':
                            s.append('"')
                        else:
                            s.append(c2)
                    else:
                        s.append(c)
                else:
                    self.error("unterminated string")
                tokens.append(Token(TOKEN_STRING, ''.join(s), line, col))
                continue

            if ch == '{':
                tokens.append(Token(TOKEN_LBRACE, None, line, col))
                self.advance()
                continue
            if ch == '}':
                tokens.append(Token(TOKEN_RBRACE, None, line, col))
                self.advance()
                continue
            if ch == ';':
                tokens.append(Token(TOKEN_SEMI, None, line, col))
                self.advance()
                continue
            if ch == '=':
                tokens.append(Token(TOKEN_EQ, None, line, col))
                self.advance()
                continue
            if ch == '<':
                tokens.append(Token(TOKEN_LANGLE, None, line, col))
                self.advance()
                continue
            if ch == '>':
                tokens.append(Token(TOKEN_RANGLE, None, line, col))
                self.advance()
                continue
            if ch == '&':
                tokens.append(Token(TOKEN_AMPERS, None, line, col))
                self.advance()
                continue
            if ch == ':':
                tokens.append(Token(TOKEN_COLON, None, line, col))
                self.advance()
                continue
            if ch == ',':
                tokens.append(Token(TOKEN_COMMA, None, line, col))
                self.advance()
                continue
            if ch == '#':
                tokens.append(Token(TOKEN_POUND, None, line, col))
                self.advance()
                continue
            if ch == '@':
                tokens.append(Token(TOKEN_AT, None, line, col))
                self.advance()
                continue

            if ch.isdigit() or (ch == '-' and self.pos + 1 < len(self.text)
                                and self.text[self.pos + 1].isdigit()):
                start: int = self.pos
                if ch == '0' and self.pos + 1 < len(self.text) and self.text[self.pos + 1] in ('x', 'X'):
                    self.advance()
                    self.advance()
                    while self.pos < len(self.text) and (self.peek().isdigit() or self.peek() in 'abcdefABCDEF'):
                        self.advance()
                else:
                    if ch == '-':
                        self.advance()
                    while self.pos < len(self.text) and self.peek().isdigit():
                        self.advance()
                val: int = int(self.text[start:self.pos], 0)
                tokens.append(Token(TOKEN_INT, val, line, col))
                continue

            if ch.isalpha() or ch == '_' or ch == '.':
                start = self.pos
                while self.pos < len(self.text):
                    c = self.peek()
                    if c.isalnum() or c in '_-.,/':
                        self.advance()
                    else:
                        break
                ident: str = self.text[start:self.pos]
                if ident == 'dts-v1' and start > 0 and self.text[start-1] == '/':
                    continue
                tokens.append(Token(TOKEN_IDENT, ident, line, col))
                continue

            if ch in '()|!~^':
                self.advance()
                continue

            self.error(f"unexpected character '{ch}'")

        tokens.append(Token(TOKEN_EOF, None, self.line, self.col))
        return tokens


# =========================================================================
#  AST（抽象语法树）
# =========================================================================

class DtsProperty:
    """DTS 属性 (key = value)"""
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
    """DTS 节点"""
    __slots__ = ('name', 'label', 'parent', 'children', 'props', 'line')

    def __init__(self, name: str, label: Optional[str] = None,
                 parent: Optional[DtsNode] = None, line: int = 0) -> None:
        self.name: str = name
        self.label: Optional[str] = label
        self.parent: Optional[DtsNode] = parent
        self.children: List[DtsNode] = []
        self.props: List[DtsProperty] = []
        self.line: int = line

    @property
    def path(self) -> str:
        """从根到当前节点的路径"""
        parts: List[str] = []
        node: Optional[DtsNode] = self
        while node:
            if node.name:
                parts.insert(0, node.name)
            node = node.parent
        return '/' + '/'.join(parts)

    def find_child(self, name: str) -> Optional[DtsNode]:
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find_node_by_path(self, path: str) -> Optional[DtsNode]:
        """按路径查找子节点 (相对路径或绝对路径)"""
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
        else:
            parts = path.split('/')
            node: Optional[DtsNode] = self
            for part in parts:
                if node is None:
                    return None
                node = node.find_child(part)
            return node

    def get_prop(self, name: str) -> Optional[DtsProperty]:
        for p in self.props:
            if p.name == name:
                return p
        return None

    def collect_all_devices(self) -> List[DtsNode]:
        """递归收集所有包含 compatible 属性的节点 (候选设备)"""
        devices: List[DtsNode] = []
        if any(p.name == 'compatible' for p in self.props):
            devices.append(self)
        for c in self.children:
            devices.extend(c.collect_all_devices())
        return devices

    def __repr__(self) -> str:
        lbl: str = f"{self.label}: " if self.label else ""
        return f"Node({lbl}{self.name})"


# =========================================================================
#  Parser（解析器）
# =========================================================================

class DtsParser:
    """DTS 递归下降解析器"""

    def __init__(self, tokens: List[Token]) -> None:
        self.tokens: List[Token] = tokens
        self.pos: int = 0

    def peek(self, offset: int = 0) -> Optional[Token]:
        idx: int = self.pos + offset
        return self.tokens[idx] if idx < len(self.tokens) else None

    def advance(self) -> Token:
        t: Token = self.tokens[self.pos]
        self.pos += 1
        return t

    def expect(self, type: int, msg: Optional[str] = None) -> Token:
        t: Token = self.advance()
        if t.type != type:
            raise SyntaxError(
                f"line {t.line}: expected {token_names.get(type, type)}"
                f" but got {token_names.get(t.type, t.type)}"
                f" ({msg or ''})"
            )
        return t

    def skip_semi(self) -> None:
        """跳过可选的分号 (某些 DTS 风格可省略)"""
        if self.peek() and self.peek().type == TOKEN_SEMI:
            self.advance()

    def parse(self) -> DtsNode:
        """dts := header root nodes (支持多 / 节合并)"""
        self.parse_header()
        root: Optional[DtsNode] = self.parse_node()
        if not root or root.name != '':
            raise SyntaxError("missing root node '/'")

        # 后续 / { ... } 节: Linux DTS 支持同文件拆分为多个根节
        if root:
            self._merge_extra_root_sections(root)
        return root

    def _merge_extra_root_sections(self, root: DtsNode) -> None:
        """合并后续 / { ... } 节到 root"""
        while self.peek() and self.peek().type != TOKEN_EOF:
            t: Token = self.peek()
            if t.type == TOKEN_SLASH:
                self.advance()
                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    extra: DtsNode = self._parse_root_body_only(root_for_delete=root)
                    if extra:
                        for prop in extra.props:
                            existing: Optional[DtsProperty] = root.get_prop(prop.name)
                            if existing:
                                existing.strings = prop.strings
                                existing.ints = prop.ints
                                existing.phandles = prop.phandles
                            else:
                                root.props.append(prop)
                        for child in extra.children:
                            existing_child: Optional[DtsNode] = root.find_child(child.name)
                            if existing_child:
                                self._merge_node_into(existing_child, child)
                            else:
                                child.parent = root
                                root.children.append(child)
                continue
            elif t.type == TOKEN_DTSV1:
                self.advance()
                continue
            else:
                break

    def _parse_root_body_only(self, root_for_delete: Optional[DtsNode] = None) -> Optional[DtsNode]:
        """解析 / { body } 中的 body, 返回临时节点

        若 root_for_delete 不为 None, 其中的 /delete-node/ 和 /delete-property/
        直接作用于 root_for_delete (而非 temp 节点), 确保合并节中的删除指令生效。
        """
        if not self.peek() or self.peek().type != TOKEN_LBRACE:
            return None
        self.advance()
        tmp: DtsNode = DtsNode('_root_extra', line=0)
        while self.peek() and self.peek().type != TOKEN_RBRACE:
            if root_for_delete and self.peek().type in (TOKEN_DELETE_NODE, TOKEN_DELETE_PROP):
                # 顶层删除直接作用于 root, 不经过 tmp
                t = self.advance()
                if t.type == TOKEN_DELETE_NODE:
                    self._delete_node_from(root_for_delete)
                else:
                    self._delete_prop_from(root_for_delete)
                self.skip_semi()
            else:
                self.parse_body_item(tmp)
        if self.peek() and self.peek().type == TOKEN_RBRACE:
            self.advance()
        self.skip_semi()
        return tmp

    def _merge_node_into(self, target: DtsNode, src: DtsNode) -> None:
        """递归合并 src 到 target"""
        for prop in src.props:
            existing: Optional[DtsProperty] = target.get_prop(prop.name)
            if existing:
                existing.strings = prop.strings
                existing.ints = prop.ints
                existing.phandles = prop.phandles
            else:
                target.props.append(prop)
        for child in src.children:
            existing_child: Optional[DtsNode] = target.find_child(child.name)
            if existing_child:
                self._merge_node_into(existing_child, child)
            else:
                child.parent = target
                target.children.append(child)


    def parse_header(self) -> None:
        """header := '/dts-v1/' ';'"""
        if self.peek() and self.peek().type == TOKEN_DTSV1:
            self.advance()
            self.skip_semi()

    def parse_node(self) -> Optional[DtsNode]:
        """node := ['/' | label ':' name] '{' body '}' ';'?"""
        if not self.peek():
            return None

        t: Token = self.peek()
        line: int = t.line
        label: Optional[str] = None
        name: Optional[str] = None

        if t.type == TOKEN_SLASH:
            self.advance()
            name = ''
        elif t.type == TOKEN_IDENT:
            ident: str = self.advance().value
            if self.peek() and self.peek().type == TOKEN_COLON:
                self.advance()
                label = ident
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    name = self.advance().value
                else:
                    name = label
            elif self.peek() and self.peek().type == TOKEN_AT:
                name = ident
            else:
                name = ident
        else:
            return None

        if self.peek() and self.peek().type == TOKEN_AT:
            self.advance()
            addr_tok: Token = self.advance()
            addr_str: str = str(addr_tok.value) if addr_tok.type in (TOKEN_INT, TOKEN_IDENT) else ""
            name = f"{name}@{addr_str}"

        if not self.peek() or self.peek().type != TOKEN_LBRACE:
            return None

        node: DtsNode = DtsNode(name, label=label, parent=None, line=line)
        self.advance()

        while self.peek() and self.peek().type != TOKEN_RBRACE:
            t = self.peek()

            if t.type == TOKEN_POUND:
                self.advance()
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    ident = "#" + self.advance().value
                    if self.peek() and self.peek().type == TOKEN_EQ:
                        self.advance()
                        prop: DtsProperty = DtsProperty(ident, line=t.line)
                        self.parse_prop_value(prop)
                        node.props.append(prop)
                    else:
                        prop = DtsProperty(ident, line=t.line)
                        prop.strings = ["true"]
                        node.props.append(prop)
                continue

            if t.type == TOKEN_IDENT:
                ident = t.value
                self.advance()

                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    child: DtsNode = DtsNode(ident, parent=node, line=t.line)
                    self.advance()
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(child)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                    node.children.append(child)

                elif self.peek() and self.peek().type == TOKEN_COLON:
                    self.advance()
                    label_name: str = ident
                    if self.peek() and self.peek().type == TOKEN_IDENT:
                        child_name: str = self.advance().value
                    else:
                        child_name = label_name

                    if self.peek() and self.peek().type == TOKEN_AT:
                        self.advance()
                        addr_tok = self.advance()
                        addr_str = str(addr_tok.value) if addr_tok.type in (TOKEN_INT, TOKEN_IDENT) else ""
                        child_name = f"{child_name}@{addr_str}"

                    if self.peek() and self.peek().type == TOKEN_LBRACE:
                        child = DtsNode(child_name, label=label_name, parent=node, line=t.line)
                        self.advance()
                        while self.peek() and self.peek().type != TOKEN_RBRACE:
                            self.parse_body_item(child)
                        if self.peek() and self.peek().type == TOKEN_RBRACE:
                            self.advance()
                        self.skip_semi()
                        node.children.append(child)
                    else:
                        prop = DtsProperty(label_name, line=t.line)
                        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                            val_token: Token = self.advance()
                            if val_token.type == TOKEN_STRING:
                                prop.strings.append(val_token.value)
                            elif val_token.type == TOKEN_INT:
                                prop.ints.append(val_token.value)
                            elif val_token.type == TOKEN_AMPERS:
                                ph: Token = self.advance()
                                if ph.type == TOKEN_IDENT:
                                    prop.phandles.append(ph.value)
                            elif val_token.type in (TOKEN_LANGLE, TOKEN_RANGLE):
                                pass
                            elif val_token.type == TOKEN_COMMA:
                                pass
                        if self.peek() and self.peek().type == TOKEN_SEMI:
                            self.advance()
                        node.props.append(prop)

                elif self.peek() and self.peek().type == TOKEN_EQ:
                    self.advance()
                    prop = DtsProperty(ident, line=t.line)
                    self.parse_prop_value(prop)
                    node.props.append(prop)

                elif self.peek() and self.peek().type == TOKEN_SEMI:
                    node.props.append(DtsProperty(ident, line=t.line))
                    self.advance()
                    node.props[-1].strings = ["true"]

                else:
                    prop = DtsProperty(ident, line=t.line)
                    while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                        val_token = self.advance()
                        if val_token.type == TOKEN_STRING:
                            prop.strings.append(val_token.value)
                        elif val_token.type == TOKEN_INT:
                            prop.ints.append(val_token.value)
                        elif val_token.type == TOKEN_AMPERS:
                            ph = self.advance()
                            if ph.type == TOKEN_IDENT:
                                prop.phandles.append(ph.value)
                        elif val_token.type in (TOKEN_LANGLE, TOKEN_RANGLE, TOKEN_COMMA, TOKEN_AT, TOKEN_COLON):
                            pass
                    if self.peek() and self.peek().type == TOKEN_SEMI:
                        self.advance()
                    node.props.append(prop)

            elif t.type == TOKEN_AMPERS:
                self.advance()
                lbl: Token = self.advance()
                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    self.advance()
                    ref_node: DtsNode = DtsNode(f"&{lbl.value}", parent=node, line=t.line)
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(ref_node)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                    node.children.append(ref_node)

            elif t.type == TOKEN_DELETE_NODE:
                self.advance()
                self._delete_node_from(node)
                self.skip_semi()

            elif t.type == TOKEN_DELETE_PROP:
                self.advance()
                self._delete_prop_from(node)
                self.skip_semi()

            elif t.type in (TOKEN_SLASH, TOKEN_DTSV1):
                self.advance()

            else:
                break

        if self.peek() and self.peek().type == TOKEN_RBRACE:
            self.advance()
        self.skip_semi()

        return node

    def parse_body_item(self, parent: DtsNode) -> None:
        """解析 body 中的一项 (prop 或 child node)"""
        if not self.peek():
            return

        t: Token = self.peek()

        if t.type == TOKEN_POUND:
            self.advance()
            if self.peek() and self.peek().type == TOKEN_IDENT:
                ident: str = "#" + self.advance().value
                if self.peek() and self.peek().type == TOKEN_EQ:
                    self.advance()
                    prop: DtsProperty = DtsProperty(ident, line=t.line)
                    self.parse_prop_value(prop)
                    parent.props.append(prop)
                else:
                    prop = DtsProperty(ident, line=t.line)
                    prop.strings = ["true"]
                    parent.props.append(prop)
            return

        if t.type == TOKEN_IDENT:
            ident = t.value
            self.advance()

            if self.peek() and self.peek().type == TOKEN_LBRACE:
                child: DtsNode = DtsNode(ident, parent=parent, line=t.line)
                self.advance()
                while self.peek() and self.peek().type != TOKEN_RBRACE:
                    self.parse_body_item(child)
                if self.peek() and self.peek().type == TOKEN_RBRACE:
                    self.advance()
                self.skip_semi()
                parent.children.append(child)

            elif self.peek() and self.peek().type == TOKEN_COLON:
                self.advance()
                label_name: str = ident
                child_name: str = label_name
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    child_name = self.advance().value
                if self.peek() and self.peek().type == TOKEN_AT:
                    self.advance()
                    addr_tok: Token = self.advance()
                    addr_str: str = str(addr_tok.value) if addr_tok.type in (TOKEN_INT, TOKEN_IDENT) else ""
                    child_name = f"{child_name}@{addr_str}"
                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    child = DtsNode(child_name, label=label_name, parent=parent, line=t.line)
                    self.advance()
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(child)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                    parent.children.append(child)
                else:
                    prop = DtsProperty(label_name, line=t.line)
                    parent.props.append(prop)

            elif self.peek() and self.peek().type == TOKEN_EQ:
                self.advance()
                prop = DtsProperty(ident, line=t.line)
                self.parse_prop_value(prop)
                parent.props.append(prop)

            elif self.peek() and self.peek().type == TOKEN_SEMI:
                prop = DtsProperty(ident, line=t.line)
                prop.strings = ["true"]
                parent.props.append(prop)
                self.advance()

            elif self.peek() and self.peek().type == TOKEN_AT:
                self.advance()
                addr_tok = self.advance()
                addr_str = str(addr_tok.value) if addr_tok.type == TOKEN_INT else str(addr_tok.value)
                child_name = f"{ident}@{addr_str}"
                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    child = DtsNode(child_name, parent=parent, line=t.line)
                    self.advance()
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(child)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                    parent.children.append(child)
                else:
                    prop = DtsProperty(child_name, line=t.line)
                    while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                        vt: Token = self.advance()
                        if vt.type == TOKEN_INT:
                            prop.ints.append(vt.value)
                    if self.peek() and self.peek().type == TOKEN_SEMI:
                        self.advance()
                    parent.props.append(prop)

            else:
                prop = DtsProperty(ident, line=t.line)
                while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                    vt = self.advance()
                    if vt.type == TOKEN_STRING:
                        prop.strings.append(vt.value)
                    elif vt.type == TOKEN_INT:
                        prop.ints.append(vt.value)
                    elif vt.type == TOKEN_AMPERS:
                        ph: Token = self.advance()
                        if ph.type == TOKEN_IDENT:
                            prop.phandles.append(ph.value)
                    elif vt.type in (TOKEN_LANGLE, TOKEN_RANGLE, TOKEN_COMMA, TOKEN_AT, TOKEN_COLON, TOKEN_SLASH):
                        pass
                if self.peek() and self.peek().type == TOKEN_SEMI:
                    self.advance()
                parent.props.append(prop)

        elif t.type == TOKEN_DELETE_NODE:
            self.advance()
            self._delete_node_from(parent)
            self.skip_semi()

        elif t.type == TOKEN_DELETE_PROP:
            self.advance()
            self._delete_prop_from(parent)
            self.skip_semi()

        elif t.type == TOKEN_RBRACE:
            return

        else:
            self.advance()

    def _delete_node_from(self, parent: DtsNode) -> None:
        """处理 /delete-node/ 指令 — 从 parent 中移除子节点"""
        t: Optional[Token] = self.peek()
        if not t:
            return

        if t.type == TOKEN_AMPERS:
            self.advance()
            lbl: Token = self.advance() if self.peek() else None
            if lbl and lbl.type == TOKEN_IDENT:
                parent.children = [c for c in parent.children if c.label != lbl.value]

        elif t.type == TOKEN_IDENT:
            name: str = t.value
            self.advance()
            if self.peek() and self.peek().type == TOKEN_AT:
                self.advance()
                addr_tok: Token = self.advance() if self.peek() else None
                addr_str: str = str(addr_tok.value) if addr_tok and addr_tok.type == TOKEN_INT else ""
                name = f"{name}@{addr_str}"
            parent.children = [c for c in parent.children if c.name != name]

        elif t.type == TOKEN_SLASH:
            parts: List[str] = []
            self.advance()
            while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_EOF):
                if self.peek().type == TOKEN_IDENT:
                    parts.append(self.advance().value)
                elif self.peek().type == TOKEN_SLASH:
                    self.advance()
                elif self.peek().type == TOKEN_AT:
                    self.advance()
                    nxt: Token = self.advance() if self.peek() else None
                    if nxt:
                        parts[-1] = f"{parts[-1]}@{nxt.value}"
                else:
                    break
            if parts:
                target: Optional[DtsNode] = parent
                for i, part in enumerate(parts):
                    if target is None:
                        break
                    if i == len(parts) - 1:
                        target.children = [c for c in target.children if c.name != part]
                    else:
                        target = target.find_child(part)

    def _delete_prop_from(self, parent: DtsNode) -> None:
        """处理 /delete-property/ 指令 — 从 parent 中移除属性"""
        if self.peek() and self.peek().type == TOKEN_IDENT:
            name: str = self.advance().value
            parent.props = [p for p in parent.props if p.name != name]

    def parse_prop_value(self, prop: DtsProperty) -> None:
        """解析属性值 (可能是 strings, <ints>, phandles 的组合)"""
        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
            t: Token = self.peek()
            if t.type == TOKEN_STRING:
                self.advance()
                prop.strings.append(t.value)
            elif t.type == TOKEN_INT:
                self.advance()
                prop.ints.append(t.value)
            elif t.type == TOKEN_LANGLE:
                self.advance()
                while self.peek() and self.peek().type != TOKEN_RANGLE:
                    inner: Token = self.peek()
                    if inner.type == TOKEN_INT:
                        self.advance()
                        prop.ints.append(inner.value)
                    elif inner.type == TOKEN_AMPERS:
                        self.advance()
                        ph: Token = self.advance()
                        if ph.type == TOKEN_IDENT:
                            prop.phandles.append(ph.value)
                    elif inner.type == TOKEN_IDENT:
                        self.advance()
                    else:
                        break
                if self.peek() and self.peek().type == TOKEN_RANGLE:
                    self.advance()
            elif t.type == TOKEN_AMPERS:
                self.advance()
                ph = self.advance()
                if ph.type == TOKEN_IDENT:
                    prop.phandles.append(ph.value)
            elif t.type == TOKEN_COMMA:
                self.advance()
            else:
                break
        if self.peek() and self.peek().type == TOKEN_SEMI:
            self.advance()

    def parse_value_sequence(self) -> List[Tuple[str, Any]]:
        """解析值序列直到 ; 或 }"""
        values: List[Tuple[str, Any]] = []
        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
            t: Token = self.advance()
            if t.type == TOKEN_STRING:
                values.append(('string', t.value))
            elif t.type == TOKEN_INT:
                values.append(('int', t.value))
            elif t.type == TOKEN_LANGLE:
                while self.peek() and self.peek().type != TOKEN_RANGLE:
                    it: Token = self.advance()
                    if it.type == TOKEN_INT:
                        values.append(('int', it.value))
                    elif it.type == TOKEN_AMPERS:
                        ph: Token = self.advance()
                        if ph.type == TOKEN_IDENT:
                            values.append(('phandle', ph.value))
                    elif it.type == TOKEN_IDENT:
                        pass
                if self.peek() and self.peek().type == TOKEN_RANGLE:
                    self.advance()
            elif t.type == TOKEN_AMPERS:
                ph = self.advance()
                if ph.type == TOKEN_IDENT:
                    values.append(('phandle', ph.value))
        return values


# =========================================================================
#  DTS 编译器 (主逻辑)
# =========================================================================

class DTSCompiler:
    """DTS 编译器: 解析 → 解析 → 生成"""

    def __init__(self, dts_path: str, driver_dirs: Optional[List[str]] = None) -> None:
        self.dts_path: str = dts_path
        self.driver_dirs: List[str] = driver_dirs or []
        self.root: Optional[DtsNode] = None
        self.label_map: Dict[str, DtsNode] = {}
        self.alias_map: Dict[str, Any] = {}
        self.chosen_map: Dict[str, DtsNode] = {}
        self.device_list: List[DtsNode] = []
        self.driver_map: Dict[str, Tuple[str, str]] = {}
        self.interrupt_controllers: Dict[str, Tuple[DtsNode, int]] = {}  # label → (node, #interrupt-cells)
        self.device_irq_info: List[List[Tuple[int, int, int]]] = []  # index matches device_list
        self._macros: Dict[str, str] = {}
        self._visited: Set[str] = set()

    def _preprocess(self, text: str) -> str:
        """类 CPP 预处理: #include 展开 + #define 宏替换"""
        base_dir: str = os.path.dirname(os.path.abspath(self.dts_path))
        result: List[str] = self._preprocess_lines(text, base_dir)
        return '\n'.join(result)

    def _preprocess_lines(self, text: str, base_dir: str) -> List[str]:
        lines: List[str] = text.split('\n')
        out: List[str] = []
        skip_depth: int = 0

        for line in lines:
            stripped: str = line.strip()

            m = re.match(r'#include\s+"([^"]+)"', stripped)
            if not m:
                m = re.match(r'#include\s+<([^>]+)>', stripped)
            if not m:
                m = re.match(r'/include/\s+"([^"]+)"', stripped)
            if m and skip_depth == 0:
                inc_path: Optional[str] = self._resolve_inc(m.group(1), base_dir)
                if inc_path:
                    with open(inc_path, 'r', encoding='utf-8') as f:
                        inc_text: str = f.read()
                    inc_lines: List[str] = self._preprocess_lines(
                        inc_text, os.path.dirname(inc_path))
                    out.extend(inc_lines)
                continue

            if stripped.startswith('#'):
                m_def = re.match(r'#define\s+(\w+)\s*(.*)', stripped)
                m_ifn = re.match(r'#ifndef\s+(\w+)', stripped)
                m_ifd = re.match(r'#ifdef\s+(\w+)', stripped)

                if m_def and skip_depth == 0:
                    self._macros[m_def.group(1)] = m_def.group(2).strip()
                    continue
                elif m_ifn and skip_depth == 0:
                    macro_name: str = m_ifn.group(1)
                    if macro_name in self._macros:
                        skip_depth = 1
                    continue
                elif m_ifd and skip_depth == 0:
                    macro_name = m_ifd.group(1)
                    if macro_name not in self._macros:
                        skip_depth = 1
                    continue
                elif stripped == '#endif' and skip_depth > 0:
                    skip_depth -= 1
                    continue

            if skip_depth > 0:
                continue

            out.append(self._replace_macros(line))

        return out

    def _replace_macros(self, text: str) -> str:
        """将文本中已知宏替换为值 (贪心匹配, 长名优先)"""
        if not self._macros:
            return text
        for name in sorted(self._macros, key=lambda n: -len(n)):
            text = re.sub(r'\b' + re.escape(name) + r'\b',
                          self._macros[name], text)
        return text

    def _resolve_inc(self, name: str, base_dir: str) -> Optional[str]:
        """解析 #include 路径, 返回绝对路径; 已包含则返回 None"""
        candidates: List[str] = [
            os.path.join(base_dir, name),
            os.path.join(os.getcwd(), name),
        ]
        for p in candidates:
            p = os.path.normpath(p)
            if os.path.isfile(p):
                if p in self._visited:
                    return None
                self._visited.add(p)
                return p
        print(f"[dtc-lite] warning: include not found: '{name}' "
              f"(searched {base_dir}, {os.getcwd()})", file=sys.stderr)
        return None

    def compile(self) -> DTSCompiler:
        """完整编译流程"""
        with open(self.dts_path, 'r', encoding='utf-8') as f:
            text: str = f.read()
        text = self._preprocess(text)
        tokenizer: Tokenizer = Tokenizer(text, self.dts_path)
        tokens: List[Token] = tokenizer.tokenize()
        parser: DtsParser = DtsParser(tokens)
        self.root = parser.parse()

        self._build_label_map(self.root)
        self._merge_overlays()
        self._parse_special_nodes()
        self._scan_interrupt_controllers()
        self.device_list = self.root.collect_all_devices()
        self._deduplicate_devices()
        self._resolve_interrupts()
        self._scan_drivers()
        self._validate_compatibles()

        return self

    def _deduplicate_devices(self) -> None:
        """去重 (同名节点只保留一个)"""
        seen: Set[str] = set()
        unique: List[DtsNode] = []
        for dev in self.device_list:
            key: str = dev.path
            if key not in seen:
                seen.add(key)
                unique.append(dev)
        self.device_list = unique

    def _scan_interrupt_controllers(self) -> None:
        """DFS 扫描所有含 interrupt-controller 的节点, 构建映射"""
        def _dfs(node: DtsNode) -> None:
            if node.get_prop('interrupt-controller'):
                cells_prop: Optional[DtsProperty] = node.get_prop('#interrupt-cells')
                cells: int = cells_prop.ints[0] if cells_prop and cells_prop.ints else 1
                if node.label:
                    self.interrupt_controllers[node.label] = (node, cells)
            for child in node.children:
                _dfs(child)
        if self.root:
            _dfs(self.root)

    def _resolve_interrupts(self) -> None:
        """为所有设备解析 interrupts → 猜测 irq 号"""
        self.device_irq_info = [[] for _ in self.device_list]
        for i, dev in enumerate(self.device_list):
            irqs: List[Tuple[int, int, int]] = self._resolve_device_interrupts(dev)
            self.device_irq_info[i] = irqs

    def _resolve_device_interrupts(self, dev: DtsNode) -> List[Tuple[int, int, int]]:
        """解析单个设备的 interrupts 属性

        返回 [(irq, type, flags), ...] 元组列表:
          - #interrupt-cells=1: 单 cell 即为 irq, type=0, flags=0
          - #interrupt-cells=2: cell[0]=irq, cell[1]=type, flags=0
          - #interrupt-cells=3: cell[0]=type, cell[1]=irq, cell[2]=flags (GIC 风格)
        """
        prop: Optional[DtsProperty] = dev.get_prop('interrupts')
        if not prop or not prop.ints:
            return []

        ints: List[int] = prop.ints

        # 找 interrupt-parent (先看自身, 再沿父链)
        parent_label: Optional[str] = None
        ip_prop: Optional[DtsProperty] = dev.get_prop('interrupt-parent')
        if ip_prop and ip_prop.phandles:
            parent_label = ip_prop.phandles[0]
        if not parent_label:
            p: Optional[DtsNode] = dev.parent
            while p:
                ip_prop = p.get_prop('interrupt-parent')
                if ip_prop and ip_prop.phandles:
                    parent_label = ip_prop.phandles[0]
                    break
                p = p.parent

        cells: int = 1  # default
        if parent_label and parent_label in self.interrupt_controllers:
            _, cells = self.interrupt_controllers[parent_label]

        result: List[Tuple[int, int, int]] = []
        for j in range(0, len(ints), cells):
            chunk: List[int] = ints[j:j + cells]
            if cells == 1:
                result.append((chunk[0], 0, 0))
            elif cells == 2:
                result.append((chunk[0], chunk[1], 0))
            elif cells >= 3:
                result.append((chunk[1], chunk[0], chunk[2]))
            else:
                result.append((chunk[0], 0, 0))
        return result

    def _build_label_map(self, node: DtsNode) -> None:
        """递归构建 label → node 映射"""
        if node.label:
            self.label_map[node.label] = node
        for c in node.children:
            self._build_label_map(c)

    def _merge_overlays(self) -> None:
        """将 &label { ... } overlay 节点合并到 label 对应的目标节点。

        解析器已将 &label { body } 挂为父节点的子节点(名称为 &label)，
        这里后处理: 在 label_map 中找到目标，合并属性+子节点，移除引用。
        """
        refs: List[DtsNode] = []
        self._collect_ref_nodes(self.root, refs)

        for ref in refs:
            label: str = ref.name[1:]  # 去掉 "&" 前缀
            target: Optional[DtsNode] = self.label_map.get(label)
            if target is None:
                print(f"[dtc-lite] warning: overlay references unknown label "
                      f"'{label}' at line {ref.line}", file=sys.stderr)
                continue

            # 合并属性: 同名覆盖，新属性追加
            for prop in ref.props:
                existing: Optional[DtsProperty] = target.get_prop(prop.name)
                if existing:
                    existing.strings = prop.strings
                    existing.ints = prop.ints
                    existing.phandles = prop.phandles
                else:
                    target.props.append(prop)

            # 合并子节点: 递归检查同名子节点并合并
            for child in ref.children:
                existing_child: Optional[DtsNode] = target.find_child(child.name)
                if existing_child:
                    self._merge_node(existing_child, child)
                else:
                    child.parent = target
                    target.children.append(child)

            # 从父节点移除引用节点
            if ref.parent:
                ref.parent.children = [c for c in ref.parent.children
                                       if c is not ref]

    def _collect_ref_nodes(self, node: DtsNode, refs: List[DtsNode]) -> None:
        """DFS 收集所有 &label 引用节点"""
        if node.name.startswith('&'):
            refs.append(node)
        for child in node.children:
            self._collect_ref_nodes(child, refs)

    def _merge_node(self, target: DtsNode, src: DtsNode) -> None:
        """将 src 节点的属性+子节点合并到 target 节点"""
        for prop in src.props:
            existing: Optional[DtsProperty] = target.get_prop(prop.name)
            if existing:
                existing.strings = prop.strings
                existing.ints = prop.ints
                existing.phandles = prop.phandles
            else:
                target.props.append(prop)
        for child in src.children:
            existing_child: Optional[DtsNode] = target.find_child(child.name)
            if existing_child:
                self._merge_node(existing_child, child)
            else:
                child.parent = target
                target.children.append(child)

    def _parse_special_nodes(self) -> None:
        """解析 aliases 和 chosen 节点"""
        if self.root is None:
            return
        aliases_node: Optional[DtsNode] = self.root.find_node_by_path('/aliases')
        if aliases_node:
            for prop in aliases_node.props:
                if prop.phandles:
                    label: str = prop.phandles[0]
                    if label in self.label_map:
                        self.alias_map[prop.name] = self.label_map[label]
                elif prop.strings:
                    self.alias_map[prop.name] = prop.strings[0]

        chosen_node: Optional[DtsNode] = self.root.find_node_by_path('/chosen')
        if chosen_node:
            for prop in chosen_node.props:
                if prop.phandles:
                    label = prop.phandles[0]
                    if label in self.label_map:
                        self.chosen_map[prop.name] = self.label_map[label]
                elif prop.strings:
                    node: Optional[DtsNode] = self.root.find_node_by_path(prop.strings[0])
                    if node:
                        self.chosen_map[prop.name] = node

    def _scan_drivers(self) -> None:
        """扫描驱动源文件, 提取 DRIVER_REGISTER 宏"""
        pattern: re.Pattern[str] = re.compile(
            r'DRIVER_REGISTER\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\)'
        )

        for drv_dir in self.driver_dirs:
            if not os.path.isdir(drv_dir):
                continue
            for root_dir, dirs, files in os.walk(drv_dir):
                for f in files:
                    if f.endswith('.c') or f.endswith('.h'):
                        path: str = os.path.join(root_dir, f)
                        if os.path.getsize(path) > 1024 * 1024:
                            continue
                        try:
                            with open(path, 'r', encoding='utf-8') as fh:
                                content: str = fh.read()
                            for m in pattern.finditer(content):
                                drv_name: str = m.group(1)
                                compat: str = m.group(2)
                                probe_fn: str = f"board_driver_probe_{drv_name}"
                                remove_fn: str = f"board_driver_remove_{drv_name}"
                                self.driver_map[compat] = (probe_fn, remove_fn)
                        except Exception:
                            pass

    def _validate_compatibles(self) -> None:
        """验证: 所有设备的 compatible 在 driver_map 中都有对应"""
        PLATFORM: Set[str] = {
            'esp32,cpu',
            'esp32,spi-bus',
            'esp32,i2s-bus',
            'esp32,uart',
            'esp32,gpio',
            'esp32,i2c-bus',
            'esp32,rmt-tx',
            'esp32,adc',
            'arm,gic-400',
            'arm,cortex-a12',
            'arm,cortex-a7',
            'arm,cortex-m4',
            'arm,cortex-m3',
            'arm,cortex-m7',
            'arm,cortex-m0',
            'arm,armv7-timer',
        }
        errors: List[str] = []
        for dev in self.device_list:
            if dev.parent is None:
                continue
            compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
            if compat_prop and compat_prop.strings:
                compat: str = compat_prop.strings[0]
                if compat in PLATFORM:
                    continue
                if compat not in self.driver_map:
                    status_prop: Optional[DtsProperty] = dev.get_prop('status')
                    is_disabled: bool = (status_prop and
                                         status_prop.strings and
                                         status_prop.strings[0] == 'disabled')
                    if not is_disabled:
                        errors.append(
                            f"device '{dev.path}' (compatible='{compat}'): "
                            f"no driver registered for this compatible string"
                        )
        if errors:
            for e in errors:
                print(f"ERROR: {e}", file=sys.stderr)
            if self.driver_dirs:
                sys.exit(1)

    def _resolve_address_cells(self, node: DtsNode) -> Tuple[int, int]:
        """沿父链向上查找 #address-cells / #size-cells, 默认 1/1"""
        ac: int = 1
        sc: int = 1
        parent: Optional[DtsNode] = node.parent
        while parent:
            ac_prop: Optional[DtsProperty] = parent.get_prop('#address-cells')
            if ac_prop and ac_prop.ints:
                ac = ac_prop.ints[0]
                break
            parent = parent.parent
        parent = node.parent
        while parent:
            sc_prop: Optional[DtsProperty] = parent.get_prop('#size-cells')
            if sc_prop and sc_prop.ints:
                sc = sc_prop.ints[0]
                break
            parent = parent.parent
        return ac, sc

    def get_device_deps(self, dev: DtsNode) -> List[str]:
        """获取设备的依赖 phandle 列表"""
        deps: List[str] = []
        for pname in ('depends-on', 'depends_on'):
            prop: Optional[DtsProperty] = dev.get_prop(pname)
            if prop:
                deps.extend(prop.phandles)
        if dev.parent and dev.parent.name not in ('', 'soc', 'display', 'audio', 'input', 'leds', 'storage'):
            parent_label: Optional[str] = dev.parent.label
            if parent_label:
                deps.append(parent_label)
        return deps

    def topological_sort(self) -> List[int]:
        """
        对设备列表进行拓扑排序 (Kahn's algorithm)
        返回按依赖顺序排列的设备索引列表
        """
        n: int = len(self.device_list)
        name_to_idx: Dict[str, int] = {}
        label_to_idx: Dict[str, int] = {}
        for i, dev in enumerate(self.device_list):
            name_to_idx[dev.name] = i
            if dev.label:
                label_to_idx[dev.label] = i

        graph: List[List[int]] = [[] for _ in range(n)]
        in_degree: List[int] = [0] * n

        for i, dev in enumerate(self.device_list):
            deps: List[str] = self.get_device_deps(dev)
            dep_indices: Set[int] = set()
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_indices.add(label_to_idx[dep_label])
            for di in dep_indices:
                graph[di].append(i)
                in_degree[i] += 1

        queue: List[int] = [i for i in range(n) if in_degree[i] == 0]
        result: List[int] = []

        while queue:
            node: int = queue.pop(0)
            result.append(node)
            for neighbor in graph[node]:
                in_degree[neighbor] -= 1
                if in_degree[neighbor] == 0:
                    queue.append(neighbor)

        if len(result) != n:
            cycle_nodes: List[str] = [self.device_list[i].path for i in range(n) if in_degree[i] > 0]
            raise RuntimeError(
                f"Circular dependency detected among devices: {cycle_nodes}"
            )

        return result

    def compute_cascade_tables(self) -> Dict[int, List[int]]:
        """
        计算故障传播表: 对于每个设备, 找出所有传递依赖它的设备.
        返回 cascade_map = { dev_idx: [dependent_idx, ...] }
        """
        n: int = len(self.device_list)
        label_to_idx: Dict[str, int] = {}
        for i, dev in enumerate(self.device_list):
            if dev.label:
                label_to_idx[dev.label] = i

        graph: List[List[int]] = [[] for _ in range(n)]
        for i, dev in enumerate(self.device_list):
            deps: List[str] = self.get_device_deps(dev)
            dep_set: Set[int] = set()
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_set.add(label_to_idx[dep_label])
            for di in dep_set:
                graph[di].append(i)

        order: List[int] = self.topological_sort()
        order_idx: Dict[int, int] = {dev: pos for pos, dev in enumerate(order)}

        cascade: Dict[int, List[int]] = {}
        for i in range(n):
            visited: Set[int] = set()
            stack: List[int] = list(graph[i])
            while stack:
                child: int = stack.pop()
                if child in visited:
                    continue
                visited.add(child)
                for grandchild in graph[child]:
                    if grandchild not in visited:
                        stack.append(grandchild)
            sorted_visited: List[int] = sorted(visited, key=lambda x: order_idx.get(x, x))
            if sorted_visited:
                cascade[i] = sorted_visited

        return cascade


# =========================================================================
#  C 代码生成器
# =========================================================================

class CGenerator:
    """从编译结果生成 C 代码"""

    def __init__(self, compiler: DTSCompiler, output_dir: str) -> None:
        self.compiler: DTSCompiler = compiler
        self.output_dir: str = output_dir
        os.makedirs(output_dir, exist_ok=True)

    def _atomic_write(self, path: str, content: str) -> None:
        """原子写入：先写临时文件再 mv 替换，防止生成残缺的 C 文件"""
        if not content.endswith('\n'):
            content += '\n'
        tmp: str = path + ".tmp"
        try:
            with open(tmp, 'w', encoding='utf-8') as f:
                f.write(content)
            shutil.move(tmp, path)
        except Exception:
            if os.path.exists(tmp):
                os.unlink(tmp)
            raise

    def _write_if_changed(self, path: str, content: str) -> None:
        """防抖写入: 内容一致时不触碰磁盘, 保护 Makefile 时间戳"""
        if not content.endswith('\n'):
            content += '\n'
        try:
            display: str = os.path.relpath(path)
        except ValueError:
            display = os.path.basename(path)
        if os.path.exists(path):
            with open(path, 'r', encoding='utf-8') as f:
                if f.read() == content:
                    print(f"  [keep] {display}")
                    return
        self._atomic_write(path, content)
        print(f"  [gen]  {display}")

    def gen_all(self) -> None:
        """生成所有输出文件"""
        self._gen_board_nodes_h()
        self._gen_board_devtable_h()
        self._gen_board_devtable_c()
        self._gen_board_probe_c()
        self._gen_board_handles_h()
        self._gen_dt_config_h()
        self._gen_board_force_link_c()

    def _snake_name(self, name: str) -> str:
        """设备名 → 枚举/变量名 (sanitize)"""
        n: str = name.replace('@', '_').replace('-', '_').replace('.', '_').replace('/', '_')
        return n.upper()

    def _c_safe_name(self, name: str) -> str:
        """设备名 → C 标识符"""
        n: str = name.replace('@', '_').replace('-', '_').replace('.', '_').replace('/', '_')
        return n

    def _generate_includes(self) -> str:
        lines: List[str] = [
            '#include <stdint.h>',
            '#include <stddef.h>',
            '#include <stdbool.h>',
            '#include <string.h>',
        ]
        return '\n'.join(lines)

    def _gen_board_nodes_h(self) -> None:
        """生成 board_nodes.h — DEV_ID 枚举 + chosen/alias 宏"""
        devs: List[DtsNode] = self.compiler.device_list
        path: str = os.path.join(self.output_dir, 'board_nodes.h')

        lines: List[str] = [
            '#ifndef BOARD_NODES_H',
            '#define BOARD_NODES_H',
            '',
            '#include <stdint.h>',
            '',
            '/* ===== 设备 ID 枚举 (自动生成) ===== */',
            'typedef enum {',
        ]

        for i, dev in enumerate(devs):
            name: str = self._snake_name(dev.name)
            lines.append(f'    DEV_ID_{name} = {i},')

        lines += [
            f'    DEV_ID_COUNT = {len(devs)}',
            '} device_id_t;',
            '',
        ]

        if self.compiler.chosen_map:
            lines.append('/* ===== chosen 设备 ===== */')
            for key, node in self.compiler.chosen_map.items():
                cname: str = self._snake_name(key)
                dname: str = self._snake_name(node.name)
                lines.append(f'#define CHOSEN_{cname}    DEV_ID_{dname}')
            lines.append('')

        if self.compiler.alias_map:
            lines.append('/* ===== alias 宏 ===== */')
            for key, node in self.compiler.alias_map.items():
                cname = self._snake_name(key)
                dname = self._snake_name(node.name)
                lines.append(f'#define ALIAS_{cname}      DEV_ID_{dname}')
            lines.append('')

        lines.append('#endif /* BOARD_NODES_H */')
        lines.append('')

        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_force_link_c(self) -> None:
        """生成 board_force_link.c — 强制链接器保留所有 driver_map 中的 probe 函数"""
        path: str = os.path.join(self.output_dir, 'board_force_link.c')
        driver_map: Dict[str, Tuple[str, str]] = self.compiler.driver_map

        if not driver_map:
            self._write_if_changed(path, '/* no drivers registered — nothing to force-link */')
            print(f"  [gen] {os.path.basename(path)} (empty)")
            return

        externs: List[str] = []
        refs: List[str] = []
        for compat, (probe_fn, _) in sorted(driver_map.items()):
            externs.append(f'extern int __attribute__((weak)) {probe_fn}(device_t*);')
            refs.append(f'    s_fake_ref = (void*){probe_fn};')

        lines: List[str] = [
            '#include "device.h"',
            '',
            '/* ===== 自动生成 — 由 dtc-lite.py 扫描 DRIVER_REGISTER 宏生成 ===== */',
            '/* 每新增驱动, 重新构建即可自动更新此文件, 无需手动编辑.          */',
            '',
        ] + externs + [
            '',
            'static volatile void* s_fake_ref;',
            '',
            'static void __attribute__((constructor, used)) _force_probe_link(void)',
            '{',
        ] + refs + [
            '}',
        ]

        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_devtable_h(self) -> None:
        """生成 board_devtable.h — 设备表访问 API"""
        path: str = os.path.join(self.output_dir, 'board_devtable.h')

        lines: List[str] = [
            '#ifndef BOARD_DEVTABLE_H',
            '#define BOARD_DEVTABLE_H',
            '',
            '#include "board_nodes.h"',
            '#include "device.h"',
            '',
            '#ifdef __cplusplus',
            'extern "C" {',
            '#endif',
            '',
            '/* 编译期节点访问 (静态 .rodata) */',
            'const device_node_t* board_node_get(device_id_t id);',
            'int board_dev_count(void);',
            'device_id_t board_dev_find(const char* name);',
            'device_id_t board_dev_find_by_compat(const char* compatible);',
            'device_id_t board_dev_find_by_label(const char* label);',
            '',
            '/* 运行时设备实例访问 (由 board_device.c 管理) */',
            'device_t* board_dev_get(device_id_t id);',
            '',
            '/* probe 顺序表 (按依赖拓扑排序) */',
            'const device_id_t* board_probe_order(void);',
            'int board_probe_order_count(void);',
            '',
            '/* probe / remove 调度 */',
            'typedef int (*probe_fn_t)(device_t*);',
            'typedef int (*remove_fn_t)(device_t*);',
            'probe_fn_t board_probe_get_fn(device_id_t id);',
            'remove_fn_t board_remove_get_fn(device_id_t id);',
            '',
            '/* 故障传播表: id 失败时应一并禁用的设备列表 */',
            'const device_id_t* board_cascade_get(device_id_t id, int* count);',
            '',
            '#ifdef __cplusplus',
            '}',
            '#endif',
            '',
            '#endif /* BOARD_DEVTABLE_H */',
        ]
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_devtable_c(self) -> None:
        """生成 board_devtable.c — 静态 device_node_t 表"""
        devs: List[DtsNode] = self.compiler.device_list
        path: str = os.path.join(self.output_dir, 'board_devtable.c')

        prop_arrays: List[str] = []
        dep_arrays: List[str] = []

        for i, dev in enumerate(devs):
            safe: str = self._c_safe_name(dev.name)
            prop_list: List[DtsProperty] = [p for p in dev.props if p.name not in
                         ('compatible', 'depends-on', 'depends_on', 'status', 'criticality')]
            if prop_list:
                prop_arrays.append(f'/* {dev.path} */')
                prop_arrays.append(f'static const device_prop_t DEV_{safe}_props[] = {{')
                for p in prop_list:
                    val: str = ""
                    if p.ints:
                        val = " ".join(hex(i) for i in p.ints)
                    elif p.strings:
                        val = p.strings[0]
                    elif p.phandles:
                        val = p.phandles[0]
                    prop_arrays.append(f'    {{"{p.name}", "{val}"}},')
                prop_arrays.append('};')
                prop_arrays.append('')

        label_to_idx: Dict[str, int] = {}
        for i, dev in enumerate(devs):
            if dev.label:
                label_to_idx[dev.label] = i

        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)
            deps: List[str] = self.compiler.get_device_deps(dev)
            dep_ids: List[int] = []
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_ids.append(label_to_idx[dep_label])
            if dep_ids:
                dep_arrays.append(f'static const device_id_t DEV_{safe}_deps[] = {{')
                dep_arrays.append(f'    {", ".join(f"DEV_ID_{self._snake_name(devs[di].name)}" for di in dep_ids)},')
                dep_arrays.append('};')
                dep_arrays.append('')

        # ===== reg 分组表 =====
        reg_data_arrays: List[str] = []
        reg_arrays: List[str] = []
        reg_info_map: Dict[int, Tuple[int, List[str]]] = {}  # dev_idx -> (reg_count, reg_symbol)
        for i, dev in enumerate(devs):
            safe: str = self._c_safe_name(dev.name)
            reg_prop: Optional[DtsProperty] = dev.get_prop('reg')
            if not reg_prop or not reg_prop.ints:
                reg_info_map[i] = (0, 'NULL')
                continue

            ac, sc = self.compiler._resolve_address_cells(dev)
            ints: List[int] = reg_prop.ints
            entry_size: int = ac + sc
            if entry_size <= 0:
                reg_info_map[i] = (0, 'NULL')
                continue

            # 按 (ac+sc) 分组为多个 reg 条目
            entries: List[Tuple[List[int], List[int]]] = []
            for j in range(0, len(ints), entry_size):
                chunk: List[int] = ints[j:j + entry_size]
                addr_part: List[int] = chunk[:ac]
                size_part: List[int] = chunk[ac:] if sc > 0 else []
                entries.append((addr_part, size_part))

            # 生成平铺数据数组
            flat: List[str] = []
            for addr_part, size_part in entries:
                flat.extend(hex(v) for v in addr_part)
                flat.extend(hex(v) for v in size_part)

            reg_data_arrays.append(f'static const uint32_t DEV_{safe}_REG_DATA[] = {{')
            reg_data_arrays.append(f'    {", ".join(flat)},')
            reg_data_arrays.append('};')

            # 生成 device_reg_t 数组
            offset: int = 0
            reg_entries: List[str] = []
            for addr_part, size_part in entries:
                size_ref: str = f'&DEV_{safe}_REG_DATA[{offset + len(addr_part)}]'
                if sc == 0:
                    size_ref = 'NULL'
                reg_entries.append(
                    f'    {{ .addr = &DEV_{safe}_REG_DATA[{offset}], .addr_cells = {ac},'
                    f' .size = {size_ref}, .size_cells = {sc} }},'
                )
                offset += len(addr_part) + len(size_part)

            reg_arr_name: str = f'DEV_{safe}_REGS'
            reg_arrays.append(f'static const device_reg_t {reg_arr_name}[] = {{')
            reg_arrays.extend(reg_entries)
            reg_arrays.append('};')
            reg_arrays.append('')
            reg_info_map[i] = (len(entries), reg_arr_name)

        # ===== irq 表 =====
        irq_arrays: List[str] = []
        irq_info_map: Dict[int, Tuple[int, str]] = {}
        for i, dev in enumerate(devs):
            safe_i: str = self._c_safe_name(dev.name)
            irq_list: List[Tuple[int, int, int]] = self.compiler.device_irq_info[i] if i < len(self.compiler.device_irq_info) else []
            if not irq_list:
                irq_info_map[i] = (0, 'NULL')
                continue
            irq_entries_str: List[str] = []
            for irq, typ, flags in irq_list:
                irq_entries_str.append(f'    {{ .irq = {irq}, .type = {typ}, .flags = {flags} }},')
            irq_arr_name: str = f'DEV_{safe_i}_IRQS'
            irq_arrays.append(f'static const device_irq_t {irq_arr_name}[] = {{')
            irq_arrays.extend(irq_entries_str)
            irq_arrays.append('};')
            irq_arrays.append('')
            irq_info_map[i] = (len(irq_list), irq_arr_name)

        node_entries: List[str] = []
        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)

            compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
            compat_str: str = compat_prop.strings[0] if compat_prop and compat_prop.strings else ""

            status_prop: Optional[DtsProperty] = dev.get_prop('status')
            if status_prop and status_prop.strings and status_prop.strings[0] == 'disabled':
                status_val: str = 'DEVICE_STATUS_DISABLED'
            else:
                status_val = 'DEVICE_STATUS_READY'

            crit_prop: Optional[DtsProperty] = dev.get_prop('criticality')
            if crit_prop and crit_prop.strings:
                crit_map: Dict[str, str] = {'fatal': 'DEVICE_CRIT_FATAL', 'warning': 'DEVICE_CRIT_WARNING', 'ignore': 'DEVICE_CRIT_IGNORE'}
                crit_val: str = crit_map.get(crit_prop.strings[0].lower(), 'DEVICE_CRIT_WARNING')
            else:
                crit_val = 'DEVICE_CRIT_WARNING'

            EXCLUDED_PROPS: Tuple[str, ...] = ('compatible', 'depends-on', 'depends_on', 'status', 'criticality', 'direct')

            prop_ref: str = f'DEV_{safe}_props' if any(
                p.name not in EXCLUDED_PROPS
                for p in dev.props
            ) else 'NULL'

            dep_ref: str = f'DEV_{safe}_deps' if any(
                dep in label_to_idx for dep in self.compiler.get_device_deps(dev)
            ) else 'NULL'

            dep_count: int = sum(1 for dep in self.compiler.get_device_deps(dev)
                          if dep in label_to_idx)
            label_val: str = dev.label or ""

            direct_prop: Optional[DtsProperty] = dev.get_prop('direct')
            flags_val: str = 'DEVICE_FLAG_DIRECT' if direct_prop else '0'

            reg_count: int
            reg_ref: str
            reg_count, reg_ref = reg_info_map[i]

            irq_count: int
            irq_ref: str
            irq_count, irq_ref = irq_info_map[i]

            node_entries.append(
                f'    [DEV_ID_{self._snake_name(dev.name)}] = {{\n'
                f'        .name       = "{dev.name}",\n'
                f'        .label      = "{label_val}",\n'
                f'        .compatible = "{compat_str}",\n'
                f'        .path       = "{dev.path}",\n'
                f'        .status     = {status_val},\n'
                f'        .criticality = {crit_val},\n'
                f'        .flags      = {flags_val},\n'
                f'        .prop_count = {len([p for p in dev.props if p.name not in EXCLUDED_PROPS])},\n'
                f'        .props      = {prop_ref},\n'
                f'        .dep_count  = {dep_count},\n'
                f'        .deps       = (const device_id_t*){dep_ref},\n'
                f'        .reg_count  = {reg_count},\n'
                f'        .regs       = (const device_reg_t*){reg_ref},\n'
                f'        .irq_count  = {irq_count},\n'
                f'        .irqs       = (const device_irq_t*){irq_ref},\n'
                f'    }},'
            )

        lines: List[str] = [
            '#include "board_nodes.h"',
            '#include "board_devtable.h"',
            '#include "device.h"',
            '',
            self._generate_includes(),
            '',
            '/* ===== 属性表 (静态 .rodata) ===== */',
            '',
        ] + prop_arrays + [
            '/* ===== 依赖表 ===== */',
            '',
        ] + dep_arrays + [
            '/* ===== reg 分组表 (预分组, 按 #address-cells / #size-cells) ===== */',
            '',
        ] + reg_data_arrays + reg_arrays + [
            '/* ===== irq 表 (预分组, 按 #interrupt-cells) ===== */',
            '',
        ] + irq_arrays + [
            '/* ===== 主节点表 (只读 .rodata) ===== */',
            f'static const device_node_t s_nodes[DEV_ID_COUNT] = {{',
        ] + node_entries + [
            '};',
            '',
            '/* ===== API 实现 ===== */',
            '',
            'const device_node_t* board_node_get(device_id_t id) {',
            '    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;',
            '    return &s_nodes[id];',
            '}',
            '',
            'int board_dev_count(void) { return DEV_ID_COUNT; }',
            '',
            'device_id_t board_dev_find(const char* name) {',
            '    if (!name) return -1;',
            '    for (int i = 0; i < DEV_ID_COUNT; i++) {',
            '        if (strcmp(s_nodes[i].name, name) == 0)',
            '            return (device_id_t)i;',
            '    }',
            '    return -1;',
            '}',
            '',
            'device_id_t board_dev_find_by_compat(const char* compatible) {',
            '    if (!compatible) return -1;',
            '    for (int i = 0; i < DEV_ID_COUNT; i++) {',
            '        if (s_nodes[i].compatible[0] &&',
            '            strcmp(s_nodes[i].compatible, compatible) == 0)',
            '            return (device_id_t)i;',
            '    }',
            '    return -1;',
            '}',
            '',
            'device_id_t board_dev_find_by_label(const char* label) {',
            '    if (!label || !label[0]) return -1;',
            '    for (int i = 0; i < DEV_ID_COUNT; i++) {',
            '        if (s_nodes[i].label[0] &&',
            '            strcmp(s_nodes[i].label, label) == 0)',
            '            return (device_id_t)i;',
            '    }',
            '    return -1;',
            '}',
            '',
        ]
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_probe_c(self) -> None:
        """生成 board_probe.c — probe/remove 表 + 拓扑排序顺序"""
        devs: List[DtsNode] = self.compiler.device_list
        order: List[int] = self.compiler.topological_sort()
        path: str = os.path.join(self.output_dir, 'board_probe.c')

        PLATFORM: Set[str] = {
            'esp32,cpu',
            'esp32,spi-bus',
            'esp32,i2s-bus',
            'esp32,uart',
            'esp32,gpio',
            'esp32,i2c-bus',
            'esp32,rmt-tx',
            'esp32,adc',
            'arm,gic-400',
            'arm,cortex-a12',
            'arm,cortex-a7',
            'arm,cortex-m4',
            'arm,cortex-m3',
            'arm,cortex-m7',
            'arm,cortex-m0',
            'arm,armv7-timer',
        }

        has_platform: bool = False

        probe_externs: List[str] = []
        remove_externs: List[str] = []
        probe_array: List[str] = []
        remove_array: List[str] = []
        for i in devs:
            compat_prop: Optional[DtsProperty] = i.get_prop('compatible')
            snake: str = self._snake_name(i.name)
            if compat_prop and compat_prop.strings:
                compat: str = compat_prop.strings[0]
                if compat in self.compiler.driver_map:
                    p_fn: str
                    r_fn: str
                    p_fn, r_fn = self.compiler.driver_map[compat]
                    probe_externs.append(f'extern int __attribute__((weak)) {p_fn}(device_t* dev);')
                    remove_externs.append(f'extern int __attribute__((weak)) {r_fn}(device_t* dev);')
                    probe_array.append(f'    [DEV_ID_{snake}] = {p_fn},')
                    remove_array.append(f'    [DEV_ID_{snake}] = {r_fn},')
                elif compat in PLATFORM:
                    has_platform = True
                    probe_array.append(f'    [DEV_ID_{snake}] = board_platform_probe,')
                    remove_array.append(f'    [DEV_ID_{snake}] = NULL,')
                else:
                    probe_array.append(f'    [DEV_ID_{snake}] = NULL,')
                    remove_array.append(f'    [DEV_ID_{snake}] = NULL,')
            else:
                probe_array.append(f'    [DEV_ID_{snake}] = NULL,')
                remove_array.append(f'    [DEV_ID_{snake}] = NULL,')

        order_entries: List[str] = []
        for idx in order:
            dev: DtsNode = devs[idx]
            order_entries.append(f'    DEV_ID_{self._snake_name(dev.name)},')

        lines: List[str] = [
            '#include "board_nodes.h"',
            '#include "board_devtable.h"',
            '#include "device.h"',
            '',
            '/* ===== probe 函数声明 ===== */',
        ] + probe_externs + [
            '',
            '/* ===== remove 函数声明 ===== */',
        ] + remove_externs

        if has_platform:
            lines += [
                '',
                '/* ===== 平台基础设施透传 probe (PLATFORM devices) ===== */',
                'static int board_platform_probe(device_t* dev) {',
                '    (void)dev;',
                '    return 0;',
                '}',
            ]

        lines += [
            '',
            '/* ===== probe 函数表 (按 DEV_ID 索引, .rodata) ===== */',
            f'static const probe_fn_t s_probe_fns[DEV_ID_COUNT] = {{',
        ] + probe_array + [
            '};',
            '',
            '/* ===== remove 函数表 (按 DEV_ID 索引, .rodata) ===== */',
            f'static const remove_fn_t s_remove_fns[DEV_ID_COUNT] = {{',
        ] + remove_array + [
            '};',
            '',
            '/* ===== probe 顺序 (按依赖拓扑排序) ===== */',
            f'static const device_id_t s_probe_order[DEV_ID_COUNT] = {{',
        ] + order_entries + [
            '};',
            '',
            '/* ===== API ===== */',
            '',
            'probe_fn_t board_probe_get_fn(device_id_t id) {',
            '    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;',
            '    return s_probe_fns[id];',
            '}',
            '',
            'remove_fn_t board_remove_get_fn(device_id_t id) {',
            '    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) return NULL;',
            '    return s_remove_fns[id];',
            '}',
            '',
            'const device_id_t* board_probe_order(void) {',
            '    return s_probe_order;',
            '}',
            '',
            'int board_probe_order_count(void) {',
            '    return DEV_ID_COUNT;',
            '}',
            '',
            '/* ===== 故障传播表 (编译期预计算, 替代运行时 BFS) ===== */',
        ]

        cascade: Dict[int, List[int]] = self.compiler.compute_cascade_tables()
        dev_count: int = len(devs)

        flat_data: List[int] = []
        counts: List[int] = [0] * dev_count
        for i in range(dev_count):
            if i in cascade:
                counts[i] = len(cascade[i])
                flat_data.extend(cascade[i])
            else:
                counts[i] = 0

        offsets: List[int] = [0] * dev_count
        cumulative: int = 0
        for i in range(dev_count):
            offsets[i] = cumulative
            cumulative += counts[i]

        if flat_data:
            lines += [
                f'static const device_id_t s_cascade_data[] = {{',
            ]
            for idx in order:
                if idx in cascade:
                    for dep_idx in cascade[idx]:
                        dep_dev: DtsNode = devs[dep_idx]
                        lines.append(f'    DEV_ID_{self._snake_name(dep_dev.name)},')
            lines += [
                '};',
                '',
                f'static const uint8_t s_cascade_counts[DEV_ID_COUNT] = {{',
            ]
            for i in range(dev_count):
                lines.append(f'    [DEV_ID_{self._snake_name(devs[i].name)}] = {counts[i]},')
            lines += [
                '};',
                '',
                f'static const uint16_t s_cascade_offset[DEV_ID_COUNT] = {{',
            ]
            for i in range(dev_count):
                lines.append(f'    [DEV_ID_{self._snake_name(devs[i].name)}] = {offsets[i]},')
            lines += [
                '};',
                '',
                'const device_id_t* board_cascade_get(device_id_t id, int* count) {',
                '    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) { *count = 0; return NULL; }',
                '    *count = s_cascade_counts[id];',
                '    return *count ? &s_cascade_data[s_cascade_offset[id]] : NULL;',
                '}',
            ]

        lines += ['']
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_handles_h(self) -> None:
        """生成 board_handles.h — chosen/alias 注入宏"""
        path: str = os.path.join(self.output_dir, 'board_handles.h')

        lines: List[str] = [
            '#ifndef BOARD_HANDLES_H',
            '#define BOARD_HANDLES_H',
            '',
            '#include "board_nodes.h"',
            '',
            '/*',
            ' * board_handles.h — 编译期确定的句柄宏',
            ' *',
            ' * 用于依赖注入: app 通过 chosen/alias 宏获取设备 ID,',
            ' * 再通过 board_dev_get(id) 获取 device_t*。',
            ' *',
            ' * 此文件取代 getInstance()/Service Locator 模式。',
            ' */',
            '',
        ]

        if self.compiler.chosen_map:
            lines.append('/* ===== chosen 设备 (系统关键设备) ===== */')
            lines.append('#ifndef BOARD_CHOSEN_DEFINED')
            lines.append('#define BOARD_CHOSEN_DEFINED')
            for key, node in self.compiler.chosen_map.items():
                dname: str = self._snake_name(node.name)
                lines.append(f'#define BOARD_CHOSEN_{self._snake_name(key)}   DEV_ID_{dname}')
            lines.append('#endif')
            lines.append('')

        if self.compiler.alias_map:
            lines.append('/* ===== alias 引用 ===== */')
            for key, node in self.compiler.alias_map.items():
                dname = self._snake_name(node.name)
                lines.append(f'#define BOARD_ALIAS_{self._snake_name(key)}     DEV_ID_{dname}')
            lines.append('')

        lines += [
            '#endif /* BOARD_HANDLES_H */',
            '',
        ]
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_dt_config_h(self) -> None:
        """生成 dt_config_gen.h — 从 DTS 设备计数自动生成 Pool Size 宏"""
        devs: List[DtsNode] = self.compiler.device_list
        path: str = os.path.join(self.output_dir, 'dt_config_gen.h')

        compat_counts: Dict[str, int] = {}
        for dev in devs:
            compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
            if compat_prop and compat_prop.strings:
                compat: str = compat_prop.strings[0]
                compat_counts[compat] = compat_counts.get(compat, 0) + 1

        def _compat_to_macro(compat: str) -> str:
            return compat.replace(',', '_').replace('-', '_').replace('.', '_').upper()

        lines: List[str] = [
            '#ifndef DT_CONFIG_GEN_H',
            '#define DT_CONFIG_GEN_H',
            '',
            '/* Auto-generated by dtc-lite.py — DO NOT EDIT */',
            '/*',
            ' * Resource limits derived from DTS device count per compatible.',
            ' * Clock/tick values extracted from DTS cpu@0 and chosen nodes.',
            ' *',
            ' * IEC 61508 §7.4.2.4: 静态资源分配必须由系统配置工具验证.',
            ' */',
            '',
        ]

        for compat in sorted(compat_counts.keys()):
            macro: str = _compat_to_macro(compat)
            lines.append(f'#define DTC_GEN_COUNT_{macro}  {compat_counts[compat]}')

        cpu_node: Optional[DtsNode] = None
        for dev in devs:
            if dev.path in ('/cpus/cpu@0', '/cpus/cpu@0/'):
                cpu_node = dev
                break
        if cpu_node:
            cf_prop: Optional[DtsProperty] = cpu_node.get_prop('clock-frequency')
            if cf_prop and cf_prop.ints:
                lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  {cf_prop.ints[0]}')
            else:
                lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  16000000')
        else:
            lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  16000000')

        chosen_node: Optional[DtsNode] = self.compiler.root.find_node_by_path('/chosen') if self.compiler.root else None
        if chosen_node:
            tr_prop: Optional[DtsProperty] = chosen_node.get_prop('tick-rate')
            if tr_prop and tr_prop.ints:
                lines.append(f'#define DTC_GEN_TICK_RATE_HZ  {tr_prop.ints[0]}')
            else:
                lines.append(f'#define DTC_GEN_TICK_RATE_HZ  1000')
        else:
            lines.append(f'#define DTC_GEN_TICK_RATE_HZ  1000')

        if chosen_node:
            hs_prop: Optional[DtsProperty] = chosen_node.get_prop('heap-size')
            if hs_prop and hs_prop.ints:
                lines.append(f'#define DTC_GEN_HEAP_SIZE  {hs_prop.ints[0]}')
            else:
                lines.append(f'#define DTC_GEN_HEAP_SIZE  32768')
        else:
            lines.append(f'#define DTC_GEN_HEAP_SIZE  32768')

        lines += [
            '',
            '#endif /* DT_CONFIG_GEN_H */',
            '',
        ]
        self._write_if_changed(path, '\n'.join(lines))


# =========================================================================
#  主函数
# =========================================================================

def main() -> None:
    parser = argparse.ArgumentParser(
        description="MCU 编译期 DeviceTree 编译器 — DTS → C 代码生成"
    )
    parser.add_argument("dts_path", type=str, help="输入的 .dts 文件路径")
    parser.add_argument("output_dir", type=str, help="生成文件的输出目录")
    parser.add_argument(
        "driver_dirs", type=str, nargs="*", default=[],
        help="扫描 DRIVER_REGISTER 宏的驱动源码目录 (可多个)",
    )
    args = parser.parse_args()

    dts_path: str = args.dts_path
    output_dir: str = args.output_dir
    driver_dirs: List[str] = args.driver_dirs

    if not os.path.isfile(dts_path):
        print(f"ERROR: DTS file not found: {dts_path}", file=sys.stderr)
        sys.exit(1)

    print(f"dtc-lite: {dts_path}")
    print(f"  output: {output_dir}")
    if driver_dirs:
        print(f"  driver scan: {', '.join(driver_dirs)}")

    compiler: DTSCompiler = DTSCompiler(dts_path, driver_dirs)
    compiler.compile()

    print(f"  devices: {len(compiler.device_list)}")
    for dev in compiler.device_list:
        compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
        compat: str = compat_prop.strings[0] if compat_prop and compat_prop.strings else "(no compatible)"
        deps: List[str] = compiler.get_device_deps(dev)
        dep_labels: str = ', '.join(deps) if deps else '(none)'
        print(f"    {dev.path:40s} compat={compat:25s} deps=[{dep_labels}]")

    print(f"  drivers matched: {len(compiler.driver_map)}")
    for compat, fn in sorted(compiler.driver_map.items()):
        print(f"    {compat:40s} → {fn}")

    generator: CGenerator = CGenerator(compiler, output_dir)
    generator.gen_all()

    print("dtc-lite: done")


if __name__ == '__main__':
    main()
