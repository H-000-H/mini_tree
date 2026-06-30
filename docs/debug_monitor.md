## 10. 调试与监控

### 10.1 反汇编审查

开启 `CONFIG_BUILD_DISASM=y`：

```bash
cmake --preset Debug -DCONFIG_BUILD_DISASM=y
cmake --build build/Debug
ls build/Debug/disasm/
# algorithm.lst  board.lst  core.lst  hal.lst  osal.lst  system.lst
```

> 反汇编 `.lst` 路径在 `<build_dir>/<preset>/disasm/` 下，由 `cmake/disasm.cmake` 通过 `mini_tree_add_disasm_postbuild()` 注入到具体项目的 POST_BUILD 阶段。`hal.lst` 包含 `hal/hal_if_dummy.c`、`hal/cpu/hal_cpu_amp.c`、`hal/pwm/hal_pwm.c` 以及各平台目录下的 HAL 实现文件（如 `hal/spi/hal_spi_stm32.c`、`hal/uart/hal_uart_ch32.c`、`hal/gpio/hal_gpio_esp32.c`），具体由 `.config` 中 `CONFIG_HAL_GPIO_<VENDOR>` 选项决定。

审查要点：
- BufferPool 无锁分配的原子指令 (`lock cmpxchg` / RV32 `amoswap`)
- `__builtin_ctz` 的前导零扫描（ARM `rbit+clz` / RISC-V `ctz`）
- 编译器是否内联了关键路径上的小函数
- C++ `fno-rtti` / `fno-exceptions` 后是否仍残留 RTTI 符号

### 10.2 安全监控

| 监控项 | 机制 | 告警阈值 |
|--------|------|---------|
| 栈溢出 | 魔术字扫描 | 剩余 < 512 字节 |
| Task 卡死 | TWDT 超时复位 | 3 秒未喂狗 |
| CPU 总线死锁 | RTC 硬件看门狗 | 8 秒超时 |
| Flash Bit-Rot | CRC32 逐页巡检 | 任意校验失配 |
| Bootloop | NVS 计数器 | ≥ 5 次连续崩溃 |

### 10.3 HardFault 现场调试

ARM Cortex-M 硬故障关键寄存器：

| 寄存器 | 含义 |
|--------|------|
| `PC` | 异常发生时的指令地址 |
| `LR` | 返回地址或 EXC_RETURN |
| `CFSR` | 细分故障类型 (Usage/Bus/MemFault) |
| `HFSR` | 硬故障状态 (Forced = escalation) |
| `MMFAR` | MemFault 目标地址 |

定位步骤：

1. 从调试器提取 `PC` 值
2. 反查反汇编 `.lst`：
   - STM32：`arm-none-eabi-objdump -S build/Debug/disasm/hal.lst`
   - CH32V307：`riscv32-wch-elf-objdump -S build/Debug/disasm/hal.lst`
3. 在 `.lst` 中搜索 `PC` 地址定位源码行

> RISC-V (CH32V307) 异常寄存器：`mepc`（异常 PC）、`mcause`（异常原因）、`mtval`（异常值）。

### 10.4 烧录与调试 (CMake 路线)

CMake 构建产出的 `.elf` 通过以下路线烧录调试。

#### 路线 A：Cortex-Debug 插件 (主流推荐)

VS Code 的 **Cortex-Debug** 插件（绿色瓢虫图标）提供图形化调试：设断点、看寄存器、单步执行。只需配置一次 `.vscode/launch.json`：

```json
{
    "version": "0.2.0",
    "configurations": [{
        "name": "OpenOCD Debug",
        "type": "cortex-debug",
        "request": "launch",
        "servertype": "openocd",
        "device": "YOUR_CHIP_MODEL",
        "interface": "swd",
        "runToMain": true,
        "svdFile": "path/to/your_chip.svd",
        "executable": "${workspaceFolder}/build/Debug/ch307_node.elf"
    }]
}
```

CMake 的 .elf 输出路径在 `build/<preset>/` 下，按实际路径修改 `executable` 指向（如 `build/Debug/ch307_node.elf`）。

配置好后 F5 启动调试，打断点、看外设寄存器、变量监视全在 VS Code 界面内完成。

#### 路线 B：OpenOCD + GDB (CLI 备选)

无插件或 CI 环境下使用：

```bash
# STM32 (ARM): OpenOCD + arm-none-eabi-gdb
openocd -f interface/stlink.cfg -f target/stm32f4x.cfg
arm-none-eabi-gdb build/Debug/demo.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "continue"

# CH32V307 (RISC-V): wch-openocd + riscv32-wch-elf-gdb
wch-openocd -f interface/cmsis-dap.cfg -f target/riscv.cfg
riscv32-wch-elf-gdb build/Debug/ch307_node.elf \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "load" \
    -ex "continue"
```

### 10.5 OSAL_NULL 单元测试

`OSAL_NULL` 后端允许在主机上编译运行测试，无需开发板：

```bash
cmake -B build_test -DPLATFORM_POSIX=ON -DOSAL_BACKEND=NULL
cmake --build build_test
```

```c
// 测试 buffer pool
void test_buffer_pool(void)
{
    bp_config_t cfg = { .name = "test", .buf_size = 64, .buf_count = 4 };
    bp_t* pool = bp_create(&cfg);
    assert(pool != NULL);

    void* b1 = bp_alloc(pool);
    void* b2 = bp_alloc(pool);
    assert(b1 != NULL && b2 != NULL);
    assert(b1 != b2);

    bp_free(pool, b1);
    bp_free(pool, b2);

    void* b3 = bp_alloc(pool);
    assert(b3 != NULL);  /* 释放后可重用 */

    bp_destroy(pool);
}

// 测试 EventBus
void test_eventbus(void)
{
    EventBus::getInstance().init();
    g_system_os_initialized = true;

    bool received = false;
    EventBus::getInstance().subscribe(
        EVENT_SYS_READY, EVENT_SYS_READY,
        [](const Event& e, void* ud) { *(bool*)ud = true; }, &received);

    EventBus::getInstance().post(EVENT_SYS_READY);
    mini_tree_system_loop();

    assert(received);
}
```

### 10.6 业务任务调试技巧

业务任务通过 `task_manager_create_task()` 创建并自动订阅 TWDT，调试时可借助以下机制：

| 现象 | 排查方向 |
|------|----------|
| 3 秒后复位（TWDT 触发） | 任务死循环或 `device_read` 阻塞超时；检查是否调用 `system_wdt_feed()` |
| 设备找不到（`IS_ERR(dev)`） | DTS 未定义该 `label`，或 `status="disabled"`；查 `device_get_status(dev)` |
| `device_open` 返回 `VFS_ERR_BUSY` | 设备已被其他任务打开且未关闭；检查 `ref_count` |
| `device_ioctl` 返回 `VFS_ERR_INVAL` | `cmd` 不支持或 `arg_len` 不匹配；查驱动 `ops->ioctl` 实现 |
| 任务创建后立即删除 | `task_manager_create_task` 返回 NULL；查 stack_size 是否过大（SRAM 不够） |
| SPI 收包后 `dispatch` 不到 handler | 命令名拼写不一致；`app_cmd_handlers_register()` 未在 `start_tasks` 前调用 |
