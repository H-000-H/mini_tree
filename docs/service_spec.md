## 8. 服务编写规范

### 8.1 Meyers Singleton 模式

```cpp
// audio_service.hpp
class AudioService {
public:
    static AudioService& getInstance();
    bool init();
    bool start();
    void stop();

private:
    AudioService() = default;
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;
};

// audio_service.cpp
AudioService& AudioService::getInstance()
{
    static AudioService service;    // C++11 保证线程安全
    return service;
}
```

### 8.2 生命周期规范

| 方法 | 何时调用 | 做什么 |
|------|---------|--------|
| `init()` | Phase 1 | 分配资源，注册 EventBus 订阅 |
| `start()` | Phase 2 | 启用硬件，开始工作 |
| `stop()` | 系统停机 | 关闭硬件，释放资源 |
| `suspend()` | 低功耗 | 暂停运行，保持配置 |
| `resume()` | 唤醒 | 从暂停点恢复 |

### 8.3 EventBus 通信

```cpp
// 订阅事件
EventBus::getInstance().subscribe(
    EVENT_SYS_READY, EVENT_SYS_READY,
    [](const Event& event, void* user_data) {
        // 系统就绪回调
    }
);

// 发布事件
EventBus::getInstance().post(EVENT_MY_FEATURE, (uintptr_t)some_data);

// 事件 ID 分配:
//   0x0000 - 0x0FFF  框架保留
//   0x1000 - 0xFFFF  用户自定义 (EVENT_USER_BASE + n)
```

> **重要约束**:
> - `subscribe()` 只能在 Phase 2 点火完成前调用，封表后返回 false
> - `post()` 在 Phase 1 完成前静默丢弃事件，防止全局构造函数偷跑

---

## 9. 应用层解耦规范

### 9.1 面向 VFS 设备树编程

业务层通过 VFS 节点操作硬件，不直接调用芯片 SDK：

```cpp
void led_status_task(void* param) {
    int fd = vfs_open("/dev/gpio_led", O_WRONLY);
    while (1) {
        uint8_t level = 1;
        vfs_write(fd, &level, 1);
        osal_task_delay(500);
    }
}
```

更换芯片时，业务代码无需修改——底层 HAL 实现切换由设备树和 soc_port 层完成。

### 9.2 EventBus 事件驱动

```cpp
// UI 层：调整音量时发送事件，不直接调用 AudioService
void knob_callback(lv_event_t* e) {
    uint32_t volume = get_knob_value();
    EventBus::getInstance().post(EVENT_AUDIO_VOLUME_CHANGED, volume);
}

// 音频层：在独立上下文中订阅处理
void audio_service_callback(const Event& event, void* user_data) {
    uint32_t new_volume = event.payload;
    set_hardware_gain(new_volume);
}
```

EventBus 的发布者和订阅者无需感知对方存在。
