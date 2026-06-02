## 10. 调试与监控

### 10.1 反汇编审查

开启 `CONFIG_BUILD_DISASM=y`：

```bash
cmake -B build -DCONFIG_BUILD_DISASM=y
cmake --build build
ls build/disasm/
# algorithm.lst  board.lst  core.lst  hal_if.lst  osal.lst  system.lst
```

审查要点：
- BufferPool 无锁分配的原子指令 (`lock cmpxchg`)
- `__builtin_ctz` 的前导零扫描 (`bsf` / `clz`)
- 编译器是否内联了关键路径上的小函数

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
2. `arm-none-eabi-objdump -S build/board/src/board_device.lst`
3. 在 `.lst` 中搜索 `PC` 地址定位源码行

### 10.4 烧录与调试 (CMake / Makefile 路线)

CMake/Makefile 构建产出的 `.elf` 通过以下两条路线烧录调试。Keil 路线见 [Keil MDK 集成说明](keil_integration.md)。

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
        "executable": "${workspaceFolder}/build/demo.elf"
    }]
}
```

CMake 与 Makefile 的 .elf 输出路径不同（`build/` vs `build_make/`），按实际路径修改 `executable` 指向。

配置好后 F5 启动调试，打断点、看外设寄存器、变量监视全在 VS Code 界面内完成。

#### 路线 B：OpenOCD + GDB (CLI 备选)

无插件或 CI 环境下使用：

```bash
# 以目标芯片为例，替换为实际芯片配置
openocd -f interface/stlink.cfg -f target/your_chip_target.cfg
```

```bash
arm-none-eabi-gdb build/demo.elf \
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
