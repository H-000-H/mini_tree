"""DTS 编译器: 预处理、PLY 解析、语义合并与驱动校验."""

from __future__ import annotations

import os
import re
import sys
from typing import Any, Dict, List, Optional, Set, Tuple

from .ast import DtsNode, DtsProperty
from .parser import parse_dts
from .platform import is_platform_node

class DTSCompiler:
    """DTS 编译器: 解析 → 解析 → 生成"""

    def __init__(self, dts_path: str, driver_dirs: Optional[List[str]] = None) -> None:
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

    def _preprocess(self, text: str) -> str:
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
                m_def = re.match(r'#define\s+(\s+)\s*(.*)', stripped)
                if not m_def:
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
                elif stripped == '#endif':
                    continue

            if skip_depth > 0:
                continue

            out.append(self._replace_macros(line))

        return out

    def _replace_macros(self, text: str) -> str:
        if not self._macros:
            return text
        for name in sorted(self._macros, key=lambda n: -len(n)):
            if name:
                text = re.sub(r'\b' + re.escape(name) + r'\b',
                              self._macros[name], text)
        return text

    def _resolve_inc(self, name: str, base_dir: str) -> Optional[str]:
        candidates: List[str] = [
            os.path.join(base_dir, name),
            os.path.join(os.getcwd(), name),
            os.path.join(self.board_dir, name),
            os.path.join(self.board_dir, 'dtsi', name),
        ]
        if name.startswith('dt-bindings/'):
            candidates.insert(0, os.path.join(self.board_dir, name))
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

