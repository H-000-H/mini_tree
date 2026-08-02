# Linux 设备模型 vs mini_tree 对照学习指南 / Linux Device Model vs mini_tree — A Comparative Study Guide

> 用途：以 **Linux 内核为主线** 深入学习 device 体系，每节末尾简要对照 mini_tree。
> Purpose: study the device model deeply with the **Linux kernel as the main thread**, then briefly compare mini_tree at the end of each section.
> 建议学习顺序：**自上而下**（用户 → VFS → cdev → 设备模型 → 总线 → 子系统 → 资源 → DT → 硬件）。
> Suggested order: **top-down** (user → VFS → cdev → device model → bus → subsystem → resources → DT → hardware).

---

## 1. 总览：从用户到硬件的一条链 / Overview: One Chain from User to Hardware

```
用户: open("/dev/spidev0") / read / ioctl
  ↓ 系统调用
VFS:  struct file → struct inode → f_op / cdev
  ↓
字符设备: struct cdev + file_operations
  ↓
设备模型: struct device ↔ struct device_driver (probe/remove)
  ↓
总线:   platform_bus / spi_bus / i2c_bus ...
  ↓
子系统: SPI core / MTD / input / regmap ...
  ↓
资源:   devm_ioremap / devm_request_irq / clk / gpio / pinctrl
  ↓
设备树: struct device_node (of_node) — 硬件静态描述
  ↓
硬件:   MMIO 寄存器 / GIC 中断 / DMA 引擎
```

### 层级对照表（mini_tree 速查）/ Layer Comparison Table (mini_tree Cheat Sheet)

| 层级 / Layer | Linux | mini_tree |
| ------- | -------------------------------- | ------------------------------- |
| L0 用户 / Userspace | `open/read` on `/dev` | `device_open/read` |
| L1 VFS | `struct file` / `inode` | `dev_lifecycle` |
| L2 cdev | `struct cdev` + `dev_t` | `dev->ops`（无 dev_t / no dev_t） |
| L3 core | `device` + `driver` + `bus_type` | `s_devices` + `DRIVER_REGISTER` |
| L4 bus | `spi_master` / `platform_device` | `bus_controller/client` |
| L5 子系统 / subsystem | MTD / input / misc | `fft_spi_drv` / `ws2812_drv` |
| L6 资源 / resources | `devm_*` | HAL + DTS props |
| L7 DT | `device_node` + DTB | `s_nodes`（dtc-lite） |
| L8 HW | SoC 驱动 / SoC drivers | ESP-IDF HAL |

---

## 2. Linux 各级详解 / Linux Layer-by-Layer

---

### L0 — 用户空间 / Userspace

#### 2.0.1 用户看到什么 / What the User Sees

- **字符设备节点 / character device nodes**：`/dev/ttyS0`、`/dev/spidev0`、`/dev/input/event0`
- **块设备节点 / block device nodes**：`/dev/sda`、`/dev/mmcblk0`
- **sysfs 属性 / sysfs attributes**：`/sys/class/spi_master/spi0/`、`/sys/devices/platform/...`
- **debugfs**（可选 / optional）：驱动调试接口 / driver debug interface

#### 2.0.2 典型系统调用链 / Typical Syscall Chain

```c
// 用户程序
int fd = open("/dev/mydev", O_RDWR);
read(fd, buf, len);
ioctl(fd, CMD, &arg);
close(fd);
```

```
libc open()
  → syscall __x64_sys_openat / arm64 sys_openat
    → do_sys_open()                    [fs/open.c]
      → path_openat()
        → vfs_open()
          → do_dentry_open()
            → chrdev_open()            [fs/char_dev.c]  （字符设备）
              → cdev->ops->open()      驱动 fops
```

`read` 类似：`vfs_read` → `file->f_op->read` 或 `read_iter`。
`read` is analogous: `vfs_read` → `file->f_op->read` or `read_iter`.

#### 2.0.3 设备号（dev_t）/ Device Numbers (dev_t)

```c
// include/linux/kdev_t.h
// dev_t = 32bit: major(12) + minor(20)
MKDEV(major, minor);
MAJOR(dev); MINOR(dev);
```

- **major**：决定哪个驱动处理 open / which driver handles open
- **minor**：同一驱动下的第几个实例 / which instance of the same driver
- 用户态 / userspace：`ls -l /dev/spidev0` 可见 / shows `crw-rw---- 1 root spi 153, 0`

#### mini_tree 对照 / mini_tree Comparison

无 `/dev`、无 `dev_t`；应用持有 `struct device*` 或 `DEV_ID_xxx`，直接调 `device_open/read`。
No `/dev`, no `dev_t`; the app holds a `struct device*` or `DEV_ID_xxx` and calls `device_open/read` directly.

---

### L1 — VFS（虚拟文件系统）/ VFS (Virtual File System)

#### 2.1.1 核心数据结构 / Core Data Structures

`**struct inode**`（`include/linux/fs.h`）— 文件的「档案」/ the file's "record":

| 字段 / Field | 含义 / Meaning |
| -------- | ---------------------- |
| `i_rdev` | 字符/块设备的 dev_t / dev_t of a char/block device |
| `i_fop` | 文件操作（部分设备直接用）/ file operations (some devices use directly) |
| `i_cdev` | 指向 `struct cdev`（字符设备）/ points to `struct cdev` (char device) |
| `i_mode` | 文件类型（S_IFCHR 等）/ file type (S_IFCHR etc.) |

`**struct file**` — 每次 `open()` 一个实例 / one instance per `open()`:

| 字段 / Field | 含义 / Meaning |
| -------------- | --------------------------- |
| `f_op` | 本次 open 使用的 fops / the fops used by this open |
| `f_inode` | 关联 inode / associated inode |
| `f_pos` | 读写偏移 / read/write offset |
| `private_data` | 驱动在 open 里设置，供 read/write 用 / set by the driver in open, used by read/write |

**要点**：同一 `/dev/foo` 可被多个进程 open，每个 fd 对应不同的 `struct file`，可有不同 `private_data` 和 `f_pos`。
**Key point**: the same `/dev/foo` can be opened by multiple processes; each fd has its own `struct file` with its own `private_data` and `f_pos`.

#### 2.1.2 dentry 与 path

```
路径 /dev/spidev0
  → path_lookupat → 找到 dentry
    → d_inode → inode
      → chrdev_open 根据 i_rdev 找 cdev
```

相关源码 / Related source：`fs/namei.c`、`fs/dcache.c`。

#### 2.1.3 必读函数 / Must-Read Functions

| 函数 / Function | 文件 / File | 作用 / Purpose |
| ------------------------ | ----------------- | -------------- |
| `do_sys_open` | `fs/open.c` | 系统调用入口 / syscall entry |
| `vfs_read` / `vfs_write` | `fs/read_write.c` | 读写分发 / read/write dispatch |
| `chrdev_open` | `fs/char_dev.c` | 字符设备 open / char device open |
| `def_chr_fops` | `fs/char_dev.c` | 默认 fops，转 cdev / default fops, forwards to cdev |

#### mini_tree 对照 / mini_tree Comparison

无 `struct file`；`dev_lifecycle.opens` 做引用计数，`device_open` 持设备锁后调 `dev->ops->open`。
No `struct file`; `dev_lifecycle.opens` does refcounting, and `device_open` calls `dev->ops->open` while holding the device lock.

---

### L2 — 字符设备（cdev）/ Character Devices (cdev)

#### 2.2.1 为什么需要 cdev / Why cdev Is Needed

设备模型（L3）管「设备存在、驱动绑定」；**cdev 管「用户怎么通过文件接口访问」**。二者分离：一个 platform 设备可以没有 cdev（纯内核内部用），也可以再注册 cdev 给用户。
The device model (L3) manages "device exists, driver bound"; **cdev manages "how users access it through the file interface"**. They are separate: a platform device may have no cdev (kernel-internal only), or may additionally register a cdev for users.

#### 2.2.2 关键结构 / Key Structures

```c
// include/linux/cdev.h
struct cdev {
    struct kobject kobj;
    struct module *owner;
    const struct file_operations *ops;
    dev_t dev;          // 完整 dev_t（含 major+minor）
    unsigned int count; // 连续 minor 数量
};

// include/linux/fs.h（精简）
struct file_operations {
    struct module *owner;
    loff_t (*llseek)(struct file *, loff_t, int);
    ssize_t (*read)(struct file *, char __user *, size_t, loff_t *);
    ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *);
    long (*unlocked_ioctl)(struct file *, unsigned int, unsigned long);
    int (*open)(struct file *, struct inode *);
    int (*release)(struct file *);   // close 时
    __poll_t (*poll)(struct file *, struct poll_table_struct *);
    ...
};
```

#### 2.2.3 注册流程（现代写法）/ Registration Flow (Modern Style)

```c
// 1. 申请设备号
alloc_chrdev_region(&devno, 0, 1, "mydev");

// 2. 初始化 cdev
struct cdev my_cdev;
cdev_init(&my_cdev, &my_fops);
my_cdev.owner = THIS_MODULE;

// 3. 加入内核 cdev 表
cdev_add(&my_cdev, devno, 1);

// 4. 创建设备节点（udev/mdev 会在 /dev 下建文件）
device_create(my_class, NULL, devno, NULL, "mydev");
// → /dev/mydev
```

卸载顺序相反：`device_destroy` → `cdev_del` → `unregister_chrdev_region`。
Teardown is the reverse: `device_destroy` → `cdev_del` → `unregister_chrdev_region`.

#### 2.2.4 open 时如何找到驱动 / How open Finds the Driver

```
chrdev_open(inode, file)
  → i_cdev 或 根据 i_rdev 查 kobj_map
    → cdev->ops 赋给 file->f_op
      → 调 file->f_op->open(file, inode)
        → 驱动里 filp->private_data = xxx;
```

#### 2.2.5 其他字符设备注册方式 / Other cdev Registration Styles

| 方式 / Method | API | 适用 / Use case |
| ------- | ----------------- | ------------ |
| 标准 cdev / standard cdev | `cdev_add` | 多数驱动 / most drivers |
| misc 设备 / misc device | `misc_register` | 单 minor、简单驱动 / single minor, simple drivers |
| 老接口 / legacy interface | `register_chrdev` | 已不推荐 / deprecated |

misc 示例 / Example：`drivers/char/misc.c`，自动分配 minor / auto-allocates a minor.

#### mini_tree 对照 / mini_tree Comparison

不拆 cdev；`struct device.ops` 即 fops，无 major/minor、无 `/dev`。
No separate cdev; `struct device.ops` is the fops, with no major/minor and no `/dev`.

---

### L3 — 设备模型核心（driver core）★ 最重要 / Device Model Core (driver core) ★ Most Important

#### 2.3.1 三大结构 / The Three Structures

`**struct bus_type**`（`include/linux/device/bus.h`）— 总线类型 / bus type:

```c
struct bus_type {
    const char *name;
    int (*match)(struct device *dev, struct device_driver *drv);
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    ...
};
```

常见 / Common：`platform_bus_type`、`spi_bus_type`、`i2c_bus_type`、`amba_bustype`。

`**struct device_driver**` — 驱动 / driver:

```c
struct device_driver {
    const char *name;
    struct bus_type *bus;
    const struct of_device_id *of_match_table;  // DT 匹配表
    int (*probe)(struct device *dev);
    void (*remove)(struct device *dev);
    ...
};
```

`**struct device**` — 设备实例 / device instance:

```c
struct device {
    struct device *parent;
    struct device_driver *driver;
    struct bus_type *bus;
    struct device_node *of_node;   // DT 节点
    void *platform_data;
    void *driver_data;             // 驱动私有数据
    struct kobject kobj;           // sysfs 基础
    ...
};
```

#### 2.3.2 kobject 与 sysfs（用户可见的设备模型）/ kobject & sysfs (the User-Visible Device Model)

每个 `struct device` 内嵌 `kobject`：
Every `struct device` embeds a `kobject`:

- 在 `/sys/devices/` 下出现目录 / a directory appears under `/sys/devices/`
- `uevent` 通知 udev 创建设备节点 / `uevent` notifies udev to create device nodes
- 属性 / attributes：`/sys/.../uevent`、`/sys/.../modalias`

学习时可配合 / Try alongside：`ls /sys/bus/spi/devices/`、`ls /sys/bus/platform/devices/`。

#### 2.3.3 注册与 probe 完整流程 / Registration & Full Probe Flow

**驱动侧（先注册 driver，等待 device）/ Driver side (register first, wait for device):**

```c
static struct platform_driver my_driver = {
    .probe  = my_probe,
    .remove = my_remove,
    .driver = {
        .name = "my-dev",
        .of_match_table = my_of_match,
    },
};
module_platform_driver(my_driver);
// 展开为 platform_driver_register()
```

**设备侧（内核或驱动创建 device 并注册）/ Device side (kernel or driver creates and registers a device):**

```c
platform_device_register(&pdev);
// 或 DT 自动创建 platform_device
```

**内核内部调度（`drivers/base/dd.c`）/ Kernel-internal dispatch (`drivers/base/dd.c`):**

```
device_add(dev)                         [core.c]
  → bus_probe_device(dev)
    → device_attach(dev)
      → bus_for_each_drv(..., __device_attach_driver)
        → driver_match_device(drv, dev)   // bus->match
          → driver_probe_device(drv, dev)
            → drv->probe(dev)             // 你的 probe
              成功: dev->driver = drv
              失败: 负 errno，可能 EPROBE_DEFER
```

**EPROBE_DEFER**：依赖（clock/regulator）未就绪时 probe 返回此错误，内核稍后重试。
**EPROBE_DEFER**: when a dependency (clock/regulator) is not ready, probe returns this error and the kernel retries later.

#### 2.3.4 匹配（match）怎么工作 / How Matching Works

**Platform 总线**（`drivers/base/platform.c`）/ Platform bus:

```c
// 优先 OF 匹配
of_driver_match_device(dev, drv);
// of_match_table 与 dev->of_node->compatible 比较

// 或 id_table 匹配（非 DT 平台）
```

**OF 匹配表示例 / OF match table example:**

```c
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-ip", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);
```

#### 2.3.5 私有数据约定 / Private-Data Conventions

```c
// probe 成功时
platform_set_drvdata(pdev, priv);
// 或 dev_set_drvdata(&pdev->dev, priv);

// 其他函数
priv = platform_get_drvdata(pdev);
// 或 dev_get_drvdata(dev);
```

#### 2.3.6 remove 与 devm 对称 / remove & devm Symmetry

```
device_unregister(dev) / driver_unregister
  → bus->remove(dev)
    → drv->remove(dev)
      → 释放非 devm 资源
      → devm_* 自动释放（与 dev 绑定）
```

probe 失败时内核也会调用已注册资源的 cleanup（devm 优势）。
On probe failure the kernel also runs cleanup for already-registered resources (the devm advantage).

#### 2.3.7 必读源码清单 / Must-Read Source List

| 优先级 / Priority | 文件 / File | 关注函数 / Focus functions |
| --- | ------------------------- | ----------------------------------------------- |
| ★★★ | `drivers/base/core.c` | `device_add`, `device_del`, `device_initialize` |
| ★★★ | `drivers/base/dd.c` | `driver_probe_device`, `device_attach` |
| ★★★ | `drivers/base/bus.c` | `bus_register`, `bus_add_driver` |
| ★★ | `drivers/base/platform.c` | `platform_device_register`, `platform_match` |
| ★★ | `drivers/base/driver.c` | `driver_register` |
| ★ | `drivers/base/devres.c` | `devm_*` 实现 / implementation |

#### mini_tree 对照 / mini_tree Comparison

| Linux | mini_tree |
| --------------------- | ---------------------------------------- |
| `struct device` | `s_devices[]` |
| `of_node` | `dev->node` → `s_nodes[]` |
| `driver_data` | `priv_data` |
| `device_driver.probe` | `board_probe_get_fn` + `DRIVER_REGISTER` |
| `device_add` 触发 probe / triggers probe | `board_driver_probe_all()` 手动遍历 / manual iteration |

---

### L4 — 总线层（Bus）/ The Bus Layer

#### 2.4.1 platform 总线（SoC 内置 IP）/ platform Bus (SoC On-Chip IP)

**适用**：无独立物理总线的片上外设（UART、SPI 控制器、GPIO 控制器）。
**Applies to**: on-chip peripherals without an independent physical bus (UART, SPI controllers, GPIO controllers).

**两种注册 device 的方式 / Two ways to register a device:**

1. **设备树（主流）/ Device tree (mainstream)**：内核 boot 时根据 DT 自动创建 `platform_device` / the kernel auto-creates `platform_device`s from DT at boot
2. **代码注册 / code registration**：`platform_device_register(&foo_pdev)`（老平台或非 DT）/ legacy or non-DT platforms

**DTS 示例 / DTS example:**

```dts
uart0: serial@40001000 {
    compatible = "vendor,uart";
    reg = <0x40001000 0x1000>;
    interrupts = <GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&clk_uart>;
    status = "okay";
};
```

**驱动 / Driver:**

```c
static int uart_probe(struct platform_device *pdev)
{
    struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    void __iomem *base = devm_ioremap_resource(&pdev->dev, res);
    int irq = platform_get_irq(pdev, 0);
    devm_request_irq(&pdev->dev, irq, uart_isr, 0, "uart", priv);
    ...
}
```

#### 2.4.2 SPI 总线 ★ 与本仓库最相关 / SPI Bus ★ Most Relevant to This Repo

**三个核心结构（`include/linux/spi/spi.h`）/ Three core structures:**

```c
struct spi_master {
    struct device dev;           // 嵌入 device，挂 spi_bus
    s16 bus_num;
    unsigned int num_chipselect;
    int (*transfer)(struct spi_master *master, struct spi_message *msg);
    ...
};

struct spi_device {
    struct device dev;
    struct spi_master *master;
    u32 max_speed_hz;
    u8 chip_select;
    u8 mode;                     // SPI_MODE_0..3
    ...
};

struct spi_driver {
    struct device_driver driver;
    int (*probe)(struct spi_device *spi);
    void (*remove)(struct spi_device *spi);
};
```

**SPI 控制器驱动 probe 流程 / SPI controller driver probe flow:**

```
1. platform_driver probe (SoC SPI IP)
2. 初始化寄存器、时钟、DMA
3. spi_alloc_master() / devm_spi_alloc_master()
4. master->transfer = xxx_transfer
5. spi_register_master(master)
   → 注册 master 设备
   → of_register_spi_devices() 扫描 DT 子节点
   → 为每个子节点 spi_alloc_device + spi_add_device
```

**SPI 从设备驱动 / SPI slave device driver:**

```c
static int flash_probe(struct spi_device *spi)
{
    struct spi_message msg;
    struct spi_transfer xfer = {
        .tx_buf = cmd,
        .len = len,
    };
    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);
    spi_sync(spi, &msg);
}
static struct spi_driver flash_driver = {
    .probe = flash_probe,
    .driver = { .of_match_table = ... },
};
module_spi_driver(flash_driver);
```

**关键 API / Key APIs:**

| API | 作用 / Purpose |
| ------------------------ | ------------------- |
| `spi_register_master` | 注册控制器 / register the controller |
| `spi_add_device` | 手动添加从设备 / manually add a slave device |
| `spi_sync` / `spi_async` | 同步/异步传输 / sync/async transfer |
| `spi_write` / `spi_read` | 简化封装 / simplified wrappers |
| `spi_setup` | 应用 spi_device 配置到硬件 / apply spi_device config to hardware |

#### 2.4.3 I2C 总线（扩展）/ I2C Bus (Extension)

| SPI | I2C |
| ------------ | ------------- |
| `spi_master` | `i2c_adapter` |
| `spi_device` | `i2c_client` |
| `spi_driver` | `i2c_driver` |
| 片选 + 模式 / chip-select + mode | 7/10bit 地址 / address |

#### 2.4.4 子设备枚举：Linux vs DT / Child Device Enumeration: Linux vs DT

Linux SPI：**master 注册后** 内核读 DT 子节点自动创建 `spi_device`：
Linux SPI: **after master registration** the kernel reads DT child nodes and auto-creates `spi_device`s:

```dts
spi@0 {
    compatible = "vendor,spi";
    #address-cells = <1>;
    #size-cells = <0>;
    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;                    // chip select
        spi-max-frequency = <50000000>;
    };
};
```

父找子 / parent→child：`device_for_each_child(&master->dev, ...)`
子找父 / child→parent：`spi->dev.parent` 或 / or `to_spi_master(dev->parent)`。

#### mini_tree 对照 / mini_tree Comparison

| Linux | mini_tree |
| ----------------------- | ----------------------------------- |
| `spi_master` | `spi_bus.c` + `bus_controller_bind` |
| `spi_device` | 子设备 `s_devices`（如 FFT）/ child `s_devices` (e.g. FFT) |
| DT 子节点枚举 / DT child enumeration | 编译期 `deps` + `cascade` / compile-time `deps` + `cascade` |
| `device_for_each_child` | `board_cascade_get` |

---

### L5 — 子系统 / 类驱动 / Subsystems & Class Drivers

#### 2.5.1 分层思想 / The Layering Idea

```
总线驱动 (spi_driver)     ← 负责和芯片对话（寄存器、协议）
    ↓
子系统 (MTD / input)      ← 抽象成统一 API
    ↓
导出给用户 (cdev / evdev) ← /dev/mtd0, /dev/input/event0
```

Bus driver (spi_driver) ← talks to the chip (registers, protocol); Subsystem (MTD / input) ← abstracts into a unified API; Exported to users (cdev / evdev) ← `/dev/mtd0`, `/dev/input/event0`.

#### 2.5.2 常见子系统 / Common Subsystems

| 子系统 / Subsystem | 目录 / Directory | 用户接口 / User interface |
| ------ | ---------------------- | ------------------------------ |
| MTD | `drivers/mtd/` | `/dev/mtd*`, mount jffs2/ubifs |
| input | `drivers/input/` | `/dev/input/event*` |
| regmap | `drivers/base/regmap/` | 内核内部，统一寄存器访问 / kernel-internal, unified register access |
| misc | `drivers/char/misc.c` | `/dev/misc*` |

#### 2.5.3 SPI NOR Flash 完整 Linux 栈（学习样本）/ The Full Linux SPI NOR Flash Stack (Study Sample)

```
DT: jedec,spi-nor on SPI bus
  → spi-nor probe [drivers/mtd/spi-nor/core.c]
    → 读 JEDEC ID
    → mtd_device_register()
      → 块层 / UBI / 或 mtdchar → /dev/mtd0
用户: dd if=/dev/mtd0 ...
```

#### mini_tree 对照 / mini_tree Comparison

`fft_spi_drv.c` = 功能驱动 / functional driver；`spi_client.c` = 总线客户端 + fops / bus client + fops。

---

### L6 — 资源管理（devm_*）/ Resource Management (devm_*)

#### 2.6.1 为什么需要 devm / Why devm Is Needed

probe 里资源多、错误路径复杂；`devm_`* 把资源生命周期绑在 `struct device` 上，**remove 或 probe 失败时自动释放**。
probe acquires many resources and has complex error paths; `devm_`* ties resource lifetimes to `struct device`, so **resources are freed automatically on remove or probe failure**.

#### 2.6.2 常用 API / Common APIs

| API | 作用 / Purpose |
| ------------------------------------------ | ------------------- |
| `devm_ioremap_resource` | 映射 MMIO（检查 reg 合法性）/ map MMIO (validates reg) |
| `devm_request_irq` | 注册中断 / register an interrupt |
| `devm_clk_get` / `devm_clk_prepare_enable` | 时钟 / clocks |
| `devm_regulator_get` / `enable` | 电源 / power |
| `devm_gpiod_get` | GPIO 描述符 / GPIO descriptors |
| `devm_kmalloc` | 内存 / memory |
| `devm_spi_alloc_master` | SPI master 结构 / SPI master struct |

实现 / Implementation：`drivers/base/devres.c` — 资源挂在 `dev->devres_head` 链表 / resources hang off the `dev->devres_head` list.

#### 2.6.3 pinctrl / clock / regulator 框架

现代驱动 probe 常见顺序 / Typical modern driver probe order:

```
1. devm_clk_get → prepare_enable
2. devm_regulator_get → enable
3. pinctrl_pm_select_default_state
4. devm_ioremap_resource
5. devm_request_irq
6. 初始化硬件
7. 注册 cdev / spi_device / 子系统
```

#### mini_tree 对照 / mini_tree Comparison

从 DTS props 读引脚/频率，手动调 HAL；remove 路径自行清理（无 devm）。
Reads pins/frequencies from DTS props and calls HAL manually; the remove path cleans up by hand (no devm).

---

### L7 — 设备树（Device Tree）★ / Device Tree ★

#### 2.7.1 启动时 DTB 怎么进内核 / How the DTB Enters the Kernel at Boot

```
Bootloader (U-Boot 等)
  → 把 DTB 地址放入 a0/x0 (arch 相关) 或 embed in kernel
    → setup_arch() [arch/xxx/setup.c]
      → unflatten_device_tree()
        → 分配 struct device_node 树
          → of_platform_populate() 创建 platform_device
            → 各 bus 驱动 probe → 继续枚举子设备
```

#### 2.7.2 struct device_node

```c
// include/linux/of.h
struct device_node {
    const char *name;
    const char *type;
    phandle phandle;
    struct property *properties;
    struct device_node *parent;
    struct device_node *child;
    struct device_node *sibling;
    ...
};
```

**property 链表**：`compatible`、`reg`、`interrupts`、`status` 等。
**Property list**: `compatible`, `reg`, `interrupts`, `status`, etc.

#### 2.7.3 常用 OF API / Common OF APIs

| API | 作用 / Purpose |
| ------------------------- | --------------------- |
| `of_find_compatible_node` | 按 compatible 找节点 / find a node by compatible |
| `of_property_read_u32` | 读单个 u32 属性 / read a single u32 property |
| `of_get_property` | 读原始属性 / read a raw property |
| `of_address_to_resource` | reg → struct resource |
| `of_irq_get` | 解析 interrupts / parse interrupts |
| `of_device_is_compatible` | 判断 compatible / test compatible |
| `of_match_device` | 驱动 match 表匹配 / driver match-table matching |

#### 2.7.4 重要 DTS 属性 / Important DTS Properties

| 属性 / Property | 含义 / Meaning |
| --------------------------------- | ----------------------- |
| `compatible` | 驱动匹配字符串列表 / driver match string list |
| `reg` | 寄存器地址/长度 / register address/length |
| `interrupts` + `interrupt-parent` | 中断 / interrupts |
| `#address-cells` / `#size-cells` | 子节点地址格式 / child address format |
| `status` | `"okay"` / `"disabled"` |
| `clocks` / `pinctrl-0` | 时钟/引脚 / clocks/pins |
| `phandle` / `&label` | 引用其他节点 / reference other nodes |

#### 2.7.5 学习工具 / Learning Tools

```bash
# 编译 DTS
dtc -I dts -O dtb -o board.dtb board.dts

# 反编译
dtc -I dtb -O dts -o board.dts board.dtb

# 运行中查看（Linux 板子）
ls /proc/device-tree/
cat /proc/device-tree/soc/spi@.../compatible
```

内核文档 / Kernel docs：`Documentation/devicetree/`、`Documentation/devicetree/bindings/`。

#### mini_tree 对照 / mini_tree Comparison

dtc-lite **编译期**生成 `s_nodes[]`，无运行时 DTB；API 类似：`device_get_prop_int` ≈ `of_property_read_u32`。
dtc-lite generates `s_nodes[]` at **compile time** — no runtime DTB; the API is analogous: `device_get_prop_int` ≈ `of_property_read_u32`.

---

### L8 — 硬件访问层 / Hardware Access Layer

#### 2.8.1 MMIO

```c
void __iomem *base = devm_ioremap_resource(dev, res);
writel(val, base + REG_OFFSET);
readl(base + REG_OFFSET);
```

ARM64 上可能用 `readl_relaxed`；访问顺序注意 `wmb()`/`rmb()`。
On ARM64 `readl_relaxed` may be used; mind ordering with `wmb()`/`rmb()`.

#### 2.8.2 中断 / Interrupts

```c
devm_request_irq(dev, irq, handler, flags, name, dev_id);
// handler: hardirq → 可能 threaded irq → 或 tasklet/workqueue
enable_irq / disable_irq;
```

GIC：`interrupts = <GIC_SPI num flags>`。

#### 2.8.3 DMA

```c
dma_alloc_coherent / dma_map_single;
dev->dma_mask / coherent_dma_mask;
```

SPI 控制器常用 / Common for SPI controllers：`spi_controller_dma_map_mem_op` 等（内核版本相关 / version-dependent）。

#### mini_tree 对照 / mini_tree Comparison

ESP-IDF SPI/GPIO/DMA HAL 封装上述细节。
ESP-IDF's SPI/GPIO/DMA HAL wraps all of the above details.

---

## 3. Linux SPI 端到端实例（逐步函数级）/ End-to-End Linux SPI Example (Function-Level, Step by Step)

以 **SPI NOR Flash** 为例，从 boot 到用户 read：
Using **SPI NOR Flash** as the example, from boot to a user read:

```
[1] DTB 含 spi 控制器 + flash 子节点
[2] of_platform_populate → platform_device(spi 控制器)
[3] vendor_spi_probe(platform_device)
      devm_ioremap, clk, dma 初始化
      spi_register_master(master)
[4] of_register_spi_devices → spi_device(flash)
[5] spi_nor_probe(spi_device)
      spi_sync 读 JEDEC ID
      mtd_info 注册
[6] mtdchar 或 block 层 → /dev/mtd0
[7] 用户 open/read("/dev/mtd0")
      → vfs → mtd_read → spi_nor → spi_sync → 控制器 transfer
      → 寄存器/DMA → CS 拉低 → Flash 芯片
```

**若暴露 spidev 给用户 / If spidev is exposed to users:**

```
drivers/spi/spidev.c → misc/cdev → /dev/spidevX.Y
用户 ioctl(SPI_IOC_MESSAGE) → spi_sync_message
```

---

## 4. Linux 电源管理（扩展）/ Linux Power Management (Extension)

| 机制 / Mechanism | 说明 / Description |
| ------------------- | -------------------------- |
| `struct dev_pm_ops` | suspend/resume/shutdown |
| runtime PM | `pm_runtime_get/put`，空闲关时钟 / clock off when idle |
| system sleep | 整机 STR/hibernate / full-system STR/hibernate |

probe 里常见 / Common in probe：`pm_runtime_enable(&dev->dev); pm_runtime_get_sync(...)`。

mini_tree 用 `device_status` FSM + `device_suspend/resume` 简化实现。
mini_tree simplifies this with the `device_status` FSM + `device_suspend/resume`.

---

## 5. 关键结构体对照表 / Key Structure Comparison Table

| 概念 / Concept | Linux | mini_tree |
| ----- | ------------------------ | ------------------- |
| 静态 DT / static DT | `struct device_node` | `s_nodes[]` |
| 运行时设备 / runtime device | `struct device` | `s_devices[]` |
| 驱动 / driver | `struct device_driver` | `DRIVER_REGISTER` |
| 总线 / bus | `struct bus_type` | `bus_type` / bus.c |
| 字符设备 / char device | `struct cdev` | `dev->ops` |
| 文件实例 / file instance | `struct file` | `dev_lifecycle` |
| fops | `struct file_operations` | 同名 / same name |
| 私有数据 / private data | `dev_set_drvdata` | `device_set_priv` |
| 父设备 / parent device | `dev->parent` | `node->deps[0]` |
| 子设备枚举 / child enumeration | `device_for_each_child` | `board_cascade_get` |
| 设备号 / device number | `dev_t` | `device_id_t` |
| sysfs | `kobject` | 无 / none |
| 资源 / resources | `devm_*` | 手动 + HAL / manual + HAL |

---

## 6. Linux 内核学习路线图（详细）/ Linux Kernel Learning Roadmap (Detailed)

### 阶段 1：设备模型（1–2 周）/ Stage 1: Device Model (1–2 weeks)

1. 读文档 / read docs：`Documentation/driver-api/driver-model/{index,overview,device,binding}.rst`
2. 跟踪一次 / trace once：`platform_driver_register` → `driver_register` → `bus_add_driver`
3. 跟踪一次 / trace once：`device_add` → `device_attach` → `driver_probe_device`
4. 实验 / experiment：写 `module_platform_driver`，DTS 加 `compatible`，printk probe / write a `module_platform_driver`, add `compatible` to DTS, printk in probe

### 阶段 2：设备树（1 周）/ Stage 2: Device Tree (1 week)

1. 读 / read `Documentation/devicetree/booting-without-of.txt`（概念 / concepts）
2. 读 / read `drivers/of/base.c` — `of_property_read_u32`
3. 用 `dtc` 编译/反编译板级 DTS / compile/decompile a board DTS with `dtc`
4. 在板子上读 / read on a board `/proc/device-tree/`

### 阶段 3：Platform + 资源（1 周）/ Stage 3: Platform + Resources (1 week)

1. 跟踪 / trace `devm_ioremap_resource` in `drivers/base/devres.c`
2. 写一个 MMIO + IRQ 最小驱动（定时器中断计数）/ write a minimal MMIO + IRQ driver (timer interrupt counter)

### 阶段 4：SPI 总线（1–2 周）/ Stage 4: SPI Bus (1–2 weeks)

1. 读 / read `include/linux/spi/spi.h` 全文 / in full
2. 读 / read `drivers/spi/spi.c`：`spi_register_master`、`spi_sync`
3. 选一个 SoC 驱动 / pick an SoC driver：`drivers/spi/spi-pl022.c` 或 / or `spi-dw.c`
4. 读一个 client / read a client：`drivers/mtd/spi-nor/core.c` 或 / or `spidev.c`

### 阶段 5：VFS + cdev（1 周）/ Stage 5: VFS + cdev (1 week)

1. 读 / read `fs/char_dev.c` — `chrdev_open`、`cdev_add`
2. 写 misc 设备或 cdev 驱动，用户态 read/write 验证 / write a misc or cdev driver and verify with userspace read/write

### 推荐 grep 命令（在内核源码树）/ Recommended grep Commands (in the Kernel Source Tree)

```bash
grep -rn "spi_register_master" drivers/spi/
grep -rn "driver_probe_device" drivers/base/
grep -rn "of_platform_populate" drivers/of/
grep -rn "cdev_add" fs/
```

---

## 7. mini_tree 专用章节（简要）/ mini_tree-Specific Notes (Brief)

### 7.1 s_nodes vs s_devices

| | `s_nodes` | `s_devices` |
| -------- | ------------- | --------------------------------- |
| 类比 Linux / Linux analogy | `device_node` | `struct device` |
| 关系 / relation | — | `s_devices[i].node = &s_nodes[i]` |

### 7.2 deps vs cascade

| | 方向 / direction | API |
| ------- | --- | ------------------- |
| deps | 子→父 / child→parent | `device_get_parent` |
| cascade | 父→子 / parent→child | `board_cascade_get` |

### 7.3 本仓库阅读顺序 / Reading Order for This Repo

| 顺序 / Order | 文件 / File | 对照 Linux / Linux counterpart |
| --- | -------------------------------------- | --------------------- |
| 1 | `board/dts/*.dts` | DTS |
| 2 | `build/generated/.../board_devtable.c` | unflattened DT |
| 3 | `build/generated/.../board_probe.c` | probe order + cascade |
| 4 | `board/src/board_driver.c` | device_attach 等价 / equivalent |
| 5 | `vfs/spi/spi_bus.c` | spi_master |
| 6 | `vfs/spi/spi_client.c` | spi_device + fops |

### 7.4 刻意未实现 / Deliberately Not Implemented

无 `/dev`、无 sysfs、无 runtime DTB、无 devm、无 EPROBE_DEFER 自动重试（有简化 probe 重试）。
No `/dev`, no sysfs, no runtime DTB, no devm, no automatic EPROBE_DEFER retry (a simplified probe retry exists).

---

## 8. 参考链接 / Reference Links

- [Driver model overview](https://www.kernel.org/doc/html/latest/driver-api/driver-model/index.html)
- [Platform devices](https://www.kernel.org/doc/html/latest/driver-api/driver-model/platform.html)
- [SPI subsystem](https://www.kernel.org/doc/html/latest/driver-api/spi.html)
- [Device Tree usage](https://www.kernel.org/doc/html/latest/devicetree/usage-model.html)
- [Writing bindings](https://www.kernel.org/doc/html/latest/devicetree/bindings/index.html)
- [Regmap API](https://www.kernel.org/doc/html/latest/driver-api/regmap.html)

---
