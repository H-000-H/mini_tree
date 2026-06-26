"""C 代码生成器 — 从编译结果输出 board_devtable / board_probe 等."""

from __future__ import annotations

import os
import shutil
from typing import Dict, List, Optional, Set, Tuple

from .ast import DtsNode, DtsProperty
from .compiler import DTSCompiler
from .platform import is_platform_node

class CGenerator:
    """从编译结果生成 C 代码"""

    def __init__(self, compiler: DTSCompiler, output_dir: str) -> None:
        self.compiler: DTSCompiler = compiler
        self.output_dir: str = output_dir
        os.makedirs(output_dir, exist_ok=True)

    def _atomic_write(self, path: str, content: str) -> None:
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
        self._gen_board_nodes_h()
        self._gen_board_devtable_h()
        self._gen_board_devtable_c()
        self._gen_board_probe_c()
        self._gen_board_handles_h()
        self._gen_dt_config_h()

    def _snake_name(self, name: str) -> str:
        n: str = name.replace('@', '_').replace('-', '_').replace('.', '_').replace('/', '_')
        return n.upper()

    def _c_safe_name(self, name: str) -> str:
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

    def _gen_board_devtable_h(self) -> None:
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
            'const struct device_node* board_node_get(device_id_t id);',
            'int board_dev_count(void);',
            'device_id_t board_dev_find(const char* name);',
            'device_id_t board_dev_find_by_compat(const char* compatible);',
            'device_id_t board_dev_find_by_label(const char* label);',
            '',
            'struct device* board_dev_get(device_id_t id);',
            '',
            'const device_id_t* board_probe_order(void);',
            'int board_probe_order_count(void);',
            '',
            'typedef int (*probe_fn_t)(struct device*);',
            'typedef int (*remove_fn_t)(struct device*);',
            'probe_fn_t board_probe_get_fn(device_id_t id);',
            'remove_fn_t board_remove_get_fn(device_id_t id);',
            '',
            'const device_id_t* board_cascade_get(device_id_t id, int* count);',
            'const device_id_t* board_children_get(device_id_t id, int* count);',
            '',
            '#ifdef __cplusplus',
            '}',
            '#endif',
            '',
            '#endif /* BOARD_DEVTABLE_H */',
        ]
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_devtable_c(self) -> None:
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
                prop_arrays.append(f'static const struct device_property DEV_{safe}_props[] = {{')
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

        reg_data_arrays: List[str] = []
        reg_arrays: List[str] = []
        reg_info_map: Dict[int, Tuple[int, str]] = {}
        for i, dev in enumerate(devs):
            safe = self._c_safe_name(dev.name)
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

            entries: List[Tuple[List[int], List[int]]] = []
            for j in range(0, len(ints), entry_size):
                chunk: List[int] = ints[j:j + entry_size]
                addr_part: List[int] = chunk[:ac]
                size_part: List[int] = chunk[ac:] if sc > 0 else []
                entries.append((addr_part, size_part))

            flat: List[str] = []
            for addr_part, size_part in entries:
                flat.extend(hex(v) for v in addr_part)
                flat.extend(hex(v) for v in size_part)

            reg_data_arrays.append(f'static const uint32_t DEV_{safe}_REG_DATA[] = {{')
            reg_data_arrays.append(f'    {", ".join(flat)},')
            reg_data_arrays.append('};')

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
            reg_arrays.append(f'static const struct device_reg {reg_arr_name}[] = {{')
            reg_arrays.extend(reg_entries)
            reg_arrays.append('};')
            reg_arrays.append('')
            reg_info_map[i] = (len(entries), reg_arr_name)

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
            irq_arrays.append(f'static const struct device_irq {irq_arr_name}[] = {{')
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
                d in label_to_idx for d in self.compiler.get_device_deps(dev)
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
                f'        .regs       = (const struct device_reg*){reg_ref},\n'
                f'        .irq_count  = {irq_count},\n'
                f'        .irqs       = (const struct device_irq*){irq_ref},\n'
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
            '/* ===== reg 分组表 ===== */',
            '',
        ] + reg_data_arrays + reg_arrays + [
            '/* ===== irq 表 ===== */',
            '',
        ] + irq_arrays + [
            '/* ===== 主节点表 ===== */',
            f'static const struct device_node s_nodes[DEV_ID_COUNT] = {{',
        ] + node_entries + [
            '};',
            '',
            'const struct device_node* board_node_get(device_id_t id) {',
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

    def _append_device_id_relation_table(
        self,
        lines: List[str],
        devs: List[DtsNode],
        table: Dict[int, List[int]],
        sym_prefix: str,
        func_name: str,
    ) -> None:
        dev_count: int = len(devs)
        counts: List[int] = [0] * dev_count
        for i in range(dev_count):
            if i in table:
                counts[i] = len(table[i])

        offsets: List[int] = [0] * dev_count
        cumulative: int = 0
        for i in range(dev_count):
            offsets[i] = cumulative
            cumulative += counts[i]

        lines.append(f'static const device_id_t s_{sym_prefix}_data[] = {{')
        has_data: bool = False
        for i in range(dev_count):
            if i in table:
                has_data = True
                for child_idx in table[i]:
                    child_dev: DtsNode = devs[child_idx]
                    lines.append(f'    DEV_ID_{self._snake_name(child_dev.name)},')
        if not has_data:
            lines.append('    0,')
        lines += [
            '};',
            '',
            f'static const uint8_t s_{sym_prefix}_counts[DEV_ID_COUNT] = {{',
        ]
        for i in range(dev_count):
            lines.append(f'    [DEV_ID_{self._snake_name(devs[i].name)}] = {counts[i]},')
        lines += [
            '};',
            '',
            f'static const uint16_t s_{sym_prefix}_offset[DEV_ID_COUNT] = {{',
        ]
        for i in range(dev_count):
            lines.append(f'    [DEV_ID_{self._snake_name(devs[i].name)}] = {offsets[i]},')
        lines += [
            '};',
            '',
            f'const device_id_t* {func_name}(device_id_t id, int* count) {{',
            '    if ((int)id < 0 || (int)id >= DEV_ID_COUNT) { *count = 0; return NULL; }',
            f'    *count = s_{sym_prefix}_counts[id];',
            f'    return *count ? &s_{sym_prefix}_data[s_{sym_prefix}_offset[id]] : NULL;',
            '}',
        ]

    def _gen_board_probe_c(self) -> None:
        devs: List[DtsNode] = self.compiler.device_list
        order: List[int] = self.compiler.topological_sort()
        path: str = os.path.join(self.output_dir, 'board_probe.c')

        has_platform: bool = False
        probe_externs: List[str] = []
        remove_externs: List[str] = []
        probe_extern_seen: Set[str] = set()
        remove_extern_seen: Set[str] = set()
        probe_array: List[str] = []
        remove_array: List[str] = []
        for i in devs:
            compat_prop: Optional[DtsProperty] = i.get_prop('compatible')
            snake: str = self._snake_name(i.name)
            if compat_prop and compat_prop.strings:
                compat: str = compat_prop.strings[0]
                if compat in self.compiler.driver_map:
                    p_fn, r_fn = self.compiler.driver_map[compat]
                    if p_fn not in probe_extern_seen:
                        probe_extern_seen.add(p_fn)
                        probe_externs.append(f'extern int __attribute__((weak)) {p_fn}(struct device* dev);')
                    if r_fn not in remove_extern_seen:
                        remove_extern_seen.add(r_fn)
                        remove_externs.append(f'extern int __attribute__((weak)) {r_fn}(struct device* dev);')
                    probe_array.append(f'    [DEV_ID_{snake}] = {p_fn},')
                    remove_array.append(f'    [DEV_ID_{snake}] = {r_fn},')
                elif is_platform_node(i):
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
                'static int board_platform_probe(struct device* dev) {',
                '    (void)dev;',
                '    return 0;',
                '}',
            ]

        lines += [
            '',
            f'static const probe_fn_t s_probe_fns[DEV_ID_COUNT] = {{',
        ] + probe_array + [
            '};',
            '',
            f'static const remove_fn_t s_remove_fns[DEV_ID_COUNT] = {{',
        ] + remove_array + [
            '};',
            '',
            f'static const device_id_t s_probe_order[DEV_ID_COUNT] = {{',
        ] + order_entries + [
            '};',
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
        ]

        cascade: Dict[int, List[int]] = self.compiler.compute_cascade_tables()
        children: Dict[int, List[int]] = self.compiler.compute_direct_children_tables()

        self._append_device_id_relation_table(
            lines, devs, cascade, 'cascade', 'board_cascade_get')
        lines.append('')
        self._append_device_id_relation_table(
            lines, devs, children, 'children', 'board_children_get')

        lines += ['']
        self._write_if_changed(path, '\n'.join(lines))

    def _gen_board_handles_h(self) -> None:
        path: str = os.path.join(self.output_dir, 'board_handles.h')
        lines: List[str] = [
            '#ifndef BOARD_HANDLES_H',
            '#define BOARD_HANDLES_H',
            '',
            '#include "board_nodes.h"',
            '',
        ]

        if self.compiler.chosen_map:
            lines.append('/* ===== chosen 设备 ===== */')
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

