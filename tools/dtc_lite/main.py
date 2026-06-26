"""dtc-lite 命令行入口."""

from __future__ import annotations

import argparse
import os
import sys
from typing import List, Optional

from .ast import DtsProperty
from .compiler import DTSCompiler
from .generator import CGenerator


def main(argv: Optional[List[str]] = None) -> None:
    parser = argparse.ArgumentParser(
        description='MCU 编译期 DeviceTree 编译器 — DTS → C 代码生成 (PLY)',
    )
    parser.add_argument('dts_path', type=str, help='输入的 .dts 文件路径')
    parser.add_argument('output_dir', type=str, help='生成文件的输出目录')
    parser.add_argument(
        'driver_dirs', type=str, nargs='*', default=[],
        help='扫描 DRIVER_REGISTER 宏的驱动源码目录 (可多个)',
    )
    args = parser.parse_args(argv)

    dts_path: str = args.dts_path
    output_dir: str = args.output_dir
    driver_dirs: List[str] = args.driver_dirs

    if not os.path.isfile(dts_path):
        print(f'ERROR: DTS file not found: {dts_path}', file=sys.stderr)
        sys.exit(1)

    print(f'dtc-lite: {dts_path}')
    print(f'  output: {output_dir}')
    if driver_dirs:
        print(f'  driver scan: {", ".join(driver_dirs)}')

    compiler = DTSCompiler(dts_path, driver_dirs)
    compiler.compile()

    print(f'  devices: {len(compiler.device_list)}')
    for dev in compiler.device_list:
        compat_prop: Optional[DtsProperty] = dev.get_prop('compatible')
        compat = (
            compat_prop.strings[0]
            if compat_prop and compat_prop.strings
            else '(no compatible)'
        )
        deps = compiler.get_device_deps(dev)
        dep_labels = ', '.join(deps) if deps else '(none)'
        print(f'    {dev.path:40s} compat={compat:25s} deps=[{dep_labels}]')

    print(f'  drivers matched: {len(compiler.driver_map)}')
    for compat, fn in sorted(compiler.driver_map.items()):
        print(f'    {compat:40s} → {fn}')

    generator = CGenerator(compiler, output_dir)
    generator.gen_all()

    print('dtc-lite: done')


if __name__ == '__main__':
    main()
