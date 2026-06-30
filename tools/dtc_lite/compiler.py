"""DTS 编译器: 预处理、lark 解析、语义合并与驱动校验."""

from __future__ import annotations

import os
import re
import subprocess
import sys
from typing import Any, Dict, List, Optional, Set, Tuple

from .dts_ast import DtsNode, DtsProperty
from .parser import parse_dts
from .platform import is_platform_node

class DTSCompiler:
    """DTS 编译器: 解析 → 解析 → 生成"""

    def __init__(self, dts_path: str, driver_dirs: Optional[List[str]] = None,
                 extra_inc_dirs: Optional[List[str]] = None,
                 extra_defines: Optional[List[str]] = None) -> None:
        self.dts_path: str = dts_path
        self.board_dir: str = os.path.dirname(os.path.dirname(os.path.abspath(dts_path)))
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
        # 通用 C 头支持: 用户通过命令行 -I / -D 传入任意厂商头搜索路径与预定义宏,
        # dtc-lite 见到 #include <xxx.h> 不在 dt-bindings/ 下时, 就用 cpp 跑该头
        # 提取全部 #define (含链式展开与常量求值), 不再为每个厂商写发现逻辑.
        self._extra_inc_dirs: List[str] = extra_inc_dirs or []
        self._extra_defines: List[str] = extra_defines or []
        # 已用 cpp 提取过宏的头文件集合 (避免对同一头文件重复跑 cpp)
        self._cpp_headers_loaded: Set[str] = set()

    def _preprocess(self, text: str) -> str:
        base_dir: str = os.path.dirname(os.path.abspath(self.dts_path))
        result: List[str] = self._preprocess_lines(text, base_dir)
        return '\n'.join(result)

    # ───────────────────────── 通用 C 头支持 ─────────────────────────

    def _is_extra_header(self, path: str) -> bool:
        """判断一个绝对路径是否落在用户 -I 传入的搜索路径内。

        命中即用 cpp 跑该头提取宏; 否则按 dt-bindings / 相对路径走 Python 预处理。
        """
        if not path:
            return False
        norm: str = os.path.realpath(path)
        for d in self._extra_inc_dirs:
            try:
                if os.path.commonpath([norm, os.path.realpath(d)]) == os.path.realpath(d):
                    return True
            except ValueError:
                continue
        return False

    def _extract_header_macros(self, header_path: str) -> bool:
        """用系统的 C 预处理器 (优先 arm-none-eabi-gcc, 回退 gcc/cpp) 跑该头
        (及其传递依赖), 同时提取:

          1. 所有 ``#define`` 宏  — 用 ``cpp -E -P -dM``
          2. 所有 ``enum`` 块里的常量 — 用 ``cpp -E -P`` 得到展开后文本再正则解析

        提取出的宏以原始宏体文本形式存入 ``self._macros``，后续由
        ``_replace_macros`` 链式解析、由 ``_eval_c_constant`` 计算常量表达式。

        返回 True 表示成功；False 表示 cpp 不可用或失败 (调用方应回退到旧路径)。

        这是通用机制: 只要用户在命令行传 ``-I<dir> -D<macro>`` 就能让任意厂商/
        SDK 头 (STM32 LL / ESP-IDF / WCH Standard Peripheral / FreeRTOS ...)
        里的 #define 与 enum 直接被 dtsi 引用, 无需为每个厂商在 py 里写发现逻辑。
        """
        if header_path in self._cpp_headers_loaded:
            return True
        cpp: Optional[str] = self._find_cpp()
        if cpp is None:
            return False

        # --- 公共参数 ---
        inc_args: List[str] = []
        for d in self._extra_inc_dirs:
            inc_args.append(f'-I{d}')
        for define in self._extra_defines:
            inc_args.append(f'-D{define}')

        # --- 第 1 步: cpp -dM 提取 #define ---
        cmd_dM: List[str] = [cpp, '-E', '-P', '-dM', '-x', 'c'] + inc_args + [header_path]
        define_text: str = ''
        try:
            proc: subprocess.CompletedProcess = subprocess.run(
                cmd_dM, capture_output=True, text=True, timeout=30, check=False,
            )
            if proc.returncode == 0 or proc.stdout:
                define_text = proc.stdout
        except (FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"[dtc-lite] warning: cpp -dM failed on "
                  f"'{header_path}': {e}", file=sys.stderr)
            return False
        if proc.returncode != 0:
            print(f"[dtc-lite] warning: cpp -dM returned {proc.returncode} on "
                  f"'{header_path}'", file=sys.stderr)
            if not define_text:
                return False

        # cpp -dM 输出形如:  #define NAME VALUE
        for line in define_text.split('\n'):
            line = line.rstrip('\r')
            m: Optional[re.Match] = re.match(
                r'#define\s+(\w+)(?:\s+(.*))?$', line)
            if m:
                name: str = m.group(1)
                value: str = (m.group(2) or '').strip()
                # 不覆盖用户在 dt-bindings 里手写的宏 (dt-bindings 优先级更高)
                if name not in self._macros:
                    self._macros[name] = value

        # --- 第 2 步: cpp -E -P 提取 enum 块 ---
        # enum 常量不会被 -dM 输出 (cpp 只输出 #define), 必须用 -E -P 拿展开后
        # 文本再正则解析. 输出含函数体/typedef 等, 但 enum 块语法独特, 正则可
        # 精确匹配 ``enum [Name] { ... }`` (体里无嵌套大括号).
        cmd_E: List[str] = [cpp, '-E', '-P', '-x', 'c'] + inc_args + [header_path]
        try:
            proc2: subprocess.CompletedProcess = subprocess.run(
                cmd_E, capture_output=True, text=True, timeout=30, check=False,
            )
            if proc2.returncode == 0 or proc2.stdout:
                self._extract_enums_from_text(proc2.stdout)
        except (FileNotFoundError, subprocess.TimeoutExpired) as e:
            print(f"[dtc-lite] warning: cpp -E failed on "
                  f"'{header_path}': {e}", file=sys.stderr)
            # define 已提取, enum 缺失不算致命
        if proc2.returncode != 0:
            print(f"[dtc-lite] warning: cpp -E returned {proc2.returncode} on "
                  f"'{header_path}' (enums may be incomplete)", file=sys.stderr)

        self._cpp_headers_loaded.add(header_path)
        return True

    def _extract_enums_from_text(self, text: str) -> None:
        """从 cpp 预处理后的文本里解析所有 ``enum [Tag] { ... }`` 块, 把每个
        命名常量按 C 规则求值后存入 ``self._macros``。

        C enum 规则:
          * 第一项若无显式值, 默认 0
          * 后续项若无显式值, 取上一项 + 1
          * 显式值可为任意 C 整型常量表达式 (含已有宏、位运算、括号等)

        支持: typedef enum { ... } Name;  enum Tag { ... };  enum { ... };
        不支持嵌套大括号 (enum 体内不应有大括号)。
        """
        # 剥注释 (cpp -E 仍保留 // 与 /* */ 在某些情况下)
        t: str = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
        t = re.sub(r'//[^\n]*', ' ', t)
        # 匹配 enum 块: enum 关键字 + 可选 tag + { 体 } (体内无大括号)
        for m in re.finditer(r'\benum\b\s*(?:\w+\s*)?\{([^{}]*)\}', t):
            body: str = m.group(1)
            cur_val: int = -1  # 第一项无 = 时取 0 (下面的 +1)
            for item in body.split(','):
                item = item.strip()
                if not item:
                    continue
                # 形如 NAME = EXPR 或 NAME
                mm: Optional[re.Match] = re.match(
                    r'(\w+)\s*(?:=\s*(.+))?$', item, re.S)
                if not mm:
                    continue
                ename: str = mm.group(1)
                eexpr: str = (mm.group(2) or '').strip()
                if eexpr:
                    # 链式宏替换 + 求值
                    resolved: str = self._replace_macros(eexpr)
                    val: Optional[int] = self._eval_c_constant(resolved)
                    if val is None:
                        # 求值失败 (依赖函数/未定义符号), 按递增兜底
                        cur_val += 1
                        val = cur_val
                else:
                    cur_val += 1
                    val = cur_val
                cur_val = val
                if ename not in self._macros:
                    self._macros[ename] = f'0x{val:X}'

    @staticmethod
    def _find_cpp() -> Optional[str]:
        """优先选择 arm-none-eabi-gcc (与项目工具链一致), 退到 gcc/cpp."""
        for c in ('arm-none-eabi-gcc', 'arm-none-eabi-cpp', 'gcc', 'cpp'):
            try:
                subprocess.run([c, '--version'], capture_output=True,
                               timeout=3, check=False)
                return c
            except (FileNotFoundError, subprocess.TimeoutExpired):
                continue
        return None

    # ───────────────────────── 预处理 ─────────────────────────

    def _preprocess_lines(self, text: str, base_dir: str) -> List[str]:
        """预处理 DTS 文本: 展开 #include、记录 #define 宏、按 #ifndef/#ifdef/#else
        跳过被屏蔽的分支。支持嵌套条件编译与头文件保护宏 (header guards)。

        厂商头支持:
          - 当 #include 解析到一个位于 ``Drivers/`` 之下的厂商头 (如
            ``stm32f4xx_ll_tim.h``) 时, 改用系统 cpp 跑 ``-E -P -dM`` 提取该头
            及其传递依赖的全部 #define, 直接灌入 ``_macros``; 不再递归内联该头
            文本 (厂商头里大量 #if defined(...) 与 extern "C" 等, 用 cpp 解析最稳)。
          - dt-bindings 头仍走原 Python 预处理路径, 保留 #ifndef guard 等特性。
        """
        lines: List[str] = text.split('\n')
        out: List[str] = []
        # 栈: (本分支是否跳过, 父级是否在跳过)
        skip_stack: List[Tuple[bool, bool]] = []

        def _skipping() -> bool:
            return any(s for s, _ in skip_stack)

        for line in lines:
            stripped: str = line.strip()

            # 1) #include "..." / #include <...> / /include/ "..."
            m = re.match(r'#include\s+"([^"]+)"', stripped)
            if not m:
                m = re.match(r'#include\s+<([^>]+)>', stripped)
            if not m:
                m = re.match(r'/include/\s+"([^"]+)"', stripped)
            if m and not _skipping():
                inc_path: Optional[str] = self._resolve_inc(m.group(1), base_dir)
                if inc_path:
                    if self._is_extra_header(inc_path):
                        # 用户 -I 路径里的头 (厂商/SDK): 用 cpp 一次性提取全部宏, 不内联文本
                        if not self._extract_header_macros(inc_path):
                            # cpp 不可用时回退: 用原路径展开 (尽力而为)
                            with open(inc_path, 'r', encoding='utf-8') as f:
                                inc_text = f.read()
                            out.extend(self._preprocess_lines(
                                inc_text, os.path.dirname(inc_path)))
                    else:
                        # dt-bindings 或相对路径头: 原 Python 路径展开 (保留 header guard)
                        with open(inc_path, 'r', encoding='utf-8') as f:
                            inc_text: str = f.read()
                        inc_lines: List[str] = self._preprocess_lines(
                            inc_text, os.path.dirname(inc_path))
                        out.extend(inc_lines)
                continue

            # 2) 预处理指令
            if stripped.startswith('#'):
                # #pragma once —— 由 _resolve_inc 的 _visited 去重保证，这里直接忽略
                if re.match(r'#pragma\s+once\b', stripped):
                    continue

                # #define NAME [VALUE]   (VALUE 可空，如头文件保护宏)
                m_def = re.match(r'#define\s+(\w+)(?:\s+(.*))?$', stripped)
                if m_def is not None and not _skipping():
                    name: str = m_def.group(1)
                    value: str = (m_def.group(2) or '').strip()
                    self._macros[name] = value
                    continue

                # #undef NAME
                m_und = re.match(r'#undef\s+(\w+)', stripped)
                if m_und is not None and not _skipping():
                    self._macros.pop(m_und.group(1), None)
                    continue

                # #ifndef NAME —— 若 NAME 已定义则跳过本分支
                m_ifn = re.match(r'#ifndef\s+(\w+)', stripped)
                if m_ifn is not None:
                    parent_skipped: bool = _skipping()
                    this_skipped: bool = parent_skipped or (m_ifn.group(1) in self._macros)
                    skip_stack.append((this_skipped, parent_skipped))
                    continue

                # #ifdef NAME —— 若 NAME 未定义则跳过本分支
                m_ifd = re.match(r'#ifdef\s+(\w+)', stripped)
                if m_ifd is not None:
                    parent_skipped = _skipping()
                    this_skipped = parent_skipped or (m_ifd.group(1) not in self._macros)
                    skip_stack.append((this_skipped, parent_skipped))
                    continue

                # #else —— 翻转当前分支 (仅当父级未跳过时才真正翻转)
                if stripped == '#else':
                    if skip_stack:
                        this_skipped, parent_skipped = skip_stack.pop()
                        new_skipped: bool = True if parent_skipped else (not this_skipped)
                        skip_stack.append((new_skipped, parent_skipped))
                    continue

                # #endif —— 弹出栈顶分支
                if stripped == '#endif':
                    if skip_stack:
                        skip_stack.pop()
                    continue

                # 其它指令 (#if / #elif / #error / #warning / #line ...) —— 跳过该行
                # 不影响 _skipping() 状态，避免误吞内容行
                continue

            # 3) 被屏蔽分支内的普通内容行直接丢弃
            if _skipping():
                continue

            # 4) 宏链式展开 + 常量求值, 然后输出
            out.append(self._replace_macros(line))

        return out

    # ───────────────────────── 宏展开与常量求值 ─────────────────────────

    _C_INT_RE = re.compile(r'(0[xX][0-9a-fA-F]+|\d+)([uU][lL]{0,2}|[lL]{1,2}[uU]?)\b')

    def _strip_c_int_suffix(self, text: str) -> str:
        """剥离 C 整数字面量的 U/L/UL 后缀, 如 0x10U → 0x10, 8UL → 8."""
        return self._C_INT_RE.sub(lambda m: m.group(1), text)

    def _replace_macros(self, text: str) -> str:
        """对文本做宏替换。空值宏 (如头文件保护宏 DTS_TIM_CTL_H) 不参与替换，
        避免把恰好包含该名字的标识符误替换成空串。用函数做替换值，防止宏值中
        的反斜杠序列 (如 ``\\1``) 被 ``re.sub`` 当作回溯引用。

        链式展开: 重复替换直到不再变化或达到上限 (处理 ``A → B → C → 0x10`` 这种
        厂商头里常见的链式 #define)。
        """
        if not self._macros:
            return text
        max_iters: int = 16
        for _ in range(max_iters):
            new_text: str = text
            for name in sorted(self._macros, key=lambda n: -len(n)):
                if not name:
                    continue
                value: str = self._macros[name]
                if not value:
                    # 空值宏 (头文件保护) 跳过
                    continue
                new_text = re.sub(r'\b' + re.escape(name) + r'\b',
                                  lambda _m, v=value: v, new_text)
            if new_text == text:
                break
            text = new_text

        # 常量求值: 对形如 ``< EXPR >`` 的属性值, 尝试把 EXPR 算成单个整数
        return self._eval_angle_value(text)

    def _eval_angle_value(self, text: str) -> str:
        """对文本中所有 ``< EXPR >`` 片段尝试做 C 常量求值。

        仅当 EXPR 含 ``<<`` / ``>>`` / ``|`` / ``&`` / ``~`` / ``^`` / ``(`` 等
        运算符或带 U/L 后缀的整数时才尝试求值, 失败则原样返回 (留给词法分析处理)。
        成功则用 ``< 0xNN >`` 形式替换, 保持与 dt-bindings 既有产物一致。

        注意: 表达式里可能含 ``<<`` / ``>>``, 单纯用 ``[^<>]`` 排除会失败, 所以
        用 ``(?:(?:<<|>>)|[^<>])`` 这种"先吃双字符运算符, 再吃普通字符"的策略。
        """
        if '<' not in text:
            return text

        def _replace(m: re.Match) -> str:
            expr: str = m.group(1).strip()
            if not expr:
                return m.group(0)
            # 仅在需要求值时尝试, 避免对 ``<foo>`` 这种纯标识符误判
            if not re.search(r'[|&~^<>()]|\b0[xX][0-9a-fA-F]+\b', expr):
                # 简单整数已可直接被 lexer 接受; 但若带 U/L 后缀也帮它脱掉
                if re.search(r'[uUlL]', expr):
                    stripped: str = self._strip_c_int_suffix(expr).strip()
                    return f'<{stripped}>'
                return m.group(0)
            val: Optional[int] = self._eval_c_constant(expr)
            if val is None:
                return m.group(0)
            return f'<0x{val:X}>'

        return re.sub(r'<((?:(?:<<|>>)|[^<>])*)>', _replace, text)

    def _eval_c_constant(self, expr: str) -> Optional[int]:
        """安全地求值一个 C 整型常量表达式。

        支持: 整数字面量 (含 U/L/UL 后缀), ``<<`` ``>>`` ``|`` ``&`` ``^`` ``~``
        ``+`` ``-`` ``*`` ``/`` ``%`` 以及括号。不支持函数调用 / sizeof / 复合字面量。

        返回: 求得的整数, 或 None (求值失败)。
        """
        if not expr:
            return None
        # 去掉所有 C 注释 (/* */ 与 //) —— cpp -dM 的输出已经剥过, 这里保险再剥一次
        s: str = re.sub(r'/\*.*?\*/', ' ', expr, flags=re.S)
        s = re.sub(r'//[^\n]*', ' ', s)
        # 剥 C 整数类型 cast: (uint8_t)/(uint16_t)/(uint32_t)/(int)/...
        # 厂商宏常见形如 ((uint32_t)0x00000001), 剥掉 cast 后成 (0x00000001) 可求值
        s = re.sub(
            r'\(\s*(?:uint8_t|uint16_t|uint32_t|uint64_t|int8_t|int16_t|int32_t|int64_t|unsigned|signed|unsigned\s+int|unsigned\s+long|unsigned\s+char|signed\s+int|signed\s+long|signed\s+char|int|long|short|char)\s*\)',
            '', s)
        # 剥离整数后缀
        s = self._strip_c_int_suffix(s)
        # 仅允许下列字符 (空白、数字、标识符为空、运算符、括号)
        # 若含其它字符 (函数名、sizeof 等) 直接放弃
        if not re.fullmatch(r'[\s0-9a-fA-FxX+\-*/%|&~^()<>=!]*', s):
            return None
        # 把 C 的位运算符原样保留 (Python 语法一致); 但 << / >> 已是 Python 运算符
        # 把单独的 = 滤掉 (避免被 eval 误判为赋值)
        s = s.replace('=', '')
        # C 的逻辑非 ! → Python not (注意: != 上面已把 = 删掉, 剩 ! 等于 not X)
        # 用简单替换: 独立的 ! 替成 ' not '
        s = re.sub(r'(?<![!=!])!(?!=)', ' not ', s)
        try:
            value: int = eval(s, {'__builtins__': {}}, {})  # noqa: S307 (deliberate)
            if isinstance(value, bool):  # True/False 不应出现, 但保险
                return None
            if not isinstance(value, int):
                return None
            return value
        except Exception:
            return None

    def _resolve_inc(self, name: str, base_dir: str) -> Optional[str]:
        candidates: List[str] = [
            os.path.join(base_dir, name),
            os.path.join(os.getcwd(), name),
            os.path.join(self.board_dir, name),
            os.path.join(self.board_dir, 'dtsi', name),
        ]
        if name.startswith('dt-bindings/'):
            candidates.insert(0, os.path.join(self.board_dir, name))
        # 用户 -I 传入的搜索路径: 让 #include <厂商头.h> / <任意 sdk 头> 都能找到
        for d in self._extra_inc_dirs:
            candidates.append(os.path.join(d, name))
        for p in candidates:
            p = os.path.normpath(p)
            if os.path.isfile(p):
                if p in self._visited:
                    return None
                self._visited.add(p)
                return p
        msg: str = (f"[dtc-lite] warning: include not found: '{name}' "
                    f"(searched {base_dir}, {os.getcwd()}")
        if self._extra_inc_dirs:
            msg += f", extra -I dirs: {', '.join(self._extra_inc_dirs)}"
        msg += ")"
        print(msg, file=sys.stderr)
        return None

    def compile(self) -> DTSCompiler:
        """完整编译流程"""
        with open(self.dts_path, 'r', encoding='utf-8') as f:
            text: str = f.read()
        text = self._preprocess(text)
        self.root = parse_dts(text, self.dts_path)

        # 🚀 突破：先建立基础映射，再延迟合并，允许无序引用和虚空创生
        self._build_label_map(self.root)
        self._merge_overlays()
        
        # 重新为创生的节点刷新一次全局地图
        self.label_map.clear()
        self._build_label_map(self.root)
        
        self._parse_special_nodes()
        self._scan_interrupt_controllers()
        self.device_list = self.root.collect_all_devices()
        self._deduplicate_devices()
        self._resolve_interrupts()
        self._scan_drivers()
        self._validate_compatibles()

        return self

    def _deduplicate_devices(self) -> None:
        seen: Set[str] = set()
        unique: List[DtsNode] = []
        for dev in self.device_list:
            key: str = dev.path
            if key not in seen:
                seen.add(key)
                unique.append(dev)
        self.device_list = unique

    def _scan_interrupt_controllers(self) -> None:
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
        self.device_irq_info = [[] for _ in self.device_list]
        for i, dev in enumerate(self.device_list):
            irqs: List[Tuple[int, int, int]] = self._resolve_device_interrupts(dev)
            self.device_irq_info[i] = irqs

    def _resolve_device_interrupts(self, dev: DtsNode) -> List[Tuple[int, int, int]]:
        prop: Optional[DtsProperty] = dev.get_prop('interrupts')
        if not prop or not prop.ints:
            return []

        ints: List[int] = prop.ints

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
            if len(chunk) < cells:
                break
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
        if node.label:
            self.label_map[node.label] = node
        for c in node.children:
            self._build_label_map(c)

    def _merge_overlays(self) -> None:
        """真·Linux 延迟匹配算法：支持 &label 虚空无中生有创建硬件节点"""
        refs: List[DtsNode] = []
        self._collect_ref_nodes(self.root, refs)

        for ref in refs:
            label: str = ref.name[1:]  # 剥离前缀 '&'
            target: Optional[DtsNode] = self.label_map.get(label)
            
            # 🚀 核心看齐 Linux：如果在 dtsi 里压根没声明过这个 label (例如 soc 或 spi1)
            if target is None:
                # 策略：如果名称类似 soc，直接当做根下的主 soc 控制器自创
                if label == "soc":
                    target = DtsNode("soc", label="soc", parent=self.root, line=ref.line)
                    if self.root:
                        self.root.children.append(target)
                else:
                    # 如果是外设（如 spi1），则默认自动创生并挂到虚拟根的 /soc 总线节点下面
                    soc_node = self.root.find_node_by_path('/soc') if self.root else None
                    if not soc_node:
                        # 确保兜底有 soc 容器
                        soc_node = DtsNode("soc", label="soc", parent=self.root, line=ref.line)
                        if self.root:
                            self.root.children.append(soc_node)
                    
                    # 自动猜测物理外设名称，为板级直接生成对应的硬件基底
                    target = DtsNode(label, label=label, parent=soc_node, line=ref.line)
                    soc_node.children.append(target)
                
                # 现场注册回全局地图，让后面其他同名引用也能找到
                self.label_map[label] = target

            # 执行属性级联与子外挂覆盖
            for prop in ref.props:
                existing: Optional[DtsProperty] = target.get_prop(prop.name)
                if existing:
                    existing.strings = prop.strings
                    existing.ints = prop.ints
                    existing.phandles = prop.phandles
                else:
                    target.props.append(prop)

            for child in ref.children:
                existing_child: Optional[DtsNode] = target.find_child(child.name)
                if existing_child:
                    self._merge_node(existing_child, child)
                else:
                    child.parent = target
                    target.children.append(child)

            if ref.parent:
                ref.parent.children = [c for c in ref.parent.children if c is not ref]

    def _collect_ref_nodes(self, node: DtsNode, refs: List[DtsNode]) -> None:
        if node.name.startswith('&'):
            refs.append(node)
        for child in node.children:
            self._collect_ref_nodes(child, refs)

    def _merge_node(self, target: DtsNode, src: DtsNode) -> None:
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
        pattern: re.Pattern[str] = re.compile(
            r'DRIVER_REGISTER\s*\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(\w+)\s*,\s*(\w+)\s*\)'
        )
        for drv_dir in self.driver_dirs:
            if not os.path.isdir(drv_dir):
                continue
            for root_dir, dirs, files in os.walk(drv_dir):
                for f in files:
                    if f.endswith(('.c', '.h', '.cpp', '.hpp')):
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
        errors: List[str] = []
        for dev in self.device_list:
            if dev.parent is None:
                continue
            compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
            if compat_prop and compat_prop.strings:
                compat: str = compat_prop.strings[0]
                if is_platform_node(dev):
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

    def _build_dep_graph(self) -> Tuple[List[List[int]], Dict[int, int]]:
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
        return graph, order_idx

    def compute_direct_children_tables(self) -> Dict[int, List[int]]:
        graph, order_idx = self._build_dep_graph()
        children: Dict[int, List[int]] = {}
        for i, direct in enumerate(graph):
            if direct:
                children[i] = sorted(direct, key=lambda x: order_idx.get(x, x))
        return children

    def compute_cascade_tables(self) -> Dict[int, List[int]]:
        graph, order_idx = self._build_dep_graph()
        n: int = len(self.device_list)

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

