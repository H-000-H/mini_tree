#!/bin/bash

#手动生成Kconfig文件
python tools/genconfig.py Kconfig.non_esp build/Debug/generated/kconfig/mini_tree --config .config
cat build/Debug/generated/kconfig/mini_tree/config.h