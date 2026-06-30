"""Lark 文法定义 — DTS (Device Tree Source) 子集.

兼容 PLY 版 dtc-lite 已支持的全部语法:
  * ``/dts-v1/;`` 头
  * ``/ { }`` 根节点
  * ``&label { }`` overlay
  * ``label: name@addr { }`` 带 label/地址的子节点
  * ``prop = "str";`` / ``prop = <0x1 2>;`` / ``prop = <&label>;`` / ``prop = &label;``
  * ``prop;`` bool 属性 (含 ``#prop;`` 带井号)
  * ``/delete-node/ target;`` / ``/delete-property/ name;``
"""

from __future__ import annotations

# 文法用 Earley 算法解析 (允许少量歧义, lookahead 区分 prop vs child_node).
# 关键: IDENT 不含 '/', 路径用 SLASH 显式分隔 (与 PLY 行为一致).

DTS_GRAMMAR = r"""
start: top_item*

?top_item: header
         | root_node
         | overlay
         | delete_node_stmt
         | delete_prop_stmt
         | SEMI

header: DTSV1 SEMI?

root_node: SLASH LBRACE body_item* RBRACE SEMI?              -> plain_root
         | SLASH IDENT LBRACE body_item* RBRACE SEMI?        -> named_root

overlay: AMPERS IDENT LBRACE body_item* RBRACE SEMI?

?body_item: prop
          | child_node
          | delete_node_stmt
          | delete_prop_stmt

?child_node: IDENT COLON IDENT (AT addr)? LBRACE body_item* RBRACE SEMI?  -> labeled_child
           | IDENT (AT addr)? LBRACE body_item* RBRACE SEMI?              -> simple_child

?prop: POUND IDENT prop_value? SEMI  -> hash_prop
     | IDENT prop_value? SEMI         -> ident_prop

prop_value: EQ value (COMMA value)*

?value: STRING             -> str_val
      | LANGLE angle_item* RANGLE  -> list_val
      | AMPERS IDENT        -> phandle_val
      | INT                 -> int_val

?angle_item: INT           -> ai_int
           | AMPERS IDENT   -> ai_phandle
           | IDENT          -> ai_ident

delete_node_stmt: DELETE_NODE delete_target SEMI?
delete_prop_stmt: DELETE_PROP IDENT SEMI?

?delete_target: AMPERS IDENT                       -> del_tgt_label
               | IDENT (AT addr)?                    -> del_tgt_name
               | SLASH IDENT (SLASH IDENT)*          -> del_tgt_path

?addr: INT   -> addr_int
      | IDENT -> addr_ident

DTSV1: "/dts-v1/"
DELETE_NODE: "/delete-node/"
DELETE_PROP: "/delete-property/"
SLASH: "/"
LBRACE: "{"
RBRACE: "}"
SEMI: ";"
EQ: "="
LANGLE: "<"
RANGLE: ">"
AMPERS: "&"
COLON: ":"
COMMA: ","
POUND: "#"
AT: "@"

STRING: /"(?:[^"\\]|\\.)*"/
INT: /-?(?:0[xX][0-9a-fA-F]+|\d+)/
IDENT: /[A-Za-z_][A-Za-z0-9_\-.,]*/

COMMENT_LINE: "//[^\n]*"
COMMENT_BLOCK: /\/\*[\s\S]*?\*\//

%import common.WS
%ignore WS
%ignore COMMENT_LINE
%ignore COMMENT_BLOCK
"""
