# ==============================================================================
# toolchain_riscv.cmake — RISC-V 32-bit 裸机交叉编译
# ==============================================================================
# 适用于 RT-Thread / FreeRTOS RISC-V 后端
#
# 使用方式:
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_riscv.cmake
# ==============================================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(RISCV_TOOLCHAIN_DIR "D:/ESP32/xpack-riscv-none-elf-gcc-15.2.0-1")

set(CMAKE_C_COMPILER   "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-gcc.exe")
set(CMAKE_CXX_COMPILER "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-g++.exe")
set(CMAKE_ASM_COMPILER "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-gcc.exe")
set(CMAKE_AR           "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-ar.exe")
set(CMAKE_RANLIB       "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-ranlib.exe")
set(CMAKE_SIZE         "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-size.exe")
set(CMAKE_OBJCOPY      "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-objcopy.exe")
set(CMAKE_OBJDUMP      "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-objdump.exe")
set(CMAKE_GDB          "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-gdb-py3.exe")
set(CMAKE_NM           "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-nm.exe")
set(CMAKE_STRIP        "${RISCV_TOOLCHAIN_DIR}/bin/riscv-none-elf-strip.exe")

set(CMAKE_FIND_ROOT_PATH "${RISCV_TOOLCHAIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
