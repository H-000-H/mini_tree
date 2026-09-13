# mini_ota

兼容 mini-tree 的简单 OTA 与 bootloader 项目：固件侧提供 `bootutil`（启动/下载/校验/激活/回滚）+ `algorithm`（CRC/SHA/AES），PC 侧提供镜像打包、镜像校验、OTA 全流程测试台。

## 工具路径

| 工具 | 路径 | 说明 |
|---|---|---|
| 打包 | `tools/main.py` | 把 bin 打包成 OTA 镜像（CRC/SHA/GCM/CBC/CBC_SHA） |
| 查看/对比 | `tools/check_bin_file.py` | 查看字节、逐字节 diff、镜像解析后 diff |
| 镜像校验 (C++) | `test/check.cpp` | 编译产物 `pc_check.exe`，命令行校验镜像并与明文比对 |
| PC 测试台 (C++) | `test/main.cpp` | 编译产物 `main.exe`，运行时菜单 + setjmp 模拟启动 |

## 使用方法

### 打包 (tools/main.py)

```bash
# CRC 校验(默认): payload + version + crc
python tools/main.py app.bin image.bin --version 1.0.0 --tag h-000-h

# SHA-256 校验
python tools/main.py app.bin image.bin --check SHA

# AES-GCM 加密(--key 必填, 16/24/32 字节 hex)
python tools/main.py app.bin image.bin --check GCM --key 00112233445566778899aabbccddeeff

# AES-CBC + HMAC-SHA-256(--mac_key 不给则复用 --key)
python tools/main.py app.bin image.bin --check CBC_SHA \
    --key 00112233445566778899aabbccddeeff \
    --mac_key ffeeddccbbaa99887766554433221100

# 元数据放文件头部(默认在尾部)
python tools/main.py app.bin image.bin --is_front
```

可选参数: `--version`(默认 1.0.0) `--tag`(默认空) `--is_front`
`--crc_init --crc_refin --crc_refout --crc_xor_out --crc_poly`(改 CRC 模型, 默认值即标准 CRC-32)

`test/` 下已有一组各模式的打包样例（`crc.bin` / `sha.bin` / `gcm.bin` / `cbc_sha.bin`），可直接用于校验/对比。

### 查看 / 对比 (tools/check_bin_file.py)

```bash
python tools/check_bin_file.py dump image.bin --length 64      # 看前 64 字节
python tools/check_bin_file.py dump image.bin --offset -64     # 看末尾 64 字节(负数从尾部倒数)

python tools/check_bin_file.py diff image.bin app.bin          # 逐字节对比, 生成 diff_report.txt
python tools/check_bin_file.py diff a.bin b.bin ./diff_out     # 额外导出差异片段到目录

# 解析镜像后再对比(自动剥掉 version/tag/crc/aux, 加密模式先解密, 模式由镜像 meta 自描述)
python tools/check_bin_file.py image_diff image.bin app.bin --plain_b
python tools/check_bin_file.py image_diff a.bin b.bin --key 00112233445566778899aabbccddeeff --mac_key ffeeddccbbaa99887766554433221100
```

`diff` / `image_diff` 完全一致返回 0, 存在差异返回 1, 可直接用于脚本判断。

### PC 端 (test/)

PC 端用 bootutil 的 `read.c`(解析+校验+解密) + `err.c`(错误码)，与板上同一套代码。两个可执行目标：

```powershell
cmake -S test -B test/build -G Ninja
cmake --build test/build
```

需要 SHA/GCM/CBC 解密时（会一并编译 mbedtls）：

```powershell
cmake -S test -B test/build -G Ninja -DTOOL_ENABLE_CRYPTO=ON
cmake --build test/build
```

#### 镜像校验工具 pc_check.exe

校验镜像并与其明文 bin 逐字节比对：

```powershell
cd test
.\build\pc_check.exe image.bin app.bin
```

输出示例:

```
Image   : image.bin (22605 B)
Plain   : app.bin (22592 B)
Mode    : CRC (aux 0 B, overhead 13 B, 元数据在前)
Version : "1.0.0" (5 B)
Tag     : "" (0 B)
Payload : 22592 B @ offset 9
CRC32   : stored 0xbf7e1c67 / calc 0xbf7e1c67 -> MATCH
Verify  : OK (CRC 校验通过)
Compare : payload vs plain, 长度差 +0 B, 重叠区 22592 B, 不同字节 0 B
RESULT  : PASS
```

模式与 version/tag 长度从镜像末尾 meta 自描述，无需手动指定。常用选项:

| 选项 | 说明 |
|---|---|
| `--key hex` | 解密密钥, GCM/CBC/CBC_SHA 必填 |
| `--mac_key hex` | MAC 密钥(CBC_SHA), 不给则复用 `--key` |
| `--max_diff N` | 最多列出多少条差异, 默认 16 |

退出码: `0` 校验通过且一致, `1` 校验失败或存在差异, `2` 用法/环境问题。

#### PC 测试台 main.exe

单入口 + 运行时菜单，跑真实的 `start.c` / `read.c` / `flash.c` / `ota_state*.c` 代码路径（不是复刻逻辑），介质用 RAM 模拟 NOR flash（write 按位相与、erase 置 0xFF）。用 `setjmp/longjmp` 模拟「bootloader → 当前分区 app」的跳转（跳哪片由状态字 bit6 决定，boot 阶段仍真实执行 `mini_boot_state_load()`，含 pending 回滚判定）。

```powershell
cd test
.\build\main.exe
```

```
================ PC OTA 测试台 ================
  1  查看 flash 区域布局
  2  flash 读写擦除保真测试
  3  镜像解析/校验（内存构造 + 篡改检测）
  4  下载镜像测试（boot → app 开 OTA → 流式下载）
  5  OTA 全流程测试（激活/确认/回滚, setjmp 模拟跳分区）
  6  掉电与异常场景测试
  7  擦除全片 flash（恢复出厂）
  8  单步模式：开（回车逐步）
  0  退出
===============================================
```

- **单步模式（8）**：开启后流程类测试（4/5/6/7）每个关键步骤会打印一次状态快照并等回车继续，便于观察 `open / rollback / fail / current / pending` 与各分区烧录情况；输入 `q` 可让本次剩余步骤连续跑完。关闭后一键跑完、输出 PASS/FAIL，适合回归。
- 各测试项独立、可重复，退出码在全程 PASS 时为 0。

## 板级升级示例

固件侧接入只需两件事：实现平台层（flash 后端 + 状态后端），然后在 boot 侧调用 `boot_jump_switch_app()` 跳转、在 app 侧调用 `mini_boot_source_download_stream()` + `mini_boot_start_ota()` 完成下载与激活。下面单分区、双分区各一个完整流程（`hal_*` / `transport_read` / `system_reset` 为板级 HAL，需按芯片实现）。

### 平台层（单/双分区共用，每个板子实现一次）

```c
#include "flash.h"       /* flash_ops_register / flash_area_* */
#include "ota_state.h"   /* ota_state_flash_register */
#include "boot_config.h" /* OTA_DUAL_PARTITION（双分区编译期开关） */
#include "err.h"         /* ERR_* */
#include <string.h>
/* boot 侧另需 boot.h(boot_jump_switch_app)、start.h(mini_boot_state_load)；
   app 侧另需 start.h(ota_open/下载/激活/确认) */

/* 1) 分区表：单分区只留 image_0，双分区再加 image_1 */
static const flash_area_t s_areas[] = {
#if OTA_DUAL_PARTITION
    { FLASH_AREA_ID_BOOTLOADER, 0, 0x08000000u, 0x4000u  }, /* boot 16KB  */
    { FLASH_AREA_ID_IMAGE_0,    0, 0x08004000u, 0x20000u }, /* app 128KB  */
    { FLASH_AREA_ID_IMAGE_1,    0, 0x08024000u, 0x20000u }, /* app 128KB  */
    { FLASH_AREA_ID_STATE,      0, 0x08044000u, 0x1000u  }, /* 状态区独立扇区 4KB */
#else
    { FLASH_AREA_ID_BOOTLOADER, 0, 0x08000000u, 0x4000u  },
    { FLASH_AREA_ID_IMAGE_0,    0, 0x08004000u, 0x20000u },
    { FLASH_AREA_ID_STATE,      0, 0x08024000u, 0x1000u  },
#endif
};

/* 2) flash 后端：open/erase/write/read 必填，get_sectors 可选 */
static int m_open(uint32_t id, const flash_area_t **out)
{
    for (size_t i = 0; i < sizeof(s_areas) / sizeof(s_areas[0]); ++i)
    {
        if (s_areas[i].fa_id == id) { *out = &s_areas[i]; return 0; }
    }
    return ERR_NOT_SUPPORTED;
}
static int m_erase(const flash_area_t *a, uint32_t off, uint32_t len)
{
    return board_flash_erase(a->fa_offset + off, len) ? 0 : ERR_ARG;
}
static int m_write(const flash_area_t *a, uint32_t off, const void *buf, uint32_t len)
{
    return board_flash_program(a->fa_offset + off, buf, len) ? 0 : ERR_ARG;
}
static int m_read(const flash_area_t *a, uint32_t off, void *buf, uint32_t len)
{
    memcpy(buf, (const void *)(a->fa_offset + off), len);
    return 0;
}
static const flash_ops_t s_ops = { m_open, m_erase, m_write, m_read, NULL };

/* 3) 平台初始化：boot 与 app 各自启动时各调一次 */
void board_ota_init(void)
{
    flash_ops_register(&s_ops);
    ota_state_flash_register(); /* 内置 flash 状态后端；也可 ota_state_ops_register 自定义 */
}
```

### 单分区示例

单分区没有 A/B 与回滚：下载目标固定 `image_0`（覆盖当前镜像），boot 永远跳 `image_0`。适合资源紧张、能安全擦写自身分区的场景（如跳回 boot 代理下载，或从 RAM 执行升级逻辑）。

```c
/* ------- bootloader 侧 ------- */
void boot_main(void)
{
    board_ota_init();
    mini_boot_state_load(); /* 恢复持久位（单分区无 pending 回滚） */

    mini_boot_app_area_t app = { IMAGE_0_ADDR, IMAGE_0_SIZE };
    boot_jump_switch_app(app); /* 校验向量表后跳转，不返回 */
    for (;;) {}
}

/* ------- app 侧 ------- */
static int dl_hook(void *param, uint8_t *buf, uint32_t want, int *out_len)
{
    *out_len = transport_read(buf, want); /* 从网络/串口读最多 want 字节 */
    return (*out_len > 0) ? 0 : ERR_TRANSMIT;
}

void app_upgrade(uint32_t fw_len)
{
    ota_open();
    if (mini_boot_source_download_stream(dl_hook, NULL, fw_len) != 0) return;
    if (mini_boot_start_ota() != 0) return; /* 校验刚写入 image_0 的镜像 */
    system_reset(); /* 复位进 boot，boot 校验并运行新固件 */
}
```

### 双分区示例

双分区支持 A/B 升级：下载到非当前分区，激活后切换 `current` 并置 `pending`，配合回滚保护。

```c
/* ------- bootloader 侧 ------- */
void boot_main(void)
{
    board_ota_init();
    mini_boot_state_load(); /* pending 未确认则回滚到上一分区 */

    uint32_t cur = ota_current_partition_get();
    mini_boot_app_area_t app = (cur == OTA_STATE_PARTITION_IMAGE_1)
        ? (mini_boot_app_area_t){ IMAGE_1_ADDR, IMAGE_1_SIZE }
        : (mini_boot_app_area_t){ IMAGE_0_ADDR, IMAGE_0_SIZE };
    boot_jump_switch_app(app);
    for (;;) {}
}

/* ------- app 侧 ------- */
static int dl_hook(void *param, uint8_t *buf, uint32_t want, int *out_len)
{
    *out_len = transport_read(buf, want);
    return (*out_len > 0) ? 0 : ERR_TRANSMIT;
}

void app_upgrade(uint32_t fw_len)
{
    ota_open();
    ota_rollback_open(); /* 双分区 + 回滚：激活后置 pending */

    if (mini_boot_source_download_stream(dl_hook, NULL, fw_len) != 0) return;
    if (mini_boot_start_ota() != 0) return; /* 校验 + 切 current + 置 pending */

    system_reset(); /* 复位后 boot 跳到新分区 */
}

/* 新镜像内，自检通过后确认（清 pending，下次复位不再回滚） */
void app_confirm_after_self_test(void)
{
    if (mini_boot_confirm_ota() == 0)
    {
        
    }
}
```

> 说明
> - 双分区回滚依赖 `mini_boot_confirm_ota()`：新镜像跑起来没确认就复位，boot 的 `mini_boot_state_load()` 会切回旧分区并记失败码。
> - 需要单独校验某个分区里的镜像是否完好，可用 `mini_boot_backup()`（内部复用 `read.c` 的 `image_verify_stream()`，模式/摘要/nonce/iv/tag 都由镜像自描述）。
> - 跳转前想顺手整包校验（挡位翻转/误擦写）：给 `mini_boot_app_area_t` 填上 `fa_id` 与 `image_len`（镜像实际长度），`boot_jump_switch_app()` 内部会调 `mini_boot_backup()`；两者留 0 就只做原来的向量表校验。
> - app 分区地址 `IMAGE_x_ADDR/SIZE`、向量表校验依赖的 `SRAM_START_ADDR/SRAM_SIZE` 由板级链接脚本 / 配置提供（见 `boot_config.h`，需通过 `config.h` 或 `-D` 注入）。
> - `OTA_DUAL_PARTITION` 是编译期能力开关：双分区工程 `-DOTA_DUAL_PARTITION=1`，单分区保持默认 0。

## 镜像布局

打包端 `tools/main.py` 生成、设备端 `read.c` 解析的格式如下(数字均为小端):

```
元数据在后 is_front=0(默认):
  payload | aux | version | tag | crc32(4B) | meta(4B)

元数据在前 is_front=1(--is_front):
  crc32(4B) | version | tag | aux | payload | meta(4B)
```

- `payload`: 原始固件; 加密模式下是密文(此时 crc 对密文计算)
- `version` / `tag`: 变长字符串, 长度(0~255)记在 meta 里
- `aux`: 附加数据, 长度由模式决定(见下表)
- `crc32`: 4B 小端
- `meta`: 恒在镜像最后 4B, 自描述(见下表)

### meta(4B, 恒在末尾)

| 偏移 | 含义 |
|---|---|
| 0 | 魔数 `0xA5` |
| 1 | bit7 = `is_front`; bit0~6 = 模式编号 |
| 2 | `version` 长度 |
| 3 | `tag` 长度 |

### 模式编号与 aux

| 模式 | 编号 | aux 布局 | 设备端流式处理(`image_verify_stream`) |
|---|---|---|---|
| CRC | 0 | 空 | 对 payload 算 CRC32 与 `crc32` 比对 |
| SHA | 1 | sha256(32B) | 对 payload 算 SHA-256 与 aux 比对 |
| GCM | 2 | nonce(12B) + tag(16B) | AES-GCM 流式解密(明文即弃)并验 tag |
| CBC | 3 | iv(16B) | 无认证属性, 仅布局检查, 恒通过 |
| CBC_SHA | 4 | iv(16B) + hmac(32B) | HMAC 覆盖 `iv‖密文` 并比对(verify-then-decrypt 的验证段) |

> `image_read_payload()`(一次性接口)对 CBC/CBC_SHA 会真正解密并做 PKCS#7 去填充;
> 这里列的是 `image_verify_stream()` 流式路径的行为。

> 解析器只靠镜像字节 + **总长度**工作: `meta` 从 `buf[len-4]` 读起。 所以校验时传入的长度必须是
> 镜像实际长度(含 meta), 不能是分区/槽大小; 传错会找不到 meta 直接报 `ERR_ARG`。
> `mini_boot_backup()` 与 `boot_jump_switch_app()` 的整包校验都遵循这一点。

## 打包与校验的参数必须一致

| 打包端 (tools/main.py) | 校验端 |
|---|---|
| `--check --version --tag --is_front` | 已写入镜像 meta, 自动读取, 无需配置 |
| `--key` | `--key` / `cfg.key` |
| `--mac_key` | `--mac_key` / `cfg.mac_key` |
| `--crc_init --crc_refin --crc_refout --crc_xor_out --crc_poly` | `boot_config.h` 的 `CRC_MODEL_*` |

板上改 CRC 模型只需改 `bootutil/inc/boot_config.h`, 不用动 CMake:

```c
#define CRC_MODEL_POLY      0x04C11DB7u   /* 改成与打包一致的值 */
```

## 常见问题

**1. `ModuleNotFoundError: No module named 'm_crc.image_crc'`**
脚本按脚本所在目录导入 `tools/m_crc` 包, 从任意目录运行 `python tools/main.py` 都可以, 不需要设 `PYTHONPATH`。

**2. 校验不过 (crc mismatch / sha256 mismatch / authentication failed)**
先核对上表: 密钥是否与打包一致; CRC 模型是否与 `boot_config.h` 一致。
`invalid argument` 解析失败则先看末尾 4B meta 是否完整——镜像被截断(比如下载不完整)会先在这里报错。

**3. 编译报 `undefined reference to sha256_begin / aes_gcm_decrypt_stream_begin`**
开了 `IMAGE_CRYPTO_ENABLE`(默认 1) 却没有把 `algorithm/src` 下的 `aes.c`、`hmac.c`、`sha.c` 和 mbedtls 一起编译链接。用 `test/CMakeLists.txt` 构建即可自动处理; 关掉加密则只依赖 `crc.c`。

**4. mbedtls 报 `void*` 隐式转换 / `jump to label`**
mbedtls 只能用 C 编译器编译, 不要混进 g++ 命令。`test/CMakeLists.txt` 里 `project(pc_test C CXX)`, `.c` 自动走 C 编译器。
