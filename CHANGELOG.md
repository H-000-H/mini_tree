# Changelog

> 记录中间件 shelf 用户可见的变化。更细的设计动机见 [docs/design_decisions.md](docs/design_decisions.md)。

---

## [Unreleased]

### 架构与代码

- HAL 全面 weak 空实现；默认 `board/dts/board.dts` 为通用占位  
- Bus/VFS 覆盖 gpio/spi/uart/i2c/i2s/can/usb/adc/dac/tim/rtc/iwdg/wwdg 等  
- USB 经 TinyUSB + 板级 `usb_tusb_port` 约定  
- clangd：`compile_flags.txt` + `ide/stubs`；禁止子目录覆盖  

### 文档

- 专题文档统一放到 `docs/`（小写文件名）；根目录仅保留 `README` / `CHANGELOG` / `CONTRIBUTING`  
- 新增 [docs/README.md](docs/README.md) 作为文档目录页  
- 原根目录 `NOTICE.md` → [docs/design_decisions.md](docs/design_decisions.md)；新增 Apache 惯例 [NOTICE](NOTICE)  
- 补充作者偏好取舍与 [docs/references.md](docs/references.md)（ESP VFS / FreeRTOS / Linux / RTT / LVGL / Qt 等对照）  
- 补缺口专题：USB / 外设 / AMP / can_hook / 运行时服务；ESP-IDF CMake 见 [esp_idf_cmake](docs/esp_idf_cmake.md)  
- 作者偏好补充：不推荐 Keil 作主 IDE（跳转/C++/AI 弱）；推荐 Cursor / VS Code / CLion / Qoder  
- 统一文档规格：摘要 / 读者 / 目录 / 正文 / 相关链接  
- 无 `examples/`、无 `board/docs` Wiki  

### 仓库卫生

- 补 [LICENSE](LICENSE)（Apache-2.0）  
- 补 [.gitignore](.gitignore)（对齐 Heterogeneous-Multicore / ST 板工程共用规则）  

### 配置

- OSAL：FreeRTOS V11.3.0 / RT-Thread v5.3.0 / NULL+xtask  
- SYSTEM_C / SYSTEM_CPP 二选一  

---

## [Historical]

多轮重构（设备树、硬件直投、OSAL、安全回路、文档迁徙等）详见 [docs/design_decisions.md](docs/design_decisions.md)。  
平台验证历史以各 SoC 工程仓库为准。

---

## 相关文档

- [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) · [docs/api_compatibility.md](docs/api_compatibility.md)
