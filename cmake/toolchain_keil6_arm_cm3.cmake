# ARM Cortex-M3 交叉编译工具链 (Keil MDK v6 / ARMCLANG)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   armclang)
set(CMAKE_CXX_COMPILER armclang)
set(CMAKE_ASM_COMPILER armclang)
set(CMAKE_AR           armar)

set(CMAKE_C_FLAGS   "--target=arm-arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c17 -Wall -Wextra -Os -g -ffreestanding" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--target=arm-arm-none-eabi -mcpu=cortex-m3 -mthumb -std=c++20 -Wall -Wextra -Os -g -fno-exceptions -fno-rtti -ffreestanding" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "--target=arm-arm-none-eabi -mcpu=cortex-m3 -mthumb -x assembler-with-cpp" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
