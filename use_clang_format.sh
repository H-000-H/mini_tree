#!/bin/bash
# 对 mini_tree 仓库中除 lib/ build/ dist/ 之外的所有 .c/.h 运行 clang-format
# lib/ 是 vendored 第三方源码(含自带 .clang-format 的子模块), build/ 与 dist/ 是产物, 一律不碰。
set -e

cd "$(dirname "$0")"

find . -type f \( -name "*.c" -o -name "*.h" \) \
    -not -path "./lib/*" \
    -not -path "./build/*" \
    -not -path "./dist/*" \
    -not -path "./.venv/*" \
    -not -path "./.cache/*" \
    -exec clang-format -i {} +
