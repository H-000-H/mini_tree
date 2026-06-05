# ==============================================================================
# mini_tree — 独立 Makefile (GCC / Clang / Keil 5/6)
# ==============================================================================
# 用法:
#   make PLATFORM=arm_cm4f TOOLCHAIN=gcc       # GCC ARM Cortex-M4F
#   make PLATFORM=arm_cm4f TOOLCHAIN=clang     # Clang ARM Cortex-M4F
#   make PLATFORM=arm_cm4f TOOLCHAIN=keil6     # Keil 6 ARMCLANG Cortex-M4F
#   make PLATFORM=arm_cm3  TOOLCHAIN=keil5     # Keil 5 ARMCLANG (AC6) Cortex-M3
#   make PLATFORM=riscv TOOLCHAIN=gcc           # GCC RISC-V
#   make PLATFORM=posix                         # 本地构建与测试
#   make clean
#
# 高级配置 (环境变量 / make 参数):
#   CC, CXX, AS, AR        — 覆盖编译器/汇编器/归档器
#   OSAL_BACKEND           — FREERTOS / RTTHREAD / NULL (默认 FREERTOS)
#   FREERTOS_HEAP          — 堆分配器 1-5 (默认 4)
#   FREERTOS_PORT          — FreeRTOS 端口名 (默认从 PLATFORM 派生)
#   BUILD_DIR              — 输出目录 (默认 build_make)
#   DEBUG                  — y=启用调试符号 (默认 y)
# ==============================================================================

# 默认目标: `make` = `make all`
.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# 默认配置
# ---------------------------------------------------------------------------
PLATFORM      ?= posix
TOOLCHAIN     ?= gcc
OSAL_BACKEND  ?= FREERTOS
FREERTOS_HEAP ?= 4
BUILD_DIR     ?= build_make
PYTHON        ?= python
DEBUG         ?= y

# ---------------------------------------------------------------------------
# 项目根目录 (Makefile 所在目录)
# ---------------------------------------------------------------------------
ROOT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---------------------------------------------------------------------------
# 工具链选择
# ---------------------------------------------------------------------------
ifeq ($(TOOLCHAIN),gcc)
  HOST_CC   ?= gcc
  HOST_CXX  ?= g++
  HOST_AS   ?= gcc
  HOST_AR   ?= ar

  ARM_CC    ?= arm-none-eabi-gcc
  ARM_CXX   ?= arm-none-eabi-g++
  ARM_AS    ?= arm-none-eabi-gcc
  ARM_AR    ?= arm-none-eabi-ar

  RISCV_CC  ?= riscv-none-elf-gcc
  RISCV_CXX ?= riscv-none-elf-g++
  RISCV_AS  ?= riscv-none-elf-gcc
  RISCV_AR  ?= riscv-none-elf-ar
endif

ifeq ($(TOOLCHAIN),clang)
  HOST_CC   ?= clang
  HOST_CXX  ?= clang++
  HOST_AS   ?= clang
  HOST_AR   ?= llvm-ar

  ARM_CC    ?= clang
  ARM_CXX   ?= clang++
  ARM_AS    ?= clang
  ARM_AR    ?= llvm-ar

  RISCV_CC  ?= clang
  RISCV_CXX ?= clang++
  RISCV_AS  ?= clang
  RISCV_AR  ?= llvm-ar
endif

ifeq ($(TOOLCHAIN),keil6)
  ARM_CC    ?= armclang
  ARM_CXX   ?= armclang
  ARM_AS    ?= armclang
  ARM_AR    ?= armar

  # 自动检测 STM32CubeCLT (ST 定制的 armclang)
  _ST_ARMCLANG := $(wildcard C:/ST/STM32CubeCLT_*/st-arm-clang)
  ifneq ($(_ST_ARMCLANG),)
    _LATEST := $(lastword $(sort $(_ST_ARMCLANG)))
    ifeq ($(ARM_CC),armclang)
      ARM_CC  := $(_LATEST)/bin/starm-clang
      ARM_CXX := $(_LATEST)/bin/starm-clang
      ARM_AS  := $(_LATEST)/bin/starm-clang
      ARM_AR  := $(_LATEST)/bin/starm-ar
    endif
    KEIL6_SYSROOT ?= $(_LATEST)/lib/clang-runtimes/newlib/arm-none-eabi
  endif
endif

ifeq ($(TOOLCHAIN),keil5)
  ARM_CC    ?= armclang
  ARM_CXX   ?= armclang
  ARM_AS    ?= armclang
  ARM_AR    ?= armar

  # 自动检测 Keil MDK 工具链 (标准安装路径 C:/Keil_v5)
  _KEIL_BIN := $(wildcard C:/Keil_v5/ARM/ARMCLANG/bin)
  ifneq ($(_KEIL_BIN),)
    ifeq ($(ARM_CC),armclang)
      ARM_CC  := $(_KEIL_BIN)/armclang.exe
      ARM_CXX := $(_KEIL_BIN)/armclang.exe
      ARM_AS  := $(_KEIL_BIN)/armclang.exe
      ARM_AR  := $(_KEIL_BIN)/armar.exe
    endif
  endif
endif

# Clang 需要 --target 标志
ifeq ($(TOOLCHAIN),clang)
  CLANG_ARM_TARGET   ?= --target=arm-none-eabi
  CLANG_RISCV_TARGET ?= --target=riscv32-unknown-elf
endif

# ---------------------------------------------------------------------------
# 平台 → 工具链 / 标志 映射
# ---------------------------------------------------------------------------
# arm_cm3, arm_cm4f, arm_cm7, riscv, posix
# ---------------------------------------------------------------------------
ifeq ($(PLATFORM),arm_cm3)
  CC  := $(ARM_CC)
  CXX := $(ARM_CXX)
  AS  := $(ARM_AS)
  AR  := $(ARM_AR)
  ARCH_FLAGS  := -mcpu=cortex-m3 -mthumb
  FREERTOS_PORT := GCC_ARM_CM3
  PLATFORM_MCU := ARM_CM3
  TARGET_ARM := y
endif

ifeq ($(PLATFORM),arm_cm4f)
  CC  := $(ARM_CC)
  CXX := $(ARM_CXX)
  AS  := $(ARM_AS)
  AR  := $(ARM_AR)
  ARCH_FLAGS  := -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16
  FREERTOS_PORT := GCC_ARM_CM4F
  PLATFORM_MCU := ARM_CM4F
  TARGET_ARM := y
endif

ifeq ($(PLATFORM),arm_cm7)
  CC  := $(ARM_CC)
  CXX := $(ARM_CXX)
  AS  := $(ARM_AS)
  AR  := $(ARM_AR)
  ARCH_FLAGS  := -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-d16
  FREERTOS_PORT := GCC_ARM_CM4F
  PLATFORM_MCU := ARM_CM7
  TARGET_ARM := y
endif

ifeq ($(PLATFORM),riscv)
  CC  := $(RISCV_CC)
  CXX := $(RISCV_CXX)
  AS  := $(RISCV_AS)
  AR  := $(RISCV_AR)
  ARCH_FLAGS  := -march=rv32imac_zicsr -mabi=ilp32 -mcmodel=medany
  FREERTOS_PORT := GCC_RISC_V
  PLATFORM_MCU := RISCV
  TARGET_RISCV := y
endif

ifeq ($(PLATFORM),posix)
  CC  ?= $(HOST_CC)
  CXX ?= $(HOST_CXX)
  AS  ?= $(HOST_AS)
  AR  ?= $(HOST_AR)
  ARCH_FLAGS  :=
  FREERTOS_PORT := GCC_POSIX
  PLATFORM_MCU := POSIX
endif

# Clang target flags
ifeq ($(TOOLCHAIN),clang)
  ifeq ($(TARGET_ARM),y)
    ARCH_FLAGS := $(CLANG_ARM_TARGET) $(ARCH_FLAGS)
  endif
  ifeq ($(TARGET_RISCV),y)
    ARCH_FLAGS := $(CLANG_RISCV_TARGET) $(ARCH_FLAGS)
  endif
endif

# ARMCLANG (AC6) target flags — both keil5 (Keil MDK) and keil6 (STM32CubeCLT)
ifneq ($(filter keil5 keil6,$(TOOLCHAIN)),)
  ifeq ($(TARGET_ARM),y)
    ARCH_FLAGS := --target=arm-arm-none-eabi $(ARCH_FLAGS)
  endif
endif

# keil6 (STM32CubeCLT) additional: sysroot for newlib
ifeq ($(TOOLCHAIN),keil6)
  ifneq ($(KEIL6_SYSROOT),)
    ARCH_FLAGS += --sysroot=$(KEIL6_SYSROOT)
  endif
endif
ifeq ($(PLATFORM),)
$(error 请指定 PLATFORM=arm_cm3|arm_cm4f|arm_cm7|riscv|posix)
endif

# Keil 5/6 仅支持 ARM 平台
ifeq ($(TOOLCHAIN),keil5)
  ifneq ($(TARGET_ARM),y)
    $(error TOOLCHAIN=keil5 仅支持 ARM 平台 (arm_cm3/arm_cm4f/arm_cm7))
  endif
endif
ifeq ($(TOOLCHAIN),keil6)
  ifneq ($(TARGET_ARM),y)
    $(error TOOLCHAIN=keil6 仅支持 ARM 平台 (arm_cm3/arm_cm4f/arm_cm7))
  endif
endif

# 从 PLATFORM_MCU 推導 Kconfig 等效定义
KCONFIG_DEFS := -DCONFIG_PLATFORM_$(PLATFORM_MCU)=y

# ---------------------------------------------------------------------------
# 编译标志
# ---------------------------------------------------------------------------
CFLAGS   := $(ARCH_FLAGS) -std=c2x -Wall -Wextra -Werror=implicit-function-declaration
CXXFLAGS := $(ARCH_FLAGS) -std=c++23 -Wall -Wextra -fno-exceptions -fno-rtti
ASFLAGS  := $(ARCH_FLAGS) -x assembler-with-cpp
ARFLAGS  := rcs

# ARMCLANG (AC6) C/C++ standard: C23/C++23 → C17/C++17 (armclang stable baseline)
# keil5 uses Keil MDK armclang (no -ffreestanding)
# keil6 uses STM32CubeCLT armclang (needs -ffreestanding for newlib stdatomic workaround)
ifneq ($(filter keil5 keil6,$(TOOLCHAIN)),)
  CFLAGS   := $(subst -std=c2x,-std=c17,$(CFLAGS))
  CXXFLAGS := $(subst -std=c++23,-std=c++17,$(CXXFLAGS))
  ARFLAGS  := -r
endif

# STM32CubeCLT newlib 对 C23 兼容性有限, 降级到 C17
# -ffreestanding 避免 newlib 有缺陷的 stdatomic.h (使用 clang 内置版本)
ifeq ($(TOOLCHAIN),keil6)
  CFLAGS   += -ffreestanding
endif

ifeq ($(DEBUG),y)
  CFLAGS   += -Og -ggdb
  CXXFLAGS += -Og -ggdb
endif

# 非 POSIX 平台使用优化尺寸
ifneq ($(PLATFORM),posix)
  CFLAGS   += -Os -DNDEBUG
  CXXFLAGS += -Os -DNDEBUG
endif

# POSIX 需要 pthread
ifeq ($(PLATFORM),posix)
  CFLAGS   += -pthread
  CXXFLAGS += -pthread
endif

# ── Kconfig C/C++ 标准覆盖 (CUSTOM_C_STANDARD) ──
# 当 .config 中存在 CONFIG_CUSTOM_C_STANDARD=y 时,
# 用 Kconfig 中的 C_STANDARD / CXX_STANDARD 替换默认 -std= 标志.
# GCC / Clang / ARMCLANG: -std=$(value)
# ARMCLANG (AC6) 6.x: 不支持 -std=c23 (ISO 名), 使用 c17 降级
ifneq ($(wildcard $(ROOT_DIR).config),)
  ifeq ($(shell grep -c "^CONFIG_CUSTOM_C_STANDARD=y" $(ROOT_DIR).config),1)
    _C_STD  := $(shell grep "^CONFIG_C_STANDARD=" $(ROOT_DIR).config  | sed 's/^CONFIG_C_STANDARD="\(.*\)"$$/\1/')
    _CXX_STD := $(shell grep "^CONFIG_CXX_STANDARD=" $(ROOT_DIR).config | sed 's/^CONFIG_CXX_STANDARD="\(.*\)"$$/\1/')
    ifneq ($(_C_STD),)
      ifneq ($(filter keil5 keil6,$(TOOLCHAIN)),)
        # AC6 6.x (LLVM 14) does not support -std=c23; fall back to c17
        _C_STD := $(subst c23,c17,$(subst c2x,c17,$(_C_STD)))
      endif
      CFLAGS := $(filter-out -std=%,$(CFLAGS)) -std=$(_C_STD)
    endif
    ifneq ($(_CXX_STD),)
      ifneq ($(filter keil5 keil6,$(TOOLCHAIN)),)
        _CXX_STD := $(subst c++23,c++17,$(subst c++2b,c++17,$(_CXX_STD)))
      endif
      CXXFLAGS := $(filter-out -std=%,$(CXXFLAGS)) -std=$(_CXX_STD)
    endif
  endif
endif

# ---------------------------------------------------------------------------
# OSAL 后端选择 → Kconfig 定义 + 源文件
# ---------------------------------------------------------------------------
OSAL_DEF :=

ifeq ($(OSAL_BACKEND),FREERTOS)
  OSAL_DEF     := -DCONFIG_OSAL_FREERTOS=y
  OSAL_SRCS    := osal/src/osal_freertos.c
  OSAL_DEPS    := freertos_kernel
  OSAL_EXTRA_LIBS :=
  ifeq ($(PLATFORM),posix)
    OSAL_EXTRA_LIBS := -lpthread -lrt
  endif
  NEED_FREERTOS := y
endif

ifeq ($(OSAL_BACKEND),RTTHREAD)
  OSAL_DEF     := -DCONFIG_OSAL_RTTHREAD=y
  OSAL_SRCS    := osal/src/osal_rtthread.c
  OSAL_DEPS    := rtthread_kernel
  NEED_RTTHREAD := y
endif

ifeq ($(OSAL_BACKEND),NULL)
  OSAL_DEF     := -DCONFIG_OSAL_NULL=y
  OSAL_SRCS    := osal/src/osal_null.c
  OSAL_DEPS    :=
endif

# 合并所有预定义
KCONFIG_DEFS += $(OSAL_DEF)

# 系统日志后端 (默认直出 printf, 无依赖)
SYS_LOG_BACKEND ?= PRINTF
ifeq ($(SYS_LOG_BACKEND),PRINTF)
  KCONFIG_DEFS += -DCONFIG_SYS_LOG_USE_PRINTF=y
else ifeq ($(SYS_LOG_BACKEND),OSAL)
  KCONFIG_DEFS += -DCONFIG_SYS_LOG_USE_OSAL=y
else ifeq ($(SYS_LOG_BACKEND),ESP)
  KCONFIG_DEFS += -DCONFIG_SYS_LOG_USE_ESP=y
endif

# AC6 (ARMCLANG) handles GCC inline asm natively — FreeRTOS ports work

# ---------------------------------------------------------------------------
# 路径
# ---------------------------------------------------------------------------
SRC_DIR    := $(ROOT_DIR)
BUILD      := $(BUILD_DIR)
OBJ_DIR    := $(BUILD)/obj
LIB_DIR    := $(BUILD)/lib

# 生成文件路径 (仿 CMake Kconfig 生成)
KCONFIG_GEN_DIR := $(BUILD)/generated/kconfig
BOARD_GEN_DIR   := $(BUILD)/board/generated

# ---------------------------------------------------------------------------
# 源文件
# ---------------------------------------------------------------------------

# -- algorithm --
ALGORITHM_SRCS := algorithm/buffer/circle_fifo_buffer.c

# -- hal_if --
HAL_IF_SRCS := hal_if/src/hal_if_dummy.c

# -- core --
CORE_SRCS := \
  core/src/event_bus.cpp \
  core/src/production_log.c \
  core/src/buffer_pool.c

# -- osal (由 OSAL_BACKEND 决定) --
OSAL_SRCS ?= osal/src/osal_freertos.c

# -- board --
BOARD_SRCS := \
  board/src/task_config.c \
  board/src/task_utils.c \
  board/src/board_device.c \
  board/src/board_driver.c \
  board/src/config_store.c

# -- system --
SYSTEM_SRCS := \
  system_cpp/src/lifecycle.cpp \
  system_cpp/src/safe_state.c \
  system_cpp/src/system_init.cpp \
  system_cpp/src/system_runtime.cpp \
  system_cpp/src/system_scrubber.cpp \
  system_cpp/src/system_wdt.cpp \
  system_cpp/src/task_manager.cpp

# -- FreeRTOS kernel --
FREERTOS_SRCS := \
  lib/freeRTOS/tasks.c \
  lib/freeRTOS/queue.c \
  lib/freeRTOS/list.c \
  lib/freeRTOS/timers.c \
  lib/freeRTOS/event_groups.c \
  lib/freeRTOS/stream_buffer.c \
  lib/freeRTOS/portable/Common/mpu_wrappers_v2.c \
  lib/freeRTOS/portable/MemMang/heap_$(FREERTOS_HEAP).c

# FreeRTOS 端口源文件
ifeq ($(FREERTOS_PORT),GCC_ARM_CM3)
  FREERTOS_PORT_DIR := GCC/ARM_CM3
  FREERTOS_SRCS += lib/freeRTOS/portable/GCC/ARM_CM3/port.c
endif
ifeq ($(FREERTOS_PORT),GCC_ARM_CM4F)
  FREERTOS_PORT_DIR := GCC/ARM_CM4F
  FREERTOS_SRCS += lib/freeRTOS/portable/GCC/ARM_CM4F/port.c
endif
ifeq ($(FREERTOS_PORT),GCC_RISC_V)
  FREERTOS_PORT_DIR := GCC/RISC-V
  FREERTOS_SRCS += \
    lib/freeRTOS/portable/GCC/RISC-V/port.c \
    lib/freeRTOS/portable/GCC/RISC-V/portASM.S
endif
ifeq ($(FREERTOS_PORT),GCC_POSIX)
  FREERTOS_PORT_DIR := ThirdParty/GCC/Posix
  FREERTOS_SRCS += \
    lib/freeRTOS/portable/ThirdParty/GCC/Posix/port.c \
    lib/freeRTOS/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c
endif

# FreeRTOS 端口包含路径 (在 pattern rule eval 前置入, 杜绝死锁)
FREERTOS_PORT_INC :=
ifneq ($(FREERTOS_PORT_DIR),)
  FREERTOS_PORT_INC := -I$(ROOT_DIR)lib/freeRTOS/portable/$(FREERTOS_PORT_DIR)
endif

# ---------------------------------------------------------------------------
# 包含路径
# ---------------------------------------------------------------------------
INCLUDES := \
  -Icore/include \
  -Iboard/include \
  -Ihal_if/include \
  -Iosal/include \
  -Isystem_cpp/include \
  -Ialgorithm/buffer \
  -I$(ROOT_DIR)lib/freeRTOS/include \
  -I$(ROOT_DIR)lib/freeRTOS \
  -I$(ROOT_DIR)board/include \
  -I$(KCONFIG_GEN_DIR) \
  -I$(BOARD_GEN_DIR) \
  -I$(ROOT_DIR)lib/rtthread/include \
  -I$(ROOT_DIR)lib/rtthread \
  $(FREERTOS_PORT_INC)

# -- RT-Thread kernel --
RTTHREAD_SRCS := \
  lib/rtthread/src/clock.c \
  lib/rtthread/src/cpu_up.c \
  lib/rtthread/src/idle.c \
  lib/rtthread/src/ipc.c \
  lib/rtthread/src/irq.c \
  lib/rtthread/src/kservice.c \
  lib/rtthread/src/mem.c \
  lib/rtthread/src/object.c \
  lib/rtthread/src/scheduler_comm.c \
  lib/rtthread/src/scheduler_up.c \
  lib/rtthread/src/thread.c \
  lib/rtthread/src/timer.c \

# RT-Thread 架构端口
ifeq ($(PLATFORM_MCU),ARM_CM3)
  RTTHREAD_SRCS += lib/rtthread/libcpu/arm/common/atomic_arm.c
  RTTHREAD_ARCH_DIR := arm/cortex-m3
  RTTHREAD_SRCS += \
    lib/rtthread/libcpu/arm/cortex-m3/context_gcc.S \
    lib/rtthread/libcpu/arm/cortex-m3/cpuport.c
endif
ifeq ($(PLATFORM_MCU),ARM_CM4F)
  RTTHREAD_SRCS += lib/rtthread/libcpu/arm/common/atomic_arm.c
  RTTHREAD_ARCH_DIR := arm/cortex-m4
  RTTHREAD_SRCS += \
    lib/rtthread/libcpu/arm/cortex-m4/context_gcc.S \
    lib/rtthread/libcpu/arm/cortex-m4/cpuport.c
endif
ifeq ($(PLATFORM_MCU),ARM_CM7)
  RTTHREAD_SRCS += lib/rtthread/libcpu/arm/common/atomic_arm.c
  RTTHREAD_ARCH_DIR := arm/cortex-m7
  RTTHREAD_SRCS += \
    lib/rtthread/libcpu/arm/cortex-m7/context_gcc.S \
    lib/rtthread/libcpu/arm/cortex-m7/cpuport.c
endif
ifeq ($(PLATFORM_MCU),POSIX)
  RTTHREAD_SRCS += lib/rtthread/libcpu/x86/atomic.c
endif
ifeq ($(PLATFORM_MCU),RISCV)
  RTTHREAD_ARCH_DIR := risc-v/common
  RTTHREAD_SRCS += \
    lib/rtthread/libcpu/risc-v/common/context_gcc.S \
    lib/rtthread/libcpu/risc-v/common/cpuport.c \
    lib/rtthread/libcpu/risc-v/common/interrupt_gcc.S \
    lib/rtthread/libcpu/risc-v/common/trap_common.c \
    lib/rtthread/libcpu/risc-v/common/atomic_riscv.c
endif

# AC6 (ARMCLANG) integrated assembler handles GNU .S natively

# ---------------------------------------------------------------------------
# 目标静态库
# ---------------------------------------------------------------------------
LIBS := \
  $(LIB_DIR)/libalgorithm.a \
  $(LIB_DIR)/libhal_if.a \
  $(LIB_DIR)/libcore.a \
  $(LIB_DIR)/libosal.a \
  $(LIB_DIR)/libboard.a \
  $(LIB_DIR)/libsystem.a

ifeq ($(NEED_FREERTOS),y)
  LIBS += $(LIB_DIR)/libfreertos_kernel.a
endif
ifeq ($(NEED_RTTHREAD),y)
  LIBS += $(LIB_DIR)/librtthread_kernel.a
endif

ALL_LIBS := $(LIBS)

# ---------------------------------------------------------------------------
# 规则: 编译 .c → .o
# ---------------------------------------------------------------------------

# RT-Thread kernel sources need __RT_KERNEL_SOURCE__ for internal APIs
$(OBJ_DIR)/lib/rtthread/%.o: lib/rtthread/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(KCONFIG_DEFS) -D__RT_KERNEL_SOURCE__ $(INCLUDES) -c $< -o $@
	@echo "  CC    $<"

# FreeRTOS POSIX port needs POSIX signal compat on MinGW
$(OBJ_DIR)/lib/freeRTOS/portable/ThirdParty/GCC/Posix/port.o: lib/freeRTOS/portable/ThirdParty/GCC/Posix/port.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -include board/include/freertos_compat.h $(KCONFIG_DEFS) $(INCLUDES) -c $< -o $@
	@echo "  CC    $<"

define COMPILE_C
$(OBJ_DIR)/%.o: %.c
	mkdir -p $$(dir $$@)
	$(CC) $(CFLAGS) $(KCONFIG_DEFS) $(INCLUDES) -c $$< -o $$@
	@echo "  CC    $$<"
endef
$(eval $(COMPILE_C))

# 规则: 编译 .cpp → .o
define COMPILE_CXX
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $$(dir $$@)
	$(CXX) $(CXXFLAGS) $(KCONFIG_DEFS) $(INCLUDES) -c $$< -o $$@
endef
$(eval $(COMPILE_CXX))

# 规则: 编译 .S → .o
define COMPILE_ASM
$(OBJ_DIR)/%.o: %.S
	@mkdir -p $$(dir $$@)
	$(AS) $(ASFLAGS) $(KCONFIG_DEFS) $(INCLUDES) -c $$< -o $$@
endef
$(eval $(COMPILE_ASM))

# ---------------------------------------------------------------------------
# 生成 config.h (简易版 Kconfig 生成)
# ---------------------------------------------------------------------------
$(KCONFIG_GEN_DIR)/config.h: | $(KCONFIG_GEN_DIR)
	@echo "/* auto-generated by Makefile */" > $@
	@echo "#ifndef MINI_TREE_CONFIG_H" >> $@
	@echo "#define MINI_TREE_CONFIG_H" >> $@
	@echo "" >> $@
	@echo "#define CONFIG_PLATFORM_$(PLATFORM_MCU) 1" >> $@
ifeq ($(OSAL_BACKEND),FREERTOS)
	@echo "#define CONFIG_OSAL_FREERTOS 1" >> $@
endif
ifeq ($(OSAL_BACKEND),RTTHREAD)
	@echo "#define CONFIG_OSAL_RTTHREAD 1" >> $@
endif
ifeq ($(OSAL_BACKEND),NULL)
	@echo "#define CONFIG_OSAL_NULL 1" >> $@
endif
	@echo "" >> $@
	@echo "#endif /* MINI_TREE_CONFIG_H */" >> $@

$(KCONFIG_GEN_DIR):
	@mkdir -p $(KCONFIG_GEN_DIR)

# ---------------------------------------------------------------------------
# 生成 board DTS (如果 dtc-lite.py 存在)
# ---------------------------------------------------------------------------
BOARD_DTS      := $(ROOT_DIR)board/board.dts
DTC_LITE       := $(ROOT_DIR)tools/dtc-lite.py

# 全局静态检测 DTC_LITE，根除 eval 变量死锁
DTC_EXISTS := $(shell test -f $(DTC_LITE) && echo y)

BOARD_GEN_SRCS := \
  $(BOARD_GEN_DIR)/board_devtable.c \
  $(BOARD_GEN_DIR)/board_probe.c \
  $(BOARD_GEN_DIR)/board_force_link.c

$(BOARD_GEN_SRCS): $(BOARD_DTS) $(DTC_LITE) | $(BOARD_GEN_DIR)
	@if [ "$(DTC_EXISTS)" = "y" ]; then \
		$(PYTHON) "$(DTC_LITE)" "$(BOARD_DTS)" "$(BOARD_GEN_DIR)" "$(ROOT_DIR)board/src"; \
	else \
		echo "警告: dtc-lite.py 未找到, 跳过 DTS 生成"; \
		for f in $(BOARD_GEN_SRCS); do \
			test -f $$f || echo "/* stub */" > $$f; \
		done \
	fi

$(BOARD_GEN_DIR):
	@mkdir -p $(BOARD_GEN_DIR)

# ---------------------------------------------------------------------------
# 库构建规则
# ---------------------------------------------------------------------------

# 辅助函数: 将源文件列表转为 .o 对象文件列表
src_to_obj = $(addprefix $(OBJ_DIR)/, $(addsuffix .o, $(basename $(1))))

# -- algorithm --
ALGORITHM_OBJS := $(call src_to_obj,$(ALGORITHM_SRCS))
$(LIB_DIR)/libalgorithm.a: $(ALGORITHM_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- hal_if --
HAL_IF_OBJS := $(call src_to_obj,$(HAL_IF_SRCS))
$(LIB_DIR)/libhal_if.a: $(HAL_IF_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- core --
CORE_OBJS := $(call src_to_obj,$(CORE_SRCS))
$(LIB_DIR)/libcore.a: $(CORE_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- osal --
OSAL_OBJS := $(call src_to_obj,$(OSAL_SRCS))
$(LIB_DIR)/libosal.a: $(OSAL_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- board (依赖 DTS 生成) --
BOARD_OBJS := $(call src_to_obj,$(BOARD_SRCS))
BOARD_GEN_OBJS := $(call src_to_obj,$(BOARD_GEN_SRCS))
$(LIB_DIR)/libboard.a: $(BOARD_OBJS) $(BOARD_GEN_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- system --
SYSTEM_OBJS := $(call src_to_obj,$(SYSTEM_SRCS))
$(LIB_DIR)/libsystem.a: $(SYSTEM_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- FreeRTOS kernel --
FREERTOS_OBJS := $(call src_to_obj,$(FREERTOS_SRCS))
$(LIB_DIR)/libfreertos_kernel.a: $(FREERTOS_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- RT-Thread kernel --
RTTHREAD_OBJS := $(call src_to_obj,$(RTTHREAD_SRCS))
$(LIB_DIR)/librtthread_kernel.a: $(RTTHREAD_OBJS) | $(LIB_DIR)
	$(AR) $(ARFLAGS) $@ $^

# -- display_if (头文件库, 无源文件) --

$(LIB_DIR):
	@mkdir -p $(LIB_DIR)

# ---------------------------------------------------------------------------
# 顶层目标
# ---------------------------------------------------------------------------
.PHONY: all clean

all: $(KCONFIG_GEN_DIR)/config.h $(BOARD_GEN_SRCS) $(ALL_LIBS)
	@echo ""
	@echo "=============================================="
	@echo "  mini_tree 构建完成"
	@echo "  平台:     $(PLATFORM)"
	@echo "  工具链:   $(TOOLCHAIN)"
	@echo "  OSAL:     $(OSAL_BACKEND)"
	@echo "  输出:     $(LIB_DIR)"
	@echo "=============================================="
	@ls -la $(LIB_DIR)

clean:
	rm -rf $(BUILD_DIR)

# ---------------------------------------------------------------------------
# 包含路径补全 (为 FreeRTOS 端口目录添加 include)
# ---------------------------------------------------------------------------
ifneq ($(FREERTOS_PORT_DIR),)
  FREERTOS_PORT_INC := -I$(ROOT_DIR)lib/freeRTOS/portable/$(FREERTOS_PORT_DIR)
  INCLUDES += $(FREERTOS_PORT_INC)
endif
