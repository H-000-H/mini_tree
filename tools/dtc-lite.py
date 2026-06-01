
#!/usr/bin/env python3
"""
dtc-lite.py — MCU 编译期 DeviceTree 编译器

编译期 DTS → C 代码生成器。
解析 MCU Lite DTS 格式，生成静态 C 结构体 + probe 表。

用法:
    python dtc-lite.py board.dts output_dir [driver_dirs ...]

Output (in output_dir/):
    board_nodes.h       — DEV_ID_xxx 枚举 + chosen/alias 宏
    board_devtable.h    — 设备表访问 API
    board_devtable.c    — 静态 device_t 数组
    board_probe.c       — probe 函数指针表 + 拓扑排序顺序
    board_handles.h     — chosen/alias 注入句柄
"""

import sys
import os
import re
from collections import OrderedDict


# =========================================================================
#  Tokenizer（分词器）
# =========================================================================

TOKEN_EOF      = 0
TOKEN_LBRACE   = 1   # {
TOKEN_RBRACE   = 2   # }
TOKEN_SEMI     = 3   # ;
TOKEN_EQ       = 4   # =
TOKEN_LANGLE   = 5   # <
TOKEN_RANGLE   = 6   # >
TOKEN_SLASH    = 7   # /
TOKEN_AMPERS   = 8   # &
TOKEN_COLON    = 9   # :
TOKEN_COMMA    = 10  # ,
TOKEN_STRING   = 11  # "string"
TOKEN_INT      = 12  # 123
TOKEN_IDENT    = 13  # identifier-with-hy-phens
TOKEN_DTSV1    = 14  # /dts-v1/
TOKEN_POUND    = 15  # # (for #include, etc.)
TOKEN_AT       = 16  # @ (unit-address separator)

token_names = {
    0: "EOF", 1: "{", 2: "}", 3: ";", 4: "=",
    5: "<", 6: ">", 7: "/", 8: "&", 9: ":",
    10: ",", 11: "STRING", 12: "INT", 13: "IDENT",
    14: "/dts-v1/", 15: "#", 16: "@"
}


class Token:
    __slots__ = ('type', 'value', 'line', 'col')
    def __init__(self, type, value=None, line=0, col=0):
        self.type = type
        self.value = value
        self.line = line
        self.col = col

    def __repr__(self):
        if self.value is not None:
            return f"Token({token_names.get(self.type, self.type)}, '{self.value}')"
        return f"Token({token_names.get(self.type, self.type)})"


class Tokenizer:
    """DTS 分词器 — 手写有限状态机。"""

    def __init__(self, text, filename="<dts>"):
        self.text = text
        self.filename = filename
        self.pos = 0
        self.line = 1
        self.col = 1

    def peek(self):
        return self.text[self.pos] if self.pos < len(self.text) else '\0'

    def advance(self):
        ch = self.text[self.pos] if self.pos < len(self.text) else '\0'
        self.pos += 1
        if ch == '\n':
            self.line += 1
            self.col = 1
        else:
            self.col += 1
        return ch

    def skip_ws(self):
        while self.pos < len(self.text):
            ch = self.peek()
            if ch in ' \t\n\r':
                self.advance()
            else:
                break

    def skip_line_comment(self):
        while self.pos < len(self.text):
            ch = self.advance()
            if ch == '\n':
                return

    def skip_block_comment(self):
        while self.pos + 1 < len(self.text):
            if self.peek() == '*' and self.text[self.pos + 1] == '/':
                self.advance()  # *
                self.advance()  # /
                return
            self.advance()
        raise SyntaxError(f"{self.filename}:{self.line}: unterminated block comment")

    def error(self, msg):
        raise SyntaxError(f"{self.filename}:{self.line}:{self.col}: {msg}")

    def tokenize(self):
        tokens = []
        while self.pos < len(self.text):
            ch = self.peek()
            line, col = self.line, self.col

            # 空白
            if ch in ' \t\n\r':
                self.skip_ws()
                continue

            # 注释
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
                # 单独的 / — 可能是 /dts-v1/ 或根节点
                if self.pos + 7 < len(self.text) and self.text[self.pos:self.pos+8] == '/dts-v1/':
                    # /dts-v1/
                    for _ in range(8):
                        self.advance()
                    tokens.append(Token(TOKEN_DTSV1, None, line, col))
                    continue
                tokens.append(Token(TOKEN_SLASH, None, line, col))
                self.advance()
                continue

            # 字符串
            if ch == '"':
                self.advance()
                s = []
                while self.pos < len(self.text):
                    c = self.advance()
                    if c == '"':
                        break
                    if c == '\\':
                        c2 = self.advance()
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

            # 符号单字符
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

            # 数字 (含负数)
            if ch.isdigit() or (ch == '-' and self.pos + 1 < len(self.text)
                                and self.text[self.pos + 1].isdigit()):
                start = self.pos
                if ch == '-':
                    self.advance()
                while self.pos < len(self.text) and self.peek().isdigit():
                    self.advance()
                val = int(self.text[start:self.pos])
                tokens.append(Token(TOKEN_INT, val, line, col))
                continue

            # 标识符 (含连字符, @, .)
            if ch.isalpha() or ch == '_' or ch == '.':
                start = self.pos
                while self.pos < len(self.text):
                    c = self.peek()
                    if c.isalnum() or c in '_-.,/':
                        self.advance()
                    else:
                        break
                ident = self.text[start:self.pos]
                # 检查是否是 /dts-v1/ （从 / 开始的情况已经处理过了）
                if ident == 'dts-v1' and start > 0 and self.text[start-1] == '/':
                    continue  # 已被上面的 /dts-v1 逻辑处理
                tokens.append(Token(TOKEN_IDENT, ident, line, col))
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

    def __init__(self, name, line=0):
        self.name = name
        self.strings = []    # 字符串值
        self.ints = []       # 整数值
        self.phandles = []   # phandle 引用 (label string)
        self.line = line

    def is_empty(self):
        return not self.strings and not self.ints and not self.phandles

    def __repr__(self):
        parts = []
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

    def __init__(self, name, label=None, parent=None, line=0):
        self.name = name
        self.label = label
        self.parent = parent
        self.children = []    # Ordered: 子节点列表
        self.props = []       # Property 列表
        self.line = line

    @property
    def path(self):
        """从根到当前节点的路径"""
        parts = []
        node = self
        while node:
            if node.name:
                parts.insert(0, node.name)
            node = node.parent
        return '/' + '/'.join(parts)

    def find_child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def find_node_by_path(self, path):
        """按路径查找子节点 (相对路径或绝对路径)"""
        if path.startswith('/'):
            # 绝对路径: 从根开始
            parts = [p for p in path.split('/') if p]
            node = self
            while node.parent:
                node = node.parent
            for part in parts:
                node = node.find_child(part)
                if not node:
                    return None
            return node
        else:
            # 相对路径
            parts = path.split('/')
            node = self
            for part in parts:
                node = node.find_child(part)
                if not node:
                    return None
            return node

    def get_prop(self, name):
        for p in self.props:
            if p.name == name:
                return p
        return None

    def collect_all_devices(self):
        """递归收集所有包含 compatible 属性的节点 (候选设备)"""
        devices = []
        if any(p.name == 'compatible' for p in self.props):
            devices.append(self)
        for c in self.children:
            devices.extend(c.collect_all_devices())
        return devices

    def __repr__(self):
        lbl = f"{self.label}: " if self.label else ""
        return f"Node({lbl}{self.name})"


# =========================================================================
#  Parser（解析器）
# =========================================================================

class DtsParser:
    """DTS 递归下降解析器"""

    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self, offset=0):
        idx = self.pos + offset
        return self.tokens[idx] if idx < len(self.tokens) else None

    def advance(self):
        t = self.tokens[self.pos]
        self.pos += 1
        return t

    def expect(self, type, msg=None):
        t = self.advance()
        if t.type != type:
            raise SyntaxError(
                f"line {t.line}: expected {token_names.get(type, type)}"
                f" but got {token_names.get(t.type, t.type)}"
                f" ({msg or ''})"
            )
        return t

    def skip_semi(self):
        """跳过可选的分号 (某些 DTS 风格可省略)"""
        if self.peek() and self.peek().type == TOKEN_SEMI:
            self.advance()

    def parse(self):
        """dts := header root nodes"""
        self.parse_header()
        root = self.parse_node()
        if not root or root.name != '':
            raise SyntaxError("missing root node '/'")
        return root

    def parse_header(self):
        """header := '/dts-v1/' ';'"""
        if self.peek() and self.peek().type == TOKEN_DTSV1:
            self.advance()
            self.skip_semi()
        # 如果没 header 也 OK

    def parse_node(self):
        """node := ['/' | label ':' name] '{' body '}' ';'?"""
        if not self.peek():
            return None

        t = self.peek()
        line = t.line
        label = None
        name = None

        # 根节点: /
        if t.type == TOKEN_SLASH:
            self.advance()
            name = ''
        elif t.type == TOKEN_IDENT:
            # 可能是 label: name 或仅有 name 或仅有 label:
            ident = self.advance()
            if self.peek() and self.peek().type == TOKEN_COLON:
                # label: name {  or label: {
                self.advance()  # :
                label = ident
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    name = self.advance().value
                else:
                    name = label  # label 即名字
            elif self.peek() and self.peek().type == TOKEN_AT:
                # name@addr
                name = ident.value
            else:
                name = ident.value
        else:
            return None

        # 处理 name@addr: name 部分可能包含 @addr
        if self.peek() and self.peek().type == TOKEN_AT:
            self.advance()  # @
            addr = self.advance()
            if addr.type == TOKEN_INT:
                name = f"{name}@{addr.value}"
            elif addr.type == TOKEN_IDENT:
                name = f"{name}@{addr.value}"

        # 期待 {
        if not self.peek() or self.peek().type != TOKEN_LBRACE:
            # 没有 { — 不是节点, 退回
            if label:
                # label: 可能是属性引用
                pass
            return None

        node = DtsNode(name, label=label, parent=None, line=line)
        self.advance()  # {

        # 解析 body (props + children)
        while self.peek() and self.peek().type != TOKEN_RBRACE:
            t = self.peek()

            if t.type == TOKEN_POUND:
                # #address-cells / #size-cells — # 是属性名的一部分
                self.advance()  # #
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    ident = "#" + self.advance().value
                    if self.peek() and self.peek().type == TOKEN_EQ:
                        self.advance()  # =
                        prop = DtsProperty(ident, line=t.line)
                        self.parse_prop_value(prop)
                        node.props.append(prop)
                    else:
                        prop = DtsProperty(ident, line=t.line)
                        prop.strings = ["true"]
                        node.props.append(prop)
                continue

            if t.type == TOKEN_IDENT:
                # 可能是属性或子节点
                ident = t.value
                self.advance()

                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    # 子节点 (无 label)
                    child = DtsNode(ident, parent=node, line=t.line)
                    self.advance()  # {
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(child)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                    node.children.append(child)

                elif self.peek() and self.peek().type == TOKEN_COLON:
                    # label: name { — 子节点带 label
                    self.advance()  # :
                    label_name = ident
                    if self.peek() and self.peek().type == TOKEN_IDENT:
                        child_name = self.advance().value
                    else:
                        child_name = label_name

                    if self.peek() and self.peek().type == TOKEN_AT:
                        self.advance()  # @
                        addr = self.advance()
                        if addr.type == TOKEN_INT:
                            child_name = f"{child_name}@{addr.value}"
                        elif addr.type == TOKEN_IDENT:
                            child_name = f"{child_name}@{addr.value}"

                    if self.peek() and self.peek().type == TOKEN_LBRACE:
                        child = DtsNode(child_name, label=label_name, parent=node, line=t.line)
                        self.advance()  # {
                        while self.peek() and self.peek().type != TOKEN_RBRACE:
                            self.parse_body_item(child)
                        if self.peek() and self.peek().type == TOKEN_RBRACE:
                            self.advance()
                        self.skip_semi()
                        node.children.append(child)
                    else:
                        # label: value; — 属性值引用?
                        # 当作属性处理
                        prop = DtsProperty(label_name, line=t.line)
                        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                            val_token = self.advance()
                            if val_token.type == TOKEN_STRING:
                                prop.strings.append(val_token.value)
                            elif val_token.type == TOKEN_INT:
                                prop.ints.append(val_token.value)
                            elif val_token.type == TOKEN_AMPERS:
                                # phandle: &label
                                ph = self.advance()
                                if ph.type == TOKEN_IDENT:
                                    prop.phandles.append(ph.value)
                            elif val_token.type in (TOKEN_LANGLE, TOKEN_RANGLE):
                                pass  # 忽略界符
                            elif val_token.type == TOKEN_COMMA:
                                pass
                        if self.peek() and self.peek().type == TOKEN_SEMI:
                            self.advance()
                        node.props.append(prop)

                elif self.peek() and self.peek().type == TOKEN_EQ:
                    # 属性 key = value;
                    self.advance()  # =
                    prop = DtsProperty(ident, line=t.line)
                    self.parse_prop_value(prop)
                    node.props.append(prop)

                elif self.peek() and self.peek().type == TOKEN_SEMI:
                    # 空属性 (key; )
                    node.props.append(DtsProperty(ident, line=t.line))
                    self.advance()
                    node.props[-1].strings = ["true"]

                else:
                    # 可能是子节点名后缺少 { ?
                    # 尝试当作属性处理
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
                        elif val_token.type == TOKEN_AT:
                            # 可能是 name@addr 后缺少 {
                            pass
                        elif val_token.type == TOKEN_COLON:
                            pass
                        elif val_token.type in (TOKEN_LANGLE, TOKEN_RANGLE, TOKEN_COMMA):
                            pass
                    if self.peek() and self.peek().type == TOKEN_SEMI:
                        self.advance()
                    node.props.append(prop)

            elif t.type == TOKEN_AMPERS:
                # &label { ... } — 引用标签的节点
                self.advance()
                lbl = self.advance()
                ref_node = DtsNode(f"&{lbl.value}", parent=node, line=t.line)
                if self.peek() and self.peek().type == TOKEN_LBRACE:
                    self.advance()
                    while self.peek() and self.peek().type != TOKEN_RBRACE:
                        self.parse_body_item(ref_node)
                    if self.peek() and self.peek().type == TOKEN_RBRACE:
                        self.advance()
                    self.skip_semi()
                # 不添加 & 节点到 children
                # 这是 overlay 语法, MCU Lite 不支持
                pass

            elif t.type in (TOKEN_SLASH, TOKEN_DTSV1):
                # 已处理
                self.advance()

            else:
                break

        if self.peek() and self.peek().type == TOKEN_RBRACE:
            self.advance()
        self.skip_semi()

        return node

    def parse_body_item(self, parent):
        """解析 body 中的一项 (prop 或 child node)"""
        if not self.peek():
            return

        t = self.peek()

        if t.type == TOKEN_POUND:
            # #address-cells / #size-cells
            self.advance()
            if self.peek() and self.peek().type == TOKEN_IDENT:
                ident = "#" + self.advance().value
                if self.peek() and self.peek().type == TOKEN_EQ:
                    self.advance()
                    prop = DtsProperty(ident, line=t.line)
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
                # 子节点
                child = DtsNode(ident, parent=parent, line=t.line)
                self.advance()
                while self.peek() and self.peek().type != TOKEN_RBRACE:
                    self.parse_body_item(child)
                if self.peek() and self.peek().type == TOKEN_RBRACE:
                    self.advance()
                self.skip_semi()
                parent.children.append(child)

            elif self.peek() and self.peek().type == TOKEN_COLON:
                # label: name { or label: {
                self.advance()
                label_name = ident
                child_name = label_name
                if self.peek() and self.peek().type == TOKEN_IDENT:
                    child_name = self.advance().value
                # @addr
                if self.peek() and self.peek().type == TOKEN_AT:
                    self.advance()
                    addr = self.advance()
                    if addr.type == TOKEN_INT:
                        child_name = f"{child_name}@{addr.value}"
                    elif addr.type == TOKEN_IDENT:
                        child_name = f"{child_name}@{addr.value}"
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
                    # label (no brace) — 可能是个引用标记
                    prop = DtsProperty(label_name, line=t.line)
                    parent.props.append(prop)

            elif self.peek() and self.peek().type == TOKEN_EQ:
                # 属性
                self.advance()  # =
                prop = DtsProperty(ident, line=t.line)
                self.parse_prop_value(prop)
                parent.props.append(prop)

            elif self.peek() and self.peek().type == TOKEN_SEMI:
                # 空属性
                prop = DtsProperty(ident, line=t.line)
                prop.strings = ["true"]
                parent.props.append(prop)
                self.advance()

            elif self.peek() and self.peek().type == TOKEN_AT:
                # name@addr { — 子节点
                self.advance()  # @
                addr = self.advance()
                addr_val = addr.value if addr.type == TOKEN_INT else str(addr.value)
                child_name = f"{ident}@{addr_val}"
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
                        vt = self.advance()
                        if vt.type == TOKEN_INT:
                            prop.ints.append(vt.value)
                    if self.peek() and self.peek().type == TOKEN_SEMI:
                        self.advance()
                    parent.props.append(prop)

            else:
                # 未知 — 尝试属性
                prop = DtsProperty(ident, line=t.line)
                while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
                    vt = self.advance()
                    if vt.type == TOKEN_STRING:
                        prop.strings.append(vt.value)
                    elif vt.type == TOKEN_INT:
                        prop.ints.append(vt.value)
                    elif vt.type == TOKEN_AMPERS:
                        ph = self.advance()
                        if ph.type == TOKEN_IDENT:
                            prop.phandles.append(ph.value)
                    elif vt.type in (TOKEN_LANGLE, TOKEN_RANGLE, TOKEN_COMMA, TOKEN_AT, TOKEN_COLON, TOKEN_SLASH):
                        pass
                if self.peek() and self.peek().type == TOKEN_SEMI:
                    self.advance()
                parent.props.append(prop)

        elif t.type == TOKEN_RBRACE:
            return  # 父级处理

        else:
            self.advance()  # 跳过未知 token

    def parse_prop_value(self, prop):
        """解析属性值 (可能是 strings, <ints>, phandles 的组合)"""
        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
            t = self.peek()
            if t.type == TOKEN_STRING:
                self.advance()
                prop.strings.append(t.value)
            elif t.type == TOKEN_INT:
                self.advance()
                prop.ints.append(t.value)
            elif t.type == TOKEN_LANGLE:
                self.advance()
                # 解析 < ... > 中的内容
                while self.peek() and self.peek().type != TOKEN_RANGLE:
                    inner = self.peek()
                    if inner.type == TOKEN_INT:
                        self.advance()
                        prop.ints.append(inner.value)
                    elif inner.type == TOKEN_AMPERS:
                        self.advance()
                        ph = self.advance()
                        if ph.type == TOKEN_IDENT:
                            prop.phandles.append(ph.value)
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
                # 逗号分隔, 忽略
            else:
                break
        if self.peek() and self.peek().type == TOKEN_SEMI:
            self.advance()

    def parse_value_sequence(self):
        """解析值序列直到 ; 或 }"""
        values = []
        while self.peek() and self.peek().type not in (TOKEN_SEMI, TOKEN_RBRACE, TOKEN_EOF):
            t = self.advance()
            if t.type == TOKEN_STRING:
                values.append(('string', t.value))
            elif t.type == TOKEN_INT:
                values.append(('int', t.value))
            elif t.type == TOKEN_LANGLE:
                while self.peek() and self.peek().type != TOKEN_RANGLE:
                    it = self.advance()
                    if it.type == TOKEN_INT:
                        values.append(('int', it.value))
                    elif it.type == TOKEN_AMPERS:
                        ph = self.advance()
                        if ph.type == TOKEN_IDENT:
                            values.append(('phandle', ph.value))
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

    def __init__(self, dts_path, driver_dirs=None):
        self.dts_path = dts_path
        self.driver_dirs = driver_dirs or []
        self.root = None
        self.label_map = {}     # label → DtsNode
        self.alias_map = {}     # alias_name → DtsNode
        self.chosen_map = {}    # chosen_key → DtsNode
        self.device_list = []   # 所有设备节点 (含 compatible)
        self.driver_map = {}    # compatible → (probe_fn_name, remove_fn_name)

    def compile(self):
        """完整编译流程"""
        # 1. 解析 DTS
        with open(self.dts_path, 'r', encoding='utf-8') as f:
            text = f.read()
        tokenizer = Tokenizer(text, self.dts_path)
        tokens = tokenizer.tokenize()
        parser = DtsParser(tokens)
        self.root = parser.parse()

        # 2. 构建 label 映射
        self._build_label_map(self.root)

        # 3. 解析 aliases 和 chosen
        self._parse_special_nodes()

        # 4. 收集所有设备
        self.device_list = self.root.collect_all_devices()
        self._deduplicate_devices()

        # 5. 扫描驱动注册
        self._scan_drivers()

        # 6. 验证兼容性匹配
        self._validate_compatibles()

        return self

    def _deduplicate_devices(self):
        """去重 (同名节点只保留一个)"""
        seen = set()
        unique = []
        for dev in self.device_list:
            key = dev.path
            if key not in seen:
                seen.add(key)
                unique.append(dev)
        self.device_list = unique

    def _build_label_map(self, node):
        """递归构建 label → node 映射"""
        if node.label:
            self.label_map[node.label] = node
        for c in node.children:
            self._build_label_map(c)

    def _parse_special_nodes(self):
        """解析 aliases 和 chosen 节点"""
        aliases_node = self.root.find_node_by_path('/aliases')
        if aliases_node:
            for prop in aliases_node.props:
                if prop.phandles:
                    label = prop.phandles[0]
                    if label in self.label_map:
                        self.alias_map[prop.name] = self.label_map[label]
                elif prop.strings:
                    # alias 值也可能是一个路径
                    self.alias_map[prop.name] = prop.strings[0]

        chosen_node = self.root.find_node_by_path('/chosen')
        if chosen_node:
            for prop in chosen_node.props:
                if prop.phandles:
                    label = prop.phandles[0]
                    if label in self.label_map:
                        self.chosen_map[prop.name] = self.label_map[label]
                elif prop.strings:
                    # chosen 值也可能是路径
                    node = self.root.find_node_by_path(prop.strings[0])
                    if node:
                        self.chosen_map[prop.name] = node

    def _scan_drivers(self):
        """扫描驱动源文件, 提取 DRIVER_REGISTER 宏"""
        # DRIVER_REGISTER(name, "compatible", probe_fn, remove_fn)
        pattern = re.compile(
            r'DRIVER_REGISTER\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\)'
        )

        for drv_dir in self.driver_dirs:
            if not os.path.isdir(drv_dir):
                continue
            for root_dir, dirs, files in os.walk(drv_dir):
                for f in files:
                    if f.endswith('.c') or f.endswith('.h'):
                        path = os.path.join(root_dir, f)
                        try:
                            with open(path, 'r', encoding='utf-8') as fh:
                                content = fh.read()
                            for m in pattern.finditer(content):
                                name = m.group(1)
                                compat = m.group(2)
                                probe_fn = f"board_driver_probe_{name}"
                                remove_fn = f"board_driver_remove_{name}"
                                self.driver_map[compat] = (probe_fn, remove_fn)
                        except Exception:
                            pass  # 跳过不可读的文件

    def _validate_compatibles(self):
        """验证: 所有设备的 compatible 在 driver_map 中都有对应"""
        PLATFORM = {
            'esp32,cpu',
            'esp32,spi-bus',
            'esp32,i2s-bus',
            'esp32,uart',
            'esp32,gpio',
            'esp32,i2c-bus',
            'esp32,rmt-tx',
            'esp32,adc',
        }
        errors = []
        for dev in self.device_list:
            # 跳过根节点 (/) — 它的 compatible 只是板子标识, 不需要驱动
            if dev.parent is None:
                continue
            compat_prop = dev.get_prop('compatible')
            if compat_prop and compat_prop.strings:
                compat = compat_prop.strings[0]
                if compat in PLATFORM:
                    continue
                if compat not in self.driver_map:
                    # 检查是否有 "status" = "disabled" 或 "reserved"
                    status_prop = dev.get_prop('status')
                    is_disabled = (status_prop and
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
            sys.exit(1)

    def get_device_deps(self, dev):
        """获取设备的依赖 phandle 列表"""
        deps = []
        # 1. 显式 depends-on 或 depends_on
        for pname in ('depends-on', 'depends_on'):
            prop = dev.get_prop(pname)
            if prop:
                deps.extend(prop.phandles)
        # 2. parent 节点 (如 spi 的子节点)
        if dev.parent and dev.parent.name not in ('', 'soc', 'display', 'audio', 'input', 'leds', 'storage'):
            parent_label = dev.parent.label
            if parent_label:
                deps.append(parent_label)
        return deps

    def topological_sort(self):
        """
        对设备列表进行拓扑排序 (Kahn's algorithm)
        返回按依赖顺序排列的设备索引列表
        """
        n = len(self.device_list)
        # 构建设备名 → 索引的映射
        name_to_idx = {}
        label_to_idx = {}
        for i, dev in enumerate(self.device_list):
            name_to_idx[dev.name] = i
            if dev.label:
                label_to_idx[dev.label] = i

        graph = [[] for _ in range(n)]
        in_degree = [0] * n

        for i, dev in enumerate(self.device_list):
            deps = self.get_device_deps(dev)
            dep_indices = set()
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_indices.add(label_to_idx[dep_label])
            for di in dep_indices:
                graph[di].append(i)
                in_degree[i] += 1

        # Kahn
        queue = [i for i in range(n) if in_degree[i] == 0]
        result = []

        while queue:
            node = queue.pop(0)
            result.append(node)
            for neighbor in graph[node]:
                in_degree[neighbor] -= 1
                if in_degree[neighbor] == 0:
                    queue.append(neighbor)

        if len(result) != n:
            # 找循环
            cycle_nodes = [self.device_list[i].path for i in range(n) if in_degree[i] > 0]
            raise RuntimeError(
                f"Circular dependency detected among devices: {cycle_nodes}"
            )

        return result  # 按 probe 顺序排列的设备索引

    def compute_cascade_tables(self):
        """
        计算故障传播表: 对于每个设备, 找出所有传递依赖它的设备.
        返回 cascade_map = { dev_idx: [dependent_idx, ...] }
        运行时 disable_dependents 用此表代替 BFS 收敛.
        """
        n = len(self.device_list)
        # 构建前向依赖图: parent → child (parent 失败 → child 应被禁用)
        label_to_idx = {}
        for i, dev in enumerate(self.device_list):
            if dev.label:
                label_to_idx[dev.label] = i

        graph = [[] for _ in range(n)]
        for i, dev in enumerate(self.device_list):
            deps = self.get_device_deps(dev)
            dep_set = set()
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_set.add(label_to_idx[dep_label])
            for di in dep_set:
                graph[di].append(i)  # di → i: di is dependency, i is dependent

        # 拓扑顺序 (用于下游排序)
        order = self.topological_sort()
        order_idx = {dev: pos for pos, dev in enumerate(order)}

        # 对每个设备 DFS 求传递闭包
        cascade = {}
        for i in range(n):
            visited = set()
            stack = list(graph[i])  # 直接下游
            while stack:
                child = stack.pop()
                if child in visited:
                    continue
                visited.add(child)
                for grandchild in graph[child]:
                    if grandchild not in visited:
                        stack.append(grandchild)
            # 按 probe 顺序排列
            sorted_visited = sorted(visited, key=lambda x: order_idx.get(x, x))
            if sorted_visited:
                cascade[i] = sorted_visited

        return cascade


# =========================================================================
#  C 代码生成器
# =========================================================================

class CGenerator:
    """从编译结果生成 C 代码"""

    def __init__(self, compiler, output_dir):
        self.compiler = compiler
        self.output_dir = output_dir
        os.makedirs(output_dir, exist_ok=True)

    def gen_all(self):
        """生成所有输出文件"""
        self._gen_board_nodes_h()
        self._gen_board_devtable_h()
        self._gen_board_devtable_c()
        self._gen_board_probe_c()
        self._gen_board_handles_h()
        self._gen_dt_config_h()
        self._gen_board_force_link_c()

    def _snake_name(self, name):
        """设备名 → 枚举/变量名 (sanitize)"""
        n = name.replace('@', '_').replace('-', '_').replace('.', '_').replace('/', '_')
        return n.upper()

    def _c_safe_name(self, name):
        """设备名 → C 标识符"""
        n = name.replace('@', '_').replace('-', '_').replace('.', '_').replace('/', '_')
        return n

    def _generate_includes(self):
        lines = [
            '#include <stdint.h>',
            '#include <stddef.h>',
            '#include <stdbool.h>',
            '#include <string.h>',
        ]
        return '\n'.join(lines)

    def _gen_board_nodes_h(self):
        """生成 board_nodes.h — DEV_ID 枚举 + chosen/alias 宏"""
        devs = self.compiler.device_list
        path = os.path.join(self.output_dir, 'board_nodes.h')

        lines = [
            '#ifndef BOARD_NODES_H',
            '#define BOARD_NODES_H',
            '',
            '#include <stdint.h>',
            '',
            '/* ===== 设备 ID 枚举 (自动生成) ===== */',
            'typedef enum {',
        ]

        for i, dev in enumerate(devs):
            name = self._snake_name(dev.name)
            lines.append(f'    DEV_ID_{name} = {i},')

        lines += [
            f'    DEV_ID_COUNT = {len(devs)}',
            '} device_id_t;',
            '',
        ]

        # chosen 宏
        if self.compiler.chosen_map:
            lines.append('/* ===== chosen 设备 ===== */')
            for key, node in self.compiler.chosen_map.items():
                cname = self._snake_name(key)
                dname = self._snake_name(node.name)
                lines.append(f'#define CHOSEN_{cname}    DEV_ID_{dname}')
            lines.append('')

        # alias 宏
        if self.compiler.alias_map:
            lines.append('/* ===== alias 宏 ===== */')
            for key, node in self.compiler.alias_map.items():
                cname = self._snake_name(key)
                dname = self._snake_name(node.name)
                lines.append(f'#define ALIAS_{cname}      DEV_ID_{dname}')
            lines.append('')

        lines.append('#endif /* BOARD_NODES_H */')
        lines.append('')

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_board_force_link_c(self):
        """生成 board_force_link.c — 强制链接器保留所有 driver_map 中的 probe 函数
        由 dtc-lite 扫描 DRIVER_REGISTER 宏自动生成, 消除手动维护.
        """
        path = os.path.join(self.output_dir, 'board_force_link.c')

        driver_map = self.compiler.driver_map
        if not driver_map:
            with open(path, 'w', encoding='utf-8') as f:
                f.write('/* no drivers registered — nothing to force-link */\n')
            print(f"  [gen] {path} (empty)")
            return

        externs = []
        refs = []
        for compat, (probe_fn, _) in sorted(driver_map.items()):
            externs.append(f'extern int {probe_fn}(device_t*);')
            refs.append(f'    s_fake_ref = (void*){probe_fn};')

        lines = [
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_board_devtable_h(self):
        """生成 board_devtable.h — 设备表访问 API"""
        path = os.path.join(self.output_dir, 'board_devtable.h')

        lines = [
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_board_devtable_c(self):
        """生成 board_devtable.c — 静态 device_node_t 表"""
        devs = self.compiler.device_list
        path = os.path.join(self.output_dir, 'board_devtable.c')

        # 为每个设备生成属性数组
        prop_arrays = []
        dep_arrays = []

        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)
            # 属性
            prop_list = [p for p in dev.props if p.name not in
                         ('compatible', 'depends-on', 'depends_on', 'status', 'criticality')]
            if prop_list:
                prop_arrays.append(f'/* {dev.path} */')
                prop_arrays.append(f'static const device_prop_t DEV_{safe}_props[] = {{')
                for p in prop_list:
                    val = ""
                    if p.ints:
                        val = str(p.ints[0])
                    elif p.strings:
                        val = p.strings[0]
                    elif p.phandles:
                        val = p.phandles[0]
                    # 用字符串存所有值
                    prop_arrays.append(f'    {{"{p.name}", "{val}"}},')
                prop_arrays.append('};')
                prop_arrays.append('')

        # 依赖数组 (按 label 索引)
        label_to_idx = {}
        for i, dev in enumerate(devs):
            if dev.label:
                label_to_idx[dev.label] = i

        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)
            deps = self.compiler.get_device_deps(dev)
            dep_ids = []
            for dep_label in deps:
                if dep_label in label_to_idx:
                    dep_ids.append(label_to_idx[dep_label])
            if dep_ids:
                dep_arrays.append(f'static const device_id_t DEV_{safe}_deps[] = {{')
                dep_arrays.append(f'    {", ".join(f"DEV_ID_{self._snake_name(devs[di].name)}" for di in dep_ids)},')
                dep_arrays.append('};')
                dep_arrays.append('')

        # 节点表
        node_entries = []
        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)

            # compatible
            compat_prop = dev.get_prop('compatible')
            compat_str = compat_prop.strings[0] if compat_prop and compat_prop.strings else ""

            # status
            status_prop = dev.get_prop('status')
            if status_prop and status_prop.strings and status_prop.strings[0] == 'disabled':
                status_val = 'DEVICE_STATUS_DISABLED'
            else:
                status_val = 'DEVICE_STATUS_READY'

            # criticality
            crit_prop = dev.get_prop('criticality')
            if crit_prop and crit_prop.strings:
                crit_map = {'fatal': 'DEVICE_CRIT_FATAL', 'warning': 'DEVICE_CRIT_WARNING', 'ignore': 'DEVICE_CRIT_IGNORE'}
                crit_val = crit_map.get(crit_prop.strings[0].lower(), 'DEVICE_CRIT_WARNING')
            else:
                crit_val = 'DEVICE_CRIT_WARNING'

            EXCLUDED_PROPS = ('compatible', 'depends-on', 'depends_on', 'status', 'criticality')

            prop_ref = f'DEV_{safe}_props' if any(
                p.name not in EXCLUDED_PROPS
                for p in dev.props
            ) else 'NULL'

            dep_ref = f'DEV_{safe}_deps' if any(
                dep in label_to_idx for dep in self.compiler.get_device_deps(dev)
            ) else 'NULL'

            dep_count = sum(1 for dep in self.compiler.get_device_deps(dev)
                          if dep in label_to_idx)
            label_val = dev.label or ""

            EXCLUDED_PROPS = ('compatible', 'depends-on', 'depends_on', 'status', 'criticality')

            node_entries.append(
                f'    [DEV_ID_{self._snake_name(dev.name)}] = {{\n'
                f'        .name       = "{dev.name}",\n'
                f'        .label      = "{label_val}",\n'
                f'        .compatible = "{compat_str}",\n'
                f'        .path       = "{dev.path}",\n'
                f'        .status     = {status_val},\n'
                f'        .criticality = {crit_val},\n'
                f'        .prop_count = {len([p for p in dev.props if p.name not in EXCLUDED_PROPS])},\n'
                f'        .props      = {prop_ref},\n'
                f'        .dep_count  = {dep_count},\n'
                f'        .deps       = (const device_id_t*){dep_ref},\n'
                f'    }},'
            )

        lines = [
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_board_probe_c(self):
        """生成 board_probe.c — probe/remove 表 + 拓扑排序顺序"""
        devs = self.compiler.device_list
        order = self.compiler.topological_sort()
        path = os.path.join(self.output_dir, 'board_probe.c')

        PLATFORM = {
            'esp32,cpu',
            'esp32,spi-bus',
            'esp32,i2s-bus',
            'esp32,uart',
            'esp32,gpio',
            'esp32,i2c-bus',
            'esp32,rmt-tx',
            'esp32,adc',
        }

        has_platform = False

        # 收集驱动 probe + remove 函数的外部声明
        probe_externs = []
        remove_externs = []
        probe_array = []
        remove_array = []
        for i in devs:
            compat_prop = i.get_prop('compatible')
            snake = self._snake_name(i.name)
            if compat_prop and compat_prop.strings:
                compat = compat_prop.strings[0]
                if compat in self.compiler.driver_map:
                    p_fn, r_fn = self.compiler.driver_map[compat]
                    probe_externs.append(f'extern int {p_fn}(device_t* dev);')
                    remove_externs.append(f'extern int {r_fn}(device_t* dev);')
                    probe_array.append(
                        f'    [DEV_ID_{snake}] = {p_fn},'
                    )
                    remove_array.append(
                        f'    [DEV_ID_{snake}] = {r_fn},'
                    )
                elif compat in PLATFORM:
                    has_platform = True
                    probe_array.append(
                        f'    [DEV_ID_{snake}] = board_platform_probe,'
                    )
                    remove_array.append(f'    [DEV_ID_{snake}] = NULL,')
                else:
                    probe_array.append(f'    [DEV_ID_{snake}] = NULL,')
                    remove_array.append(f'    [DEV_ID_{snake}] = NULL,')
            else:
                probe_array.append(f'    [DEV_ID_{snake}] = NULL,')
                remove_array.append(f'    [DEV_ID_{snake}] = NULL,')

        # 排序后的顺序
        order_entries = []
        for idx in order:
            dev = devs[idx]
            order_entries.append(f'    DEV_ID_{self._snake_name(dev.name)},')

        lines = [
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
            '/* ===== probe 函数表 (按 DEV_ID 索引) ===== */',
            f'static probe_fn_t s_probe_fns[DEV_ID_COUNT] = {{',
        ] + probe_array + [
            '};',
            '',
            '/* ===== remove 函数表 (按 DEV_ID 索引) ===== */',
            f'static remove_fn_t s_remove_fns[DEV_ID_COUNT] = {{',
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

        # 编译期计算故障传播
        cascade = self.compiler.compute_cascade_tables()
        dev_count = len(devs)

        # 扁平数据 + 计数 + 偏移
        flat_data = []
        counts = [0] * dev_count
        for i in range(dev_count):
            if i in cascade:
                counts[i] = len(cascade[i])
                flat_data.extend(cascade[i])
            else:
                counts[i] = 0

        offsets = [0] * dev_count
        cumulative = 0
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
                        dep_dev = devs[dep_idx]
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_board_handles_h(self):
        """生成 board_handles.h — chosen/alias 注入宏"""
        path = os.path.join(self.output_dir, 'board_handles.h')

        lines = [
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
                dname = self._snake_name(node.name)
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")

    def _gen_dt_config_h(self):
        """生成 dt_config_gen.h — 从 DTS 设备计数自动生成 Pool Size 宏
        IEC 61508 §7.4.2.4: 禁止拍脑袋硬编码资源上限.
        所有静态池大小由编译期设备树唯一确定.
        """
        devs = self.compiler.device_list
        path = os.path.join(self.output_dir, 'dt_config_gen.h')

        compat_counts = {}
        for dev in devs:
            compat_prop = dev.get_prop('compatible')
            if compat_prop and compat_prop.strings:
                compat = compat_prop.strings[0]
                compat_counts[compat] = compat_counts.get(compat, 0) + 1

        def _compat_to_macro(compat):
            return compat.replace(',', '_').replace('-', '_').replace('.', '_').upper()

        lines = [
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

        # ── 设备计数 (per compatible) ──
        for compat in sorted(compat_counts.keys()):
            macro = _compat_to_macro(compat)
            lines.append(f'#define DTC_GEN_COUNT_{macro}  {compat_counts[compat]}')

        # ── 时钟频率: 从 /cpus/cpu@0/clock-frequency 提取 ──
        cpu_node = None
        for dev in devs:
            if dev.path in ('/cpus/cpu@0', '/cpus/cpu@0/'):
                cpu_node = dev
                break
        if cpu_node:
            cf_prop = cpu_node.get_prop('clock-frequency')
            if cf_prop and cf_prop.ints:
                lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  {cf_prop.ints[0]}')
            else:
                lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  16000000')
        else:
            lines.append(f'#define DTC_GEN_CPU_CLOCK_HZ  16000000')

        # ── Tick 频率: 从 /chosen/tick-rate 提取 ──
        chosen_node = self.compiler.root.find_node_by_path('/chosen') if self.compiler.root else None
        if chosen_node:
            tr_prop = chosen_node.get_prop('tick-rate')
            if tr_prop and tr_prop.ints:
                lines.append(f'#define DTC_GEN_TICK_RATE_HZ  {tr_prop.ints[0]}')
            else:
                lines.append(f'#define DTC_GEN_TICK_RATE_HZ  1000')
        else:
            lines.append(f'#define DTC_GEN_TICK_RATE_HZ  1000')

        # ── 堆大小: 从 /chosen/heap-size 提取 (用于 FreeRTOS configTOTAL_HEAP_SIZE) ──
        if chosen_node:
            hs_prop = chosen_node.get_prop('heap-size')
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

        with open(path, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines))
        print(f"  [gen] {path}")


# =========================================================================
#  主函数
# =========================================================================""

def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    dts_path = sys.argv[1]
    output_dir = sys.argv[2]
    driver_dirs = sys.argv[3:] if len(sys.argv) > 3 else []

    if not os.path.isfile(dts_path):
        print(f"ERROR: DTS file not found: {dts_path}", file=sys.stderr)
        sys.exit(1)

    print(f"dtc-lite: {dts_path}")
    print(f"  output: {output_dir}")
    if driver_dirs:
        print(f"  driver scan: {', '.join(driver_dirs)}")

    compiler = DTSCompiler(dts_path, driver_dirs)
    compiler.compile()

    print(f"  devices: {len(compiler.device_list)}")
    for dev in compiler.device_list:
        compat_prop = dev.get_prop('compatible')
        compat = compat_prop.strings[0] if compat_prop and compat_prop.strings else "(no compatible)"
        deps = compiler.get_device_deps(dev)
        dep_labels = ', '.join(deps) if deps else '(none)'
        print(f"    {dev.path:40s} compat={compat:25s} deps=[{dep_labels}]")

    print(f"  drivers matched: {len(compiler.driver_map)}")
    for compat, fn in sorted(compiler.driver_map.items()):
        print(f"    {compat:40s} → {fn}")

    generator = CGenerator(compiler, output_dir)
    generator.gen_all()

    print("dtc-lite: done")


if __name__ == '__main__':
    main()
