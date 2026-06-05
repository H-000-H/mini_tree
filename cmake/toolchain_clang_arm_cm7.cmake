# ARM Cortex-M7 交叉编译工具链 (Clang/LLVM)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_ASM_COMPILER clang)
set(CMAKE_AR           llvm-ar)

set(CMAKE_C_FLAGS   "--target=arm-none-eabi -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -std=c2x -Wall -Wextra -Os -g -ffreestanding" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--target=arm-none-eabi -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -std=c++23 -Wall -Wextra -fno-exceptions -fno-rtti -Os -g -ffreestanding" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "--target=arm-none-eabi -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16 -x assembler-with-cpp" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
