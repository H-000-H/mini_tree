# Linux 设备模型 vs mini_tree 对照 / Linux Device Model vs mini_tree — Comparison

> 用途：以 Linux 设备模型为主线，说明 mini_tree 的 Device Tree + Driver Model 与之的对应关系与关键差异。不要求通读 Linux 源码，重点在"机制对照"而非"内核实现细节"。
> Purpose: map mini_tree's Device Tree + Driver Model onto the Linux device model, focusing on mechanism comparison rather than kernel internals.

---

## 1. 一条主线：从用户到硬件 / One Chain: User to Hardware

两者描述硬件的抽象层级一致，自上而下都是"用户 → 文件 → 设备 → 总线 → 子系统 → 资源 → 设备树 → 硬件"：

```
用户 / Userspace
  ↓ 系统调用 / syscall
VFS（虚拟文件 / file）
  ↓
字符设备 / cdev + file_operations
  ↓
设备模型 / device ↔ device_driver（probe/remove）
  ↓
总线 / platform / spi / i2c ...
  ↓
子系统 / SPI core / MTD / input ...
  ↓
资源 / devm_ioremap / clk / gpio / irq
  ↓
设备树 / device_node（静态硬件描述）
  ↓
硬件 / MMIO 寄存器 · 中断 · DMA
```

差异不在"层级"，而在"每层是运行时动态构建还是编译期静态生成"。

---

## 2. 层级对照表 / Layer Comparison

| 层级 / Layer | Linux | mini_tree | 关键区别 / Difference |
| ------- | -------------------------------- | ------------------------------- | ---------------------------- |
| L0 用户 / Userspace | `open/read` on `/dev` | `device_open/read` | mini_tree 无 `/dev` 文件系统节点 |
| L1 VFS | `struct file` / `inode` | `dev_lifecycle` | 无 inode，生命周期由 `dev_lifecycle` 跟踪 |
| L2 cdev | `struct cdev` + `dev_t` | `dev->ops`（无 `dev_t`） | 无主次设备号，靠 `device_id_t` 句柄 |
| L3 core | `device` + `driver` + `bus_type` | `s_devices` + `DRIVER_REGISTER` | 全量静态、无运行时 `device_add` |
| L4 bus | `spi_master` / `platform_device` | `bus_controller/client` | 总线描述静态写在 DTS |
| L5 子系统 / subsystem | MTD / input / misc | `fft_spi_drv` / `ws2812_drv` | 无统一子系统层，驱动即服务 |
| L6 资源 / resources | `devm_*` | HAL + DTS props | 资源不随 device 自动释放 |
| L7 DT | `device_node` + DTB | `s_nodes`（dtc-lite） | 编译期生成 C 数组，无运行时 unflatten |
| L8 HW | SoC 驱动 / SoC drivers | ESP-IDF HAL | 硬件访问走厂商 HAL |

---

## 3. 关键概念对照 / Key Concept Mapping

| 概念 / Concept | Linux | mini_tree |
| ----- | ------------------------ | ------------------- |
| 静态 DT / static DT | `struct device_node` | `s_nodes[]` |
| 运行时设备 / runtime device | `struct device` | `s_devices[]` |
| 驱动 / driver | `struct device_driver` | `DRIVER_REGISTER` 宏注册 |
| 总线 / bus | `struct bus_type` | `bus_type` / `bus.c` |
| 字符设备 / char device | `struct cdev` | `dev->ops` |
| 文件实例 / file instance | `struct file` | `dev_lifecycle` |
| fops | `struct file_operations` | 同名结构 / same name |
| 私有数据 / private data | `dev_set_drvdata` | `device_set_priv` |
| 父设备 / parent device | `dev->parent` | `node->deps[0]` |
| 子设备枚举 / child enumeration | `device_for_each_child` | `board_cascade_get` |
| 设备号 / device number | `dev_t` | `device_id_t` |
| sysfs | `kobject` | 无 / none |
| 资源 / resources | `devm_*`（与 device 绑定自动释放） | 手动 + HAL / manual + HAL |
| 查找设备 / lookup | `device_find_*` / `of_find_*` | `device_find`（返回 `ERR_PTR`） |

---

## 4. 机制要点：mini_tree 怎么做 / How mini_tree Works

### 4.1 编译期静态生成（对应 Linux 运行时注册）
- Linux：`device_add` / `driver_register` 在运行时把对象挂进总线，支持热插拔与动态匹配。
- mini_tree：`dtc-lite` 在编译期把 DTS 解析成 `s_nodes[]`（节点）与 `s_devices[]`（设备），全量静态，启动即就绪，无运行时创建、无热插拔。
- 代价：板级硬件必须写字面量 DTS；收益：零运行时分配、确定性的启动顺序。

### 4.2 手动 probe（对应 Linux 总线自动匹配）
- Linux：总线在 `driver` 注册时遍历 `device`，按 `of_match_table` / `id_table` 自动 `probe`。
- mini_tree：无自动匹配。由 `board_driver_probe_all()` 统一触发；它先按 `deps`/`cascade` 做 **3 趟（3-pass）依赖解析**，再按析出的顺序逐个调用 `probe`。
- 顺序由 DTS 的依赖声明决定，而非注册先后，因此驱动无需关心加载次序。

### 4.3 deps vs cascade（对应 Linux parent / child）
- `deps`：子 → 父依赖。`device_get_parent()` 顺着 `node->deps[0]` 找到父设备，父未就绪则子不 probe。
- `cascade`：父 → 子枚举。`board_cascade_get()` 让父设备遍历其下的子设备（如 SPI 控制器枚举挂在其总线上的 SPI 从设备）。
- 二者方向相反、互补：probe 阶段用 `deps` 保证父先于子，运行阶段用 `cascade` 做子设备发现。

### 4.4 错误处理（对应 Linux `ERR_PTR` 体系）
- mini_tree 同样用 `ERR_PTR` / `IS_ERR` / `PTR_ERR`：`device_find` 失败时返回编码了负错误码的指针，调用方**必须**用 `IS_ERR` 判错后再解引用，不能直接当有效指针用（见 `service_spec.md`）。
- 与 Linux 一致，但 mini_tree 不提供 `IS_ERR_OR_NULL`，区分"未找到"（错误指针）与"空"需自行约定。

### 4.5 资源管理（对应 Linux `devm_*`）
- Linux：`devm_ioremap` / `devm_clk_get` 等资源与 `device` 绑定，device 释放时自动回收，避免泄漏。
- mini_tree：资源（寄存器映射、时钟、GPIO、IRQ）由 HAL + DTS 属性显式获取，不随设备自动释放；驱动 `remove` 时须手动归还。这是刻意为之——静态设备生命周期等同固件运行期，无"设备消失"概念。

### 4.6 无 sysfs / 无用户态文件系统
- Linux 通过 `sysfs`（`kobject`）把设备树暴露给用户态，支持 `echo` 调参、`/dev` 节点、`uevent`。
- mini_tree 不暴露用户态文件系统；调试依赖 `compile_commands.json`（clangd 跳转）+ 日志。cdev 的 `dev->ops` 仅在内核态（固件态）被调用，无 `/dev/<name>` 节点。

---

## 5. 一个最小对照示例 / Minimal Example

以 SPI 从设备为例，说明两侧"同构不同实现"：

| 步骤 / Step | Linux | mini_tree |
| --- | --- | --- |
| 硬件描述 / HW desc | `spi_board_info` 或 DTS `spi{}` | `board/dts/*.dts` 的 `spi` 节点 |
| 控制器 / controller | `spi_master` | `bus_controller`（SPI） |
| 从设备 / client | `spi_device` + `spi_driver.probe` | `bus_client` + `DRIVER_REGISTER` 的 `probe` |
| 数据传输 / transfer | `spi_transfer` / `spi_sync` | `dev->ops` 中的传输回调 |
| 暴露 / expose | `/dev/spidev*` via `spidev` | 无 `/dev`，由 `device_open` 句柄访问 |

---

## 6. 建议阅读顺序（本仓）/ Reading Order (this repo)

| 顺序 / Order | 文件 / File | 对照 Linux / Linux counterpart |
| --- | -------------------------------------- | --------------------- |
| 1 | `board/dts/*.dts` | DTS |
| 2 | `build/generated/.../board_devtable.c` | unflattened DT |
| 3 | `build/generated/.../board_probe.c` | probe order + cascade |
| 4 | `board/src/board_driver.c` | `device_attach` 等价 / equivalent |
| 5 | `vfs/spi/spi_bus.c` | `spi_master` |
| 6 | `vfs/spi/spi_client.c` | `spi_device` + fops |

---

## 7. 参考（深入 Linux 时）/ References (for deep Linux study)

- [Driver model overview](https://www.kernel.org/doc/html/latest/driver-api/driver-model/index.html)
- [Platform devices](https://www.kernel.org/doc/html/latest/driver-api/driver-model/platform.html)
- [SPI subsystem](https://www.kernel.org/doc/html/latest/driver-api/spi.html)
- [Device Tree usage](https://www.kernel.org/doc/html/latest/devicetree/usage-model.html)
