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

覆盖范围：
    _SCAN_DIRS 下的 C/C++ 源文件（.c/.cpp/.cc/.cxx）与头文件（.h/.hh/.hpp/.hxx）。
    头文件条目按 .clangd 约定分语言解析：
        .h/.hh   → C 头（-x c-header, -std=gnu17）
        .hpp/.hxx → C++ 头（-x c++, -std=gnu++17）
    这样 clang-tidy 对头文件单独检查时也能拿到正确的编译参数。

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

# 需要索引的目录（递归扫描源文件与头文件; src 与 include 均覆盖）
_SCAN_DIRS = [
    "core",
    "board",
    "osal",
    "system_c",
    "system_cpp",
    "hal",
    "bus",
    "vfs",
    "drivers",
    "can_hook",
    "interrupt",
    "algorithm",
    "time_slice",
]

_SOURCE_EXTENSIONS = {".c", ".cpp", ".cc", ".cxx"}
_HEADER_EXTENSIONS = {".h", ".hh", ".hpp", ".hxx"}

# 头文件 → (clang 语言参数, 标准)，与 .clangd 的语言切换规则一致
_HEADER_LANG = {
    ".h": ("c-header", "gnu17"),
    ".hh": ("c-header", "gnu17"),
    ".hpp": ("c++", "gnu++17"),
    ".hxx": ("c++", "gnu++17"),
}


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


def _collect_files() -> tuple[list[Path], list[Path]]:
    """扫描 _SCAN_DIRS 下的源文件与头文件，返回 (sources, headers)。"""
    sources: list[Path] = []
    headers: list[Path] = []
    for d in _SCAN_DIRS:
        base = _ROOT / d
        if not base.is_dir():
            continue
        for f in sorted(base.rglob("*")):
            if not f.is_file():
                continue
            if f.suffix in _SOURCE_EXTENSIONS:
                sources.append(f)
            elif f.suffix in _HEADER_EXTENSIONS:
                headers.append(f)
    return sources, headers


def _build_entries(flags: list[str], sources: list[Path], headers: list[Path]) -> list[dict]:
    """为每个源文件 / 头文件生成 compile_commands.json 条目。"""
    entries: list[dict] = []
    root_str = str(_ROOT)

    # 源文件：直接使用 compile_flags.txt 的参数（-std=gnu17 等）
    for src in sources:
        entries.append({
            "directory": root_str,
            "file": str(src),
            "arguments": ["clang"] + flags + ["-c", str(src)],
        })

    # 头文件：去掉 -std 后按扩展名指定语言，避免与源文件的 -std 冲突
    no_std_flags = [f for f in flags if not f.startswith("-std")]
    for hdr in headers:
        lang, std = _HEADER_LANG[hdr.suffix]
        entries.append({
            "directory": root_str,
            "file": str(hdr),
            "arguments": ["clang"] + no_std_flags + ["-x", lang, "-std=" + std, "-c", str(hdr)],
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
    sources, headers = _collect_files()
    if not sources:
        print("警告：未扫描到任何源文件", file=sys.stderr)

    entries = _build_entries(flags, sources, headers)
    content = json.dumps(entries, indent=2, ensure_ascii=False) + "\n"
    _atomic_write(_OUTPUT, content)
    print(f"已生成 {_OUTPUT}（{len(entries)} 条目: {len(sources)} 源 + {len(headers)} 头）")


if __name__ == "__main__":
    main()
