# kconfiglib (vendored)

- **上游**: [ulfalizer/Kconfiglib](https://github.com/ulfalizer/Kconfiglib)
- **作者**: Ulf Magnusson (ulfalizer)
- **许可**: ISC — 见 `LICENSE.txt`

## 文件清单

| 文件 | 作用 |
| :--- | :--- |
| `kconfiglib.py` | Kconfig 解析器 |
| `menuconfig.py`  | curses 终端配置 UI |
| `guiconfig.py`   | Tkinter 图形配置 UI |
| `LICENSE.txt`    | ISC 许可全文 |

## 加载方式

由 `tools/_vendor_loader.py` 把本目录前置到 `sys.path`,
通过 `tools/menuconfig.py` 和 `tools/genconfig.py` 顶部的
`import _vendor_loader; _vendor_loader.prepend_kconfig_vendor()` 启用。

三个 `.py` 保持与上游一致, 不做任何本地修改。
