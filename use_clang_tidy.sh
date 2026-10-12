#!/bin/bash
run-clang-tidy -p build/Debug -j 8 \
  -exclude-header-filter='lib/' \
  '.*' 2>&1 | grep -vE "lib/lwip|lib/"

