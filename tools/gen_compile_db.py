#!/usr/bin/env python3
"""gen_compile_db.py — 从 compile_flags.txt 生成 compile_commands.json（IDE 索引用）。

用法：
    python3 tools/gen_compile_db.py          # 在 mini_tree 根目录生成
    python3 tools/gen_compile_db.py --clean  # 删除已生成的 compile_commands.json

场景：
    mini_tree 作为子模块嵌入父项目时，父项目的 compile_commands.json 会覆盖
    mini_tree 的 compile_flags.txt，导致 hal/bus/vfs 头文件找不到。
    本脚本在 mini_tree 根生成 compile_commands.json（距离源文件更近），
    clangd 优先使用它，无需父项目 configure 即可获得完整索引。

幂等：重复执行结果一致；--clean 可移除生成物。
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent.parent
_FLAGS_FILE = _ROOT / "compile_flags.txt"
_OUTPUT = _ROOT / "compile_commands.json"

# 需要索引的源文件目录（递归扫描 .c / .cpp）
_SCAN_DIRS = [
    "core/src",
    "board/src",
    "osal/src",
    "system_c/src",
    "system_cpp/src",
    "hal",
    "bus",
    "vfs",
    "drivers",
    "can_hook",
    "interrupt",
    "algorithm",
    "time_slice",
]

_EXTENSIONS = {".c", ".cpp", ".cc", ".cxx"}


def _read_flags() -> list[str]:
    """读取 compile_flags.txt，返回有效编译参数列表。"""
    if not _FLAGS_FILE.exists():
        print(f"错误：{_FLAGS_FILE} 不存在", file=sys.stderr)
        sys.exit(1)
    flags: list[str] = []
    for line in _FLAGS_FILE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            flags.append(line)
    return flags


def _collect_sources() -> list[Path]:
    """扫描 _SCAN_DIRS 下所有 C/C++ 源文件。"""
    sources: list[Path] = []
    for d in _SCAN_DIRS:
        base = _ROOT / d
        if not base.is_dir():
            continue
        for f in sorted(base.rglob("*")):
            if f.suffix in _EXTENSIONS and f.is_file():
                sources.append(f)
    return sources


def _build_entries(flags: list[str], sources: list[Path]) -> list[dict]:
    """为每个源文件生成 compile_commands.json 条目。"""
    entries: list[dict] = []
    root_str = str(_ROOT)
    for src in sources:
        entries.append({
            "directory": root_str,
            "file": str(src),
            "arguments": ["clang"] + flags + ["-c", str(src)],
        })
    return entries


def _atomic_write(path: Path, content: str) -> None:
    """原子写入：先写临时文件再替换，防止生成残缺文件。"""
    import shutil
    import tempfile
    fd, tmp = tempfile.mkstemp(dir=str(path.parent), suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            f.write(content)
        shutil.move(tmp, str(path))
    except BaseException:
        os.unlink(tmp)
        raise


def main() -> None:
    if "--clean" in sys.argv:
        if _OUTPUT.exists():
            _OUTPUT.unlink()
            print(f"已删除 {_OUTPUT}")
        else:
            print("compile_commands.json 不存在，无需清理")
        return

    flags = _read_flags()
    sources = _collect_sources()
    if not sources:
        print("警告：未扫描到任何源文件", file=sys.stderr)

    entries = _build_entries(flags, sources)
    content = json.dumps(entries, indent=2, ensure_ascii=False) + "\n"
    _atomic_write(_OUTPUT, content)
    print(f"已生成 {_OUTPUT}（{len(entries)} 条目）")


if __name__ == "__main__":
    main()
