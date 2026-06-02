## 12. 常见问题

### Q: 如何选择合适的 OSAL 后端？

| 场景 | 选择 |
|------|------|
| 产品级多任务 | FreeRTOS 或 RT-Thread |
| 纯前后台裸机 | OSAL_NULL |
| 资源极度受限 (< 8KB RAM) | OSAL_NULL + system_c |
| 需要 FinSH 调试终端 | RT-Thread |
| 社区生态广泛 | FreeRTOS |

### Q: 如何选择 system 后端？

| 场景 | 推荐选择 |
|------|---------|
| 默认现代 C++ | SYSTEM_CPP |
| 医疗/工控合规要求纯 C | SYSTEM_C |
| 团队 C 技能为主 | SYSTEM_C |
| 功能安全认证交付 | SYSTEM_C |

### Q: 如何添加新的 HAL 接口？

1. 在 `hal_if/include/` 中声明操作表结构体
2. 在 `soc_port_` 中实现
3. 驱动通过 `device_t*` 获取 ops 并调用

### Q: 为什么需要在启动前 touch Singleton？

```cpp
(void)EventBus::getInstance();  // 预触摸
```

C++11 局部静态变量的首次初始化使用 `__cxa_guard_acquire` 互斥锁。如果在 ISR 中首次调用 `getInstance()`，互斥锁可能导致死锁。Phase 1 在 RTOS 启动前执行，自然完成了实例化。

### Q: 构建报错 `Target requires the language dialect "C23"`？

ARM GCC 13.3.1 使用 `-std=c2x` 而非 `-std=c23`。使用提供的 toolchain 文件自动处理：

```bash
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_arm_cm3.cmake
```

### Q: 多核配置注意事项？

任务通过 `osal_task_create_handle` 的 `core_id` 参数绑核；EventBus 跨核事件投递由 `osal_queue_send` 保证原子性；BufferPool 内存建议对齐到 cache line 避免伪共享。
