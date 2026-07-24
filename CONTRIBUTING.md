# 贡献指南

> 如何改中间件、如何提 PR、本地要准备什么。

| 项 | 内容 |
| :--- | :--- |
| **读者** | 贡献者 |
| **相关** | [docs/api_compatibility.md](docs/api_compatibility.md) · [docs/driver_guide.md](docs/driver_guide.md) |

---

## 开发原则

1. **不向中间件公共头引入厂商 SDK 依赖**（OSAL 后端 `#if` 路径除外）。  
2. 新外设顺序：**HAL 头 + weak .c →（可选）bus → vfs → `DRIVER_REGISTER` → 文档/契约**。  
3. 错误码统一 `status.h`；对外 API 禁止 `void` 成功/失败。  
4. 文档与代码同 PR：至少更新 [docs/file_index.md](docs/file_index.md) 或对应 `docs/*`；新开源积木更新 [docs/ecosystem.md](docs/ecosystem.md) + [NOTICE](NOTICE)。  
5. 遵守 [docs/fast_path.md](docs/fast_path.md) 与分层 poison。  

---

## 环境搭建

| 组件 | 说明 |
| :--- | :--- |
| CMake + Ninja/Make | 构建 |
| Python3 + `lark` | dtc-lite |
| 可选 `kconfiglib` | menuconfig |
| clangd | 打开仓库根；用根 `compile_flags.txt` |

```bash
pip install lark
# 可选: pip install kconfiglib
```

---

## PR 规约

| 要求 | 说明 |
| :--- | :--- |
| 说明影响层 | hal / bus / vfs / board / osal / tools / docs |
| DTS 契约变更 | 同步 `docs/driver_guide.md` |
| 文档位置 | 新专题进 `docs/`；勿在根目录再堆手册 |
| 不提交 | 密钥、本机绝对路径、子目录 `compile_flags.txt`、SoC 专用 dtsi 冒充默认板 |
| 测试说明 | 至少：生成物是否更新、相关外设是否编过 |

---

## 文档规格

新专题进 `docs/`；规格与目录见 [docs/README.md](docs/README.md)。  
新增开源积木须同步 [docs/ecosystem.md](docs/ecosystem.md) 与 [NOTICE](NOTICE)。

---

## 相关文档

- [docs/roadmap.md](docs/roadmap.md) · [docs/todolist.md](docs/todolist.md) · [docs/design_decisions.md](docs/design_decisions.md) · [docs/ecosystem.md](docs/ecosystem.md)
