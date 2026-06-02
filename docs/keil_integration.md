## 15. Keil MDK 集成说明

> 不推荐日常使用 Keil。构建和验证优先围绕 GCC/Clang 进行。但如果你需要使用 Keil 进行烧录和调试，个人推荐走以下方案。

### 15.1 VS Code + Embedded IDE 方案

Keil IDE 本体打开缓慢、代码补全差、不适合作为日常编辑器。个人推荐使用 **Embedded IDE (EIDE)** 插件——VS Code 扩展，后端调用 Keil MDK 的 ARMCLANG 编译器和调试器，前端使用 VS Code 的编辑体验：

- VS Code 的编辑和补全体验
- 直接调用 Keil 编译器/调试器后端的烧录和调试
- 无需在 Keil IDE 中打开工程

**使用方法：**

1. VS Code 扩展商店搜索 "Embedded IDE" 安装
2. 打开工程文件夹，左侧 Explorer 面板会出现绿色 E 标签
3. 通过绿色 E 面板管理全流程：
   - **编译** — 点击 E 面板的编译按钮
   - **烧录** — 点击 E 面板的下载按钮
   - **调试** — 点击 E 面板的调试按钮
4. 无需使用 VS Code 左上角的"运行和调试"按钮，所有操作在绿色 E 标签中完成

详细用法见官网：[https://em-ide.com](https://em-ide.com)

### 15.2 工具链选择

Keil MDK 5.38 及以上使用 Arm Compiler 6 (ARMCLANG, AC6)，基于 LLVM/Clang 后端。**ARMCC v5.06 已被项目淘汰**，原因详见 [README.md](README.md) 中的说明。

### 15.3 生成 Keil 工程

```bash
python tools/gen_uvprojx.py --platform arm_cm4f --osal FREERTOS \
    --core MYCHIP_MODEL --clock 168000000 \
    --flash-base 0x08000000 --flash-size 0x100000
```

参数说明：

| 参数 | 说明 |
|------|------|
| `--core` | Keil 设备名（如 STM32F407VG，取决于你的芯片） |
| `--clock` | CPU 频率 (Hz) |
| `--flash-base` | Flash 基地址 |
| `--flash-size` | Flash 大小 (bytes) |

### 15.4 清理 Keil 生成文件

```bash
python tools/gen_uvprojx.py --platform arm_cm4f --osal FREERTOS --clean
```

会清除生成的 `.uvprojx`、`.uvoptx`、`.uvguix.*` 文件。
