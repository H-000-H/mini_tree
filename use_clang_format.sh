#!/bin/bash
# 对 mini_tree 仓库中除 lib/ 和 build/ 之外的所有 .c/.h 运行 clang-format
find . -type f \( -name "*.c" -o -name "*.h" \) \
    -not -path "./lib/*" \
    -not -path "./build/*" \
    -exec clang-format -i {} +
