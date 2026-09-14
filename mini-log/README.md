# mini-log

面向资源受限 MCU 的轻量日志库：**SPSC 无锁环形缓冲 + 可选 flash 落盘**，全静态分配、无 OS 依赖、可编译裁剪。包含作者自己的类外两个库的组件(crc buffer)全是Apache开源可直接使用

## 特性

- **零动态内存**：全部缓冲静态分配，无 `malloc`。
- **两条互不干扰的链路**：控制台输出 / flash 落盘。
- **SPSC 无锁**：单生产者单消费者；裸机无锁，OS / 多任务需调用方在外部加锁。
- **printf 风格宏**：带编译期级别过滤与自动行号。
- **flash 记录帧**：`magic/len/tick` 帧头 + 写入粒度对齐 + 写满回绕 + 跨扇区擦除。
- **统一错误码**：flash 链路的返回状态一律是 `int` 错误码（见 `inc/log_err.h`），并带落盘故障后的行边界重同步。
- **可裁剪**：`MINI_LOG_USE_FLASH` 一键关掉整条 flash 链路。

## 目录结构

```
mini-log/
├─ inc/
│  ├─ log.h          对外接口: 类型 / 宏 / 函数声明
│  ├─ log_config.h   配置开关
│  ├─ log_err.h      统一错误码
│  ├─ crc.h          CRC 分段续算接口
│  └─ crc_config.h   CRC 引擎 / 模型配置
├─ src/
│  ├─ log.c          实现
│  └─ crc.c          CRC 实现
├─ buffer/           SPSC 缓冲 (fifo_uni_spsc 等), 详见 buffer/README
├─ CMakeLists.txt
└─ README.md
```

## 数据流

```
控制台: MINI_LOG_x       -> mini_log_default_output -> s_ring       -> mini_log_flush       -> 输出回调 / stdout
flash : MINI_LOG_FLASH_x -> mini_log_flash_output   -> s_flash_ring -> mini_log_flash_flush -> flash
```

- **生产端**：在**栈上局部缓冲**完成格式化，只把成品字节入队，不做 I/O。
- **消费端**：分批取出环内字节；控制台走回调 / `stdout`，flash 逐条封装成记录帧落盘。

## 快速上手

### 1. 控制台日志

```c
MINI_LOG_I("boot ok");
MINI_LOG_W("voltage = %d mV", mv);

static void my_output(const char *str, size_t len) { uart_send(str, len); }
mini_log_set_output(my_output);          /* 不设置则默认 fwrite 到 stdout */
```

默认 `MINI_LOG_AUTO_FLUSH = 1`，即写即输出。

### 2. flash 落盘

先注册 flash 底层操作与分区信息（**五项 ops 必须齐全**）：

```c
static const mini_log_flash_ops_t ops = {
    .open = flash_open, .close = flash_close,
    .erase = flash_erase, .write = flash_write, .read = flash_read,
};
static const mini_log_flash_info_t info = {
    .base_addr   = 0x08080000,
    .total_size  = 64u * 1024,   /* 须为 sector_size 的整数倍 */
    .sector_size = 4096,
    .write_gran  = 8,            /* 0 或 2 的幂 */
};
if (mini_log_flash_register_cxt(&ops, &info) != MINI_LOG_OK)
    return;          /* 分区几何 / ops 不合法, 具体码见 inc/log_err.h */
```

写日志并落盘：

```c
MINI_LOG_FLASH_E("flash %d fail", id);   /* 仅入队到 flash 暂存环 */

if (mini_log_flash_flush() != MINI_LOG_OK)   /* 落盘; 或置 MINI_LOG_FLASH_AUTO_FLUSH=1 写成即落 */
    /* 底层故障, 库内会自动重同步 (见下节) */ ;

mini_log_flash_write_record("raw payload", 11);   /* 也可直接写一条记录 */
mini_log_read_from_flash(offset, len);            /* 读回, 内容落在库内私有暂存 */
mini_log_flash_clean_all();                       /* 擦除整分区并复位写偏移 */

uint32_t frames;
if (mini_log_flash_recover(&frames) == MINI_LOG_OK)
    /* frames = 有效帧数, 写偏移已恢复到第一条非法帧处 */ ;
```

### 3. 错误码

flash 链路的返回状态统一为 `int` 错误码（见 `inc/log_err.h`）：**成功恒为 0，失败恒为负**，用 `!= MINI_LOG_OK` 判断即可。错误码是普通整数宏，不是 enum，也不额外定义别名类型。

| 错误码 | 含义 |
|---|---|
| `MINI_LOG_OK` | 成功 |
| `MINI_LOG_ERR_PARAM` | 入参非法：`NULL` / `len==0` / 分区几何不合法 |
| `MINI_LOG_ERR_NOT_INIT` | 未注册，或注册校验失败 |
| `MINI_LOG_ERR_TOO_LONG` | 单帧放不进内部暂存上限或分区容量 |
| `MINI_LOG_ERR_FLASH_OPEN` | 底层 `open` 返回失败 |
| `MINI_LOG_ERR_FLASH_ERASE` | 底层 `erase` 返回失败 |
| `MINI_LOG_ERR_FLASH_WRITE` | 底层 `write` 返回失败 |
| `MINI_LOG_ERR_FLASH_READ` | 底层 `read` 返回失败 |

`mini_log_flash_recover` 把错误码与帧数分开回传：返回 `int` 错误码，帧数由出参 `uint32_t *out_frames` 带回（可传 `NULL` 只取状态）。

**落盘失败后的重同步**：`mini_log_flash_flush` 遇到底层故障（`OPEN` / `ERASE` / `WRITE`）时会把错误码原样透出，同时进入**重同步态** —— 丢弃暂存环里残留的半截数据直到下一个 `\n` 为止，让后续记录重新从行边界开始。否则那截半行会被当成一条完整日志落盘（帧 CRC 只覆盖自身 payload，事后无法识别）。重同步态由下一次 `flush` 自动消解，也可用 `mini_log_flash_clean_all` 立即清除；参数类失败（`PARAM` / `TOO_LONG`）不会触发它。

控制台 / 生产侧（`mini_log_flush`、`mini_log_default_output`、`mini_log_flash_output`）是 sink，保持 `void`。

### 4. 接入时间戳

本库不感知任何时基，由调用方注册回调：

```c
static int my_tick(void) { return (int)bsp_get_ms(); }   /* 返回 >= 0 */

mini_log_register_tick(my_tick);      /* 传 NULL 恢复 "未接入" 态 */
```

未接入（回调为 `NULL`，或回调返回负值）时，时间字段显示 `not support check time`。
回调可能在任意日志上下文（含 ISR）被调用，实现须无阻塞、无锁。

## 日志宏

| 宏 | 目标链路 |
|---|---|
| `MINI_LOG_E` / `W` / `I` / `D` | 控制台日志环 |
| `MINI_LOG_FLASH_E` / `W` / `I` / `D` | flash 暂存环 |

输出前缀：`[LEVEL] (<行号>) <时间> 消息`

```
[WARN] (42) t=12345 你的消息
[WARN] (42) not support check time 你的消息
```

级别由 `MINI_LOG_LOCAL_LEVEL` 编译期过滤，低于该级别的宏展开为 `((void)0)`。

## 配置（`inc/log_config.h`）

| 宏 | 默认 | 说明 |
|---|---|---|
| `MINI_LOG_MAX_LEN` | 128 | 单条格式化最大长度 |
| `MINI_LOG_RING_SIZE` | 1024 | 控制台环容量（2 的幂） |
| `MINI_LOG_AUTO_FLUSH` | 1 | 控制台写完立即输出 |
| `MINI_LOG_FLASH_AUTO_FLUSH` | 0 | flash 写完立即落盘 |
| `MINI_LOG_USE_FLASH` | 1 | 是否启用 flash 链路 |
| `MINI_LOG_FLASH_RING_SIZE` | 512 | flash 暂存环容量（2 的幂） |
| `MINI_LOG_COLOR_ENABLE` | 1 | ANSI 颜色开关 |
| `MINI_LOG_DEFAULT_ALIGIN` | 4 | 默认写入对齐粒度（2 的幂） |
| `MINI_LOG_MAGIC` | 4 | flash 记录帧魔数 |
| `MINI_LOG_FRAME_CRC_*` | CRC-16/CCITT-FALSE | flash 帧 CRC 模型（width/poly/init/refin/refout/xor） |

覆盖方式：`-DCONFIG_MINI_LOG_XXX=...`，或在包含头文件前 `#define MINI_LOG_XXX ...`。

## flash 记录格式

帧头 `mini_log_frame_handle_check_t`（`packed`，共 10 字节）：

| 字段 | 类型 | 说明 |
|---|---|---|
| `magic` | `uint16` | 魔数（`MINI_LOG_MAGIC`） |
| `len` | `uint16` | payload 长度 |
| `tick` | `uint32` | 写入时间戳 |
| `crc` | `uint16` | CRC-16（默认 CCITT-FALSE），覆盖 `[magic..tick] + payload` |

记录 = 帧头 + payload，按 `write_gran` 对齐，空余填 `0xFF`；写到分区尾部放不下时回绕到起点覆盖；目标落在扇区首地址时先擦除其覆盖的扇区。CRC 供帧有效性与记录边界校验。

## 并发与锁

本库为**独立无锁库**，内部环形队列均为 **SPSC**（单生产者单消费者）：
- 裸机单生产者场景无需加锁；
- 用于 OS / 多任务、且存在多个上下文并发访问同一条链路时，**必须由调用方在外部对相应环形队列的读写自行加锁**。

## 构建

```bash
cmake -S . -B build
cmake --build build
```

默认使用 `arm-none-eabi-gcc`（可通过 `MINI_LOG_GCC_JUNCTION` 指定无空格路径），产物为静态库 `libmini_log.a`。

## 已知限制 / TODO

- **掉电安全**：`write_offset` 未持久化，重启后需显式调用 `mini_log_flash_recover(&frames)` 上电扫描（逐帧校验 magic + CRC）来恢复写偏移；不调用则从头部覆盖写。
- **脏帧代价**：`recover` 若因 CRC 失败 / 帧长非法停下（残留脏帧而非 `0xFF` 空洞），会把写偏移对齐到**下一个扇区边界**再续写，本扇区剩余空间被废弃 —— 否则那处旧字节未擦除，续写会违反 flash 的 1→0 约束。
- **超长行截断**：单条日志超过 `MINI_LOG_MAX_LEN` 会被截断（末位补 `\n` 保住行边界），并按缓冲上限独立成帧（不会与相邻行黏连）。
- **配置前提**：`MINI_LOG_MAX_LEN` 不能无限调大 —— 单条最大长度的帧必须塞得进 `MINI_LOG_FLASH_RING_SIZE`（即 `ALIGN(10 + MINI_LOG_MAX_LEN - 1) <= MINI_LOG_FLASH_RING_SIZE`），否则所有记录都会被 `TOO_LONG` 拒绝；测试里有编译期断言守着这条。
- **读回无对外访问接口**：`mini_log_read_from_flash` 的结果落在库内私有暂存，暂未提供 getter / 按帧解析。

## License

Apache-2.0
