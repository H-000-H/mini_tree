"""PLY 词法分析器 — 将 DTS 源文本切分为 token 流."""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from typing import Any, List

_TOOLS_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_VENDOR_DIR = os.path.join(_TOOLS_DIR, 'vendor')
if _VENDOR_DIR not in sys.path:
    sys.path.insert(0, _VENDOR_DIR)

import ply.lex as lex  # noqa: E402


@dataclass(frozen=True)
class Token:
    type: str
    value: Any = None
    raw: str = ''
    line: int = 0
    col: int = 0


class DtsLexer:
    """PLY lexer for MCU Lite DTS."""

    tokens = (
        'LBRACE', 'RBRACE', 'SEMI', 'EQ', 'LANGLE', 'RANGLE',
        'SLASH', 'AMPERS', 'COLON', 'COMMA', 'STRING', 'INT', 'IDENT',
        'DTSV1', 'POUND', 'AT', 'DELETE_NODE', 'DELETE_PROP',
    )

    t_LBRACE = r'\{'
    t_RBRACE = r'\}'
    t_SEMI = r';'
    t_EQ = r'='
    t_LANGLE = r'<'
    t_RANGLE = r'>'
    t_AMPERS = r'&'
    t_COLON = r':'
    t_COMMA = r','
    t_POUND = r'\#'
    t_AT = r'@'
    t_ignore = ' \t\r'

    def __init__(self, filename: str = '<dts>') -> None:
        self.filename = filename
        self.lexer = lex.lex(module=self, debug=False)

    def t_COMMENT_LINE(self, t):
        r'//[^\n]*'
        pass

    def t_COMMENT_BLOCK(self, t):
        r'/\*([^*]|\*+[^*/])*\*+/'
        pass

    def t_SLASH(self, t):
        r'/'
        pos = self.lexer.lexpos
        data = self.lexer.lexdata
        if data.startswith('/dts-v1/', pos - 1):
            t.type = 'DTSV1'
            t.value = None
            self.lexer.lexpos = pos - 1 + 8
            return t
        if data.startswith('/delete-node/', pos - 1):
            t.type = 'DELETE_NODE'
            t.value = None
            self.lexer.lexpos = pos - 1 + 13
            return t
        if data.startswith('/delete-property/', pos - 1):
            t.type = 'DELETE_PROP'
            t.value = None
            self.lexer.lexpos = pos - 1 + 17
            return t
        return t

    def t_STRING(self, t):
        r'"([^"\\]|\\.)*"'
        t.value = self._decode_string(t.value[1:-1])
        return t

    def t_INT(self, t):
        r'-?(0[xX][0-9a-fA-F]+|\d+)'
        raw = t.value
        t.value = int(raw, 0)
        t.raw = raw
        return t

    def t_IDENT(self, t):
        r'[A-Za-z_][A-Za-z0-9_\-.,/]*'
        return t

    def t_newline(self, t):
        r'\n+'
        pass

    def t_error(self, t):
        if t.value[0] in '()|!~^':
            t.lexer.skip(1)
            return None
        raise SyntaxError(
            f'{self.filename}:{t.lineno}: unexpected character {t.value[0]!r}'
        )

    @staticmethod
    def _decode_string(text: str) -> str:
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

    def tokenize(self, text: str) -> List[Token]:
        self.lexer.input(text)
        tokens: List[Token] = []
        while True:
            ply_t = self.lexer.token()
            if not ply_t:
                break
            tokens.append(Token(
                type=ply_t.type,
                value=getattr(ply_t, 'value', None),
                raw=getattr(ply_t, 'raw', ''),
                line=ply_t.lineno,
                col=1,
            ))
        tokens.append(Token(type='EOF', line=tokens[-1].line if tokens else 1, col=1))
        return tokens


def tokenize(text: str, filename: str = '<dts>') -> List[Token]:
    return DtsLexer(filename).tokenize(text)
