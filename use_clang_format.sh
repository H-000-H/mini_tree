#!/bin/bash
find net/ -type f \( -name "*.c" -o -name "*.h" \) -exec clang-format -i {} +