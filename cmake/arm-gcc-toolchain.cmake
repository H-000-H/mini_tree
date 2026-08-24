# mini_tree ARM 交叉编译工具链文件
# 用法: cmake -DCMAKE_TOOLCHAIN_FILE=cmake/arm-gcc-toolchain.cmake ...

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 禁止 CMake 尝试编译测试程序（交叉编译环境无运行时）
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 工具链前缀
set(TOOLCHAIN_PREFIX arm-none-eabi-)

# 查找工具
set(CMAKE_C_COMPILER   ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_AR           ${TOOLCHAIN_PREFIX}ar)
set(CMAKE_OBJCOPY      ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP      ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE         ${TOOLCHAIN_PREFIX}size)

# 目标 MCU（默认 Cortex-M4F，板级可覆盖）
if(NOT DEFINED MCU_FLAGS)
    set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16")
endif()

# 通用编译选项
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra")
set(CMAKE_CXX_FLAGS_INIT "${MCU_FLAGS} -ffunction-sections -fdata-sections -Wall -Wextra -fno-rtti -fno-exceptions")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS}")

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage")

# 查找路径
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
