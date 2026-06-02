# ARM Cortex-M4F 交叉编译工具链 (Keil MDK v5 + ARMCLANG AC6)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   armclang)
set(CMAKE_CXX_COMPILER armclang)
set(CMAKE_ASM_COMPILER armclang)
set(CMAKE_AR           armar)

set(CMAKE_C_FLAGS   "--target=arm-arm-none-eabi -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -O3 -g -Wall" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--target=arm-arm-none-eabi -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -std=c++17 -O3 -g -Wall -fno-exceptions -fno-rtti" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "--target=arm-arm-none-eabi -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -x assembler-with-cpp" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
