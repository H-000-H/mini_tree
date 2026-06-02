# RISC-V 32-bit 裸机交叉编译工具链 (Clang/LLVM)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_AR           llvm-ar)
set(CMAKE_OBJDUMP      llvm-objdump)
set(CMAKE_SIZE         llvm-size)

set(CMAKE_C_FLAGS   "--target=riscv32-unknown-elf -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany -std=c2x -Wall -Wextra -Os -g" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--target=riscv32-unknown-elf -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany -std=c++23 -Wall -Wextra -fno-exceptions -fno-rtti -Os -g" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "--target=riscv32-unknown-elf -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany -x assembler-with-cpp" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
