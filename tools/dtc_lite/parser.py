"""PLY 词法 + 递归下降语法分析 — 构建 DtsNode AST."""

from __future__ import annotations

from typing import Any, List, Optional, Tuple

from .ast import DtsNode, DtsProperty
from .lexer import Token, tokenize


class DtsParser:
    """DTS 递归下降解析器 (消费 PLY lexer 输出的 token 流)."""

    def __init__(self, tokens: List[Token], filename: str = '<dts>') -> None:
        self.tokens = tokens
        self.filename = filename
        self.pos = 0

    def peek(self, offset: int = 0) -> Optional[Token]:
        idx = self.pos + offset
        return self.tokens[idx] if idx < len(self.tokens) else None

    def advance(self) -> Token:
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def expect(self, kind: str, msg: str = '') -> Token:
        tok = self.advance()
        if tok.type != kind:
            raise SyntaxError(
                f'{self.filename}:{tok.line}: expected {kind} '
                f'but got {tok.type} ({msg})'
            )
        return tok

    def skip_semi(self) -> None:
        if self.peek() and self.peek().type == 'SEMI':
            self.advance()

    def parse(self) -> DtsNode:
        self._parse_header()
        root = DtsNode('', line=0)
        has_root = False

        while self.peek() and self.peek().type != 'EOF':
            tok = self.peek()
            if tok.type == 'SLASH':
                self.advance()
                if self.peek() and self.peek().type == 'LBRACE':
                    has_root = True
                    extra = self._parse_root_body_only(root_for_delete=root)
                    if extra:
                        self._merge_node_into(root, extra)
                continue

            if tok.type == 'AMPERS':
                self._append_overlay_ref(root, tok.line)
                continue

            if tok.type == 'DELETE_NODE':
                self.advance()
                self._delete_node_from(root)
                self.skip_semi()
                continue

            if tok.type == 'DELETE_PROP':
                self.advance()
                self._delete_prop_from(root)
                self.skip_semi()
                continue

            if tok.type == 'DTSV1':
                self.advance()
                self.skip_semi()
                continue

            if tok.type == 'SEMI':
                self.advance()
                continue

            self.advance()

        if not has_root:
            raise SyntaxError(f'{self.filename}: missing root node "/"')
        return root

    def _append_overlay_ref(self, parent: DtsNode, line: int) -> None:
        self.advance()
        lbl = self.advance()
        if lbl.type != 'IDENT':
            return
        if not self.peek() or self.peek().type != 'LBRACE':
            return
        self.advance()
        ref_node = DtsNode(f'&{lbl.value}', parent=parent, line=line)
        while self.peek() and self.peek().type != 'RBRACE':
            self._parse_body_item(ref_node)
        if self.peek() and self.peek().type == 'RBRACE':
            self.advance()
        self.skip_semi()
        parent.children.append(ref_node)

    def _parse_root_body_only(self, root_for_delete: Optional[DtsNode] = None) -> Optional[DtsNode]:
        if not self.peek() or self.peek().type != 'LBRACE':
            return None
        self.advance()
        tmp = DtsNode('_root_extra', line=0)
        while self.peek() and self.peek().type != 'RBRACE':
            if root_for_delete and self.peek().type in ('DELETE_NODE', 'DELETE_PROP'):
                kind = self.advance().type
                if kind == 'DELETE_NODE':
                    self._delete_node_from(root_for_delete)
                else:
                    self._delete_prop_from(root_for_delete)
                self.skip_semi()
            else:
                self._parse_body_item(tmp)
        if self.peek() and self.peek().type == 'RBRACE':
            self.advance()
        self.skip_semi()
        return tmp

    def _merge_node_into(self, target: DtsNode, src: DtsNode) -> None:
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
                self._merge_node_into(existing_child, child)
            else:
                child.parent = target
                target.children.append(child)

    def _parse_header(self) -> None:
        if self.peek() and self.peek().type == 'DTSV1':
            self.advance()
            self.skip_semi()

    def _parse_body_item(self, parent: DtsNode) -> None:
        if not self.peek():
            return
        tok = self.peek()

        if tok.type == 'POUND':
            self.advance()
            if self.peek() and self.peek().type == 'IDENT':
                ident = '#' + self.advance().value
                if self.peek() and self.peek().type == 'EQ':
                    self.advance()
                    prop = DtsProperty(ident, line=tok.line)
                    self._parse_prop_value(prop)
                    parent.props.append(prop)
                else:
                    prop = DtsProperty(ident, line=tok.line)
                    prop.strings = ['true']
                    parent.props.append(prop)
            return

        if tok.type == 'IDENT':
            ident = tok.value
            self.advance()

            if self.peek() and self.peek().type == 'LBRACE':
                child = DtsNode(ident, parent=parent, line=tok.line)
                self.advance()
                while self.peek() and self.peek().type != 'RBRACE':
                    self._parse_body_item(child)
                if self.peek() and self.peek().type == 'RBRACE':
                    self.advance()
                self.skip_semi()
                parent.children.append(child)

            elif self.peek() and self.peek().type == 'COLON':
                self.advance()
                label_name = ident
                child_name = label_name
                if self.peek() and self.peek().type == 'IDENT':
                    child_name = self.advance().value
                addr_str = self._parse_optional_addr()
                if addr_str:
                    child_name = f'{child_name}@{addr_str}'
                if self.peek() and self.peek().type == 'LBRACE':
                    child = DtsNode(child_name, label=label_name, parent=parent, line=tok.line)
                    self.advance()
                    while self.peek() and self.peek().type != 'RBRACE':
                        self._parse_body_item(child)
                    if self.peek() and self.peek().type == 'RBRACE':
                        self.advance()
                    self.skip_semi()
                    parent.children.append(child)
                else:
                    parent.props.append(DtsProperty(label_name, line=tok.line))

            elif self.peek() and self.peek().type == 'EQ':
                self.advance()
                prop = DtsProperty(ident, line=tok.line)
                self._parse_prop_value(prop)
                parent.props.append(prop)

            elif self.peek() and self.peek().type == 'SEMI':
                prop = DtsProperty(ident, line=tok.line)
                prop.strings = ['true']
                parent.props.append(prop)
                self.advance()

            elif self.peek() and self.peek().type == 'AT':
                self.advance()
                addr_str = self._read_addr_token()
                child_name = f'{ident}@{addr_str}'
                if self.peek() and self.peek().type == 'LBRACE':
                    child = DtsNode(child_name, parent=parent, line=tok.line)
                    self.advance()
                    while self.peek() and self.peek().type != 'RBRACE':
                        self._parse_body_item(child)
                    if self.peek() and self.peek().type == 'RBRACE':
                        self.advance()
                    self.skip_semi()
                    parent.children.append(child)
                else:
                    prop = DtsProperty(child_name, line=tok.line)
                    while self.peek() and self.peek().type not in ('SEMI', 'RBRACE', 'EOF'):
                        vt = self.advance()
                        if vt.type == 'INT':
                            prop.ints.append(vt.value)
                    if self.peek() and self.peek().type == 'SEMI':
                        self.advance()
                    parent.props.append(prop)

            else:
                prop = DtsProperty(ident, line=tok.line)
                while self.peek() and self.peek().type not in ('SEMI', 'RBRACE', 'EOF'):
                    vt = self.advance()
                    if vt.type == 'STRING':
                        prop.strings.append(vt.value)
                    elif vt.type == 'INT':
                        prop.ints.append(vt.value)
                    elif vt.type == 'AMPERS':
                        ph = self.advance()
                        if ph.type == 'IDENT':
                            prop.phandles.append(ph.value)
                if self.peek() and self.peek().type == 'SEMI':
                    self.advance()
                parent.props.append(prop)

        elif tok.type == 'DELETE_NODE':
            self.advance()
            self._delete_node_from(parent)
            self.skip_semi()

        elif tok.type == 'DELETE_PROP':
            self.advance()
            self._delete_prop_from(parent)
            self.skip_semi()

        elif tok.type == 'RBRACE':
            return

        else:
            self.advance()

    def _parse_optional_addr(self) -> str:
        if self.peek() and self.peek().type == 'AT':
            self.advance()
            return self._read_addr_token()
        return ''

    def _read_addr_token(self) -> str:
        addr_tok = self.advance()
        if addr_tok.type == 'INT':
            return addr_tok.raw.lower() if addr_tok.raw else str(addr_tok.value)
        if addr_tok.type == 'IDENT':
            return addr_tok.value
        return ''

    def _delete_node_from(self, parent: DtsNode) -> None:
        tok = self.peek()
        if not tok:
            return

        if tok.type == 'AMPERS':
            self.advance()
            lbl = self.advance() if self.peek() else None
            if lbl and lbl.type == 'IDENT':
                parent.children = [c for c in parent.children if c.label != lbl.value]

        elif tok.type == 'IDENT':
            name = tok.value
            self.advance()
            addr_str = self._parse_optional_addr()
            if addr_str:
                name = f'{name}@{addr_str}'
            parent.children = [c for c in parent.children if c.name != name]

        elif tok.type == 'SLASH':
            parts: List[str] = []
            self.advance()
            while self.peek() and self.peek().type not in ('SEMI', 'EOF'):
                if self.peek().type == 'IDENT':
                    parts.append(self.advance().value)
                elif self.peek().type == 'SLASH':
                    self.advance()
                elif self.peek().type == 'AT':
                    self.advance()
                    nxt = self.advance() if self.peek() else None
                    if nxt and parts:
                        if nxt.type == 'INT' and nxt.raw:
                            parts[-1] = f'{parts[-1]}@{nxt.raw.lower()}'
                        else:
                            parts[-1] = f'{parts[-1]}@{nxt.value}'
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
        if self.peek() and self.peek().type == 'IDENT':
            name = self.advance().value
            parent.props = [p for p in parent.props if p.name != name]

    def _parse_prop_value(self, prop: DtsProperty) -> None:
        while self.peek() and self.peek().type not in ('SEMI', 'RBRACE', 'EOF'):
            tok = self.peek()
            if tok.type == 'STRING':
                self.advance()
                prop.strings.append(tok.value)
            elif tok.type == 'INT':
                self.advance()
                prop.ints.append(tok.value)
            elif tok.type == 'LANGLE':
                self.advance()
                while self.peek() and self.peek().type != 'RANGLE':
                    inner = self.peek()
                    if inner.type == 'INT':
                        self.advance()
                        prop.ints.append(inner.value)
                    elif inner.type == 'AMPERS':
                        self.advance()
                        ph = self.advance()
                        if ph.type == 'IDENT':
                            prop.phandles.append(ph.value)
                    elif inner.type == 'IDENT':
                        self.advance()
                    else:
                        break
                if self.peek() and self.peek().type == 'RANGLE':
                    self.advance()
            elif tok.type == 'AMPERS':
                self.advance()
                ph = self.advance()
                if ph.type == 'IDENT':
                    prop.phandles.append(ph.value)
            elif tok.type == 'COMMA':
                self.advance()
            else:
                break
        if self.peek() and self.peek().type == 'SEMI':
            self.advance()


def parse_dts(text: str, filename: str = '<dts>') -> DtsNode:
    tokens = tokenize(text, filename)
    return DtsParser(tokens, filename).parse()
