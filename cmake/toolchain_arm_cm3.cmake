# ARM Cortex-M3 交叉编译工具链 (GCC + newlib-nano)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)

set(CMAKE_C_FLAGS   "-mcpu=cortex-m3 -mthumb -std=c17 -Wall -Wextra -Os -g -ffunction-sections -fdata-sections" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-mcpu=cortex-m3 -mthumb -std=c++17 -Wall -Wextra -Os -g -ffunction-sections -fdata-sections -fno-rtti -fno-exceptions" CACHE STRING "" FORCE)
set(CMAKE_ASM_FLAGS "-mcpu=cortex-m3 -mthumb -x assembler-with-cpp" CACHE STRING "" FORCE)

# newlib-nano: 精简版 printf (不支持浮点), nosys: 弱符号 syscall 桩
set(CMAKE_EXE_LINKER_FLAGS "--specs=nano.specs --specs=nosys.specs -Wl,--gc-sections" CACHE STRING "" FORCE)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
