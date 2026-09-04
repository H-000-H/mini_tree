# 应用层 C++ 限制性指南

> 上层（app / 业务）写 C++ 前的限制性建议与推荐做法。
>
> This is a reference guide only.

| 项 | 内容 |
| :--- | :--- |
| **读者** | 在 app / system_cpp 上层用 C++ 写业务的人 |
| **前置** | [coding_style.md](coding_style.md) · [service_spec.md](service_spec.md) |
| **相关** | [fast_path.md](fast_path.md) · [runtime_services.md](runtime_services.md) |

---

## 目录

1. [语言选型](#1-语言选型)
2. [ETL 容器推荐（替代 std 容器）](#2-etl-容器推荐替代-std-容器)
3. [编码规范分档](#3-编码规范分档)
   - [推荐](#推荐)
   - [少用](#少用)
   - [非必要不用](#非必要不用)
   - [禁止（仓规）](#禁止仓规)

---

## 1. 语言选型

上层推荐使用 C++ 而非 C：C 在业务层暴露的问题和麻烦远大于 C++ 语言本身，且 C++ 写业务层有大量隐性优势（容器、类型安全、RAII 风格资源管理）。

---

## 2. ETL 容器推荐（替代 std 容器）

上层统一用 ETL 定容容器（无堆、编译期确定大小），避免 `std::` 容器的堆分配与 Flash 膨胀。

| # | 容器 | 示例 | 优势 |
|---|------|------|------|
| 1 | `etl::vector` | `etl::vector<int,16> v; v.push_back(1); for(auto x:v){}` | 定容无堆、随机访问 O(1)、替代 std::vector |
| 2 | `etl::map` | `etl::map<int,int,8> m; m.insert({1,10}); auto it=m.find(1);` | 有序键值、定容查找、无堆分配 |
| 3 | `etl::set` | `etl::set<int,8> s; s.insert(3); bool ok=s.contains(3);` | 有序去重集合、定容、适合枚举/标志集合 |
| 4 | `etl::list` | `etl::list<int,8> l; l.push_back(1); l.insert(l.begin(),0);` | 任意位置插入删除稳定、无堆节点池内分配 |
| 5 | `etl::queue` | `etl::queue<int,8> q; q.push(1); int x=q.front(); q.pop();` | FIFO 缓冲、定容、适合任务/消息排队 |
| 6 | `etl::stack` | `etl::stack<int,8> st; st.push(1); int x=st.top(); st.pop();` | LIFO、定容、适合回溯/嵌套状态 |
| 7 | `etl::deque` | `etl::deque<int,8> d; d.push_front(1); d.push_back(2);` | 双端进出、定容、比 list 更省指针开销 |
| 8 | `etl::priority_queue` | `etl::priority_queue<int,8> pq; pq.push(3); int t=pq.top();` | 自动取最值、定容堆、适合优先级调度 |
| 9 | `etl::optional` | `etl::optional<int> o; o=42; if(o){int v=*o;}` | 表达可有可无、无堆、替代哨兵值/-1 |
| 10 | `etl::variant` | `etl::variant<int,float> u=1; float f=etl::get<float>(u);` | 定长多类型联合、类型安全、无堆 |
| 11 | `etl::function` | `void f(int); etl::function<void,int> cb(f); cb(1);` | 定容回调/成员函数包装、无 std::function 堆 |
| 12 | `etl::array` | `etl::array<int,4> a={1,2,3,4}; int x=a[0];` | 固定长度、可 constexpr 友好、替代 C 数组 |
| 13 | `etl::span` | `int buf[4]; etl::span<int> sp(buf); sp[0]=1;` | 非拥有视图、统一传数组/vector 切片、零拷贝 |
| 14 | `etl::string` | `etl::string<32> s="ok"; s += "!"; size_t n=s.size();` | 定长字符串、无堆、替代 std::string |
| 15 | `etl::string_view` | `constexpr etl::string_view sv="hi"; size_t n=sv.size();` | 零拷贝只读视图、可 constexpr、不拥有内存 |
| 16 | `etl::algorithm` | `etl::sort(a.begin(),a.end()); auto it=etl::find(a.begin(),a.end(),3);` | 配套定容容器的算法、不引入 STL 堆依赖 |
| 17 | `etl::exception` | `throw etl::exception("reason",__FILE__,__LINE__);` | 轻量异常信息基类；**嵌入式不允许异常，请用错误码风格，此处仅参考** |
| 18 | `etl::bitset` | `etl::bitset<8> bs; bs.set(0); bool ok=bs.test(0);` | 定容位集合、无堆、适合标志位/布尔状态 |
| 19 | `etl::chrono` | `etl::chrono::seconds(1); etl::chrono::milliseconds(1000);` | 定容时间/间隔、无堆、替代 std::chrono |
| 20 | `etl::ratio` | `etl::ratio<1,1000>; etl::ratio<1,1000>::num;` | 定容比例、无堆、替代 std::ratio |
| 21 | `etl::numeric` | `etl::numeric<int,8> n; n.add(1); int x=n.value();` | 定容数值、无堆、替代 std::numeric |
| 22 | `etl::tuple` | `etl::tuple<int,float> t={1,2.0f}; int x=etl::get<0>(t);` | 定容元组、无堆、替代 std::tuple |

---

## 3. 编码规范分档

### 推荐

- **用 `constexpr` 声明常量** — 编译期求值、少占可变 RAM、配置错误更早暴露
- **大量使用引用** — 避免无意义拷贝与所有权模糊，接口传参优先 `T&` / `const T&`
- **使用 lambda 表达式** — 局部回调清晰、可捕获上下文；捕获列表要短，避免大对象按值捕获
- **上层 cpp 走容器而不是裸 `char*` / `uint8_t*`** — 容器带长度与边界语义，减少越界/长度丢失；缓冲优先 span/string_view
- **返回值优先用 `etl::optional<T>` 而不是一般类型** — 显式表达有无值，杜绝 -1/NULL 等哨兵歧义
- **错误码/status 而非异常** — 与中间件 `MINI_ERR_*` 一致、可检查、不依赖 EH
- **缓冲传参用 span/string_view** — 带长度零拷贝，减少裸指针长度丢失
- **固定表用 array 或 constexpr** — 数据进 Flash、无堆、启动即可用
- **`enum class` 替代宏枚举** — 强类型、作用域清晰、少污染
- **用 `{}` 统一初始化** — 减少未初始化 UB，意图明确
- **ISR 只置位/入队、业务下半部处理** — 对齐 fast_path，避免中断里重活
- **有能力时显式使用移动语义而非拷贝语义** — 移动语义避免不必要拷贝；注意：仅"有能力"时显式使用，不要大量显式用移动语义（对水平要求高，易内存泄漏）

### 少用

- **少用模板元编程** — 编译慢、符号膨胀、难读难调；仅在确有复用收益时使用
- **少用宏定义** — 无类型检查、调试困难、易卫生问题；常量与内联函数优先
- **少用虚函数** — 虚表占 Flash/RAM、难内联；优先静态分派/模板或函数表
- **少用 std 容器与 std::string** — 堆分配与 Flash 膨胀；改用 ETL 定容容器
- **少用 iostream/locale** — 体积大、嵌入式无意义
- **少用浮点，能定点则定点** — 无 FPU 时慢且代码大
- **少用全局可变状态** — 易竞态；模块内 static + 明确 API
- **少用多重继承/虚继承** — 布局复杂难审、体积与歧义

### 非必要不用

- **非必要不用 function 加 lambda** — `etl::function` 定容且捕获易撑爆；简单回调优先函数指针
- **非必要禁止递归** — MCU 栈小，递归深度不可控易栈溢出；改为迭代或显式栈
- **非必要不用深度拷贝** — 深拷贝耗栈/耗时；优先引用、移动、string_view/span
- **非必要别走巨复杂 cpp 语法** — 可读性差、工具链差异大、体积膨胀；保持简单可审查子集
- **非必要不用 shared_ptr 等堆智能指针** — 仍可能堆；用引用/句柄/对象池
- **非必要不用 std::function** — 易堆/超容量；优先函数指针或短 lambda
- **非必要不做热路径狂打日志** — 抖动；ISR 禁止重活与格式化
- **非必要不用 thread_local 大对象** — BSS/栈压力，生命周期难控

### 禁止（仓规）

- **禁止异常** — 异常表占 Flash，嵌入式常关 EH；统一用 status/错误码
- **禁止 rtti** — `typeid`/`dynamic_cast` 占空间；用静态类型与 variant/枚举
- **禁止应用层 goto** — 破坏结构化控制流；用函数拆分与状态机
- **禁止 `new`/`delete`** — 堆不确定、碎片化；用静态/定容 ETL 与对象池
- **禁止 `malloc`/`free`/`realloc`** — 同堆问题；全部静态或池化
- **禁止应用层直接调 `hal_*` 或厂商 SDK** — service_spec：只走 device/VFS/EventBus/OSAL
- **禁止 ISR 里 mutex/malloc/打印/重逻辑** — fast_path 红线，易死锁与抖动
- **开启 `DEVICE_WARN_UNUSED_RESULT` 后禁止忽略 `device_*` 返回值** — 该开关默认关闭（应用层可宽松）；开启后忽略 `device_open/read/write/ioctl` 返回值会报警。底层 HAL/bus 的 `MINI_WARN_UNUSED_RESULT` 默认开启始终强制，确需忽略用 `MINI_IGNORE_RESULT()`
- **禁止 C 风格 VLA** — 栈大小运行期不定，易溢出
- **禁止依赖未定义行为** — 未初始化/越界难复现
- **禁止头文件放非 inline 重定义实体** — ODR 与代码膨胀
- **禁止业务直调 `xTaskCreate` 等内核 API** — 统一 `osal_task_*`，便于换 OSAL 后端
- **禁止散落 magic number** — 走 Kconfig/config.h 或命名常量

---

## 相关文档

- [coding_style.md](coding_style.md) · [service_spec.md](service_spec.md) · [fast_path.md](fast_path.md)
