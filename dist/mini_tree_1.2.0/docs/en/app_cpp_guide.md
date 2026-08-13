# Application-Layer C++ Restrictions Guide

> Restrictions and recommendations for writing C++ in the upper layers (app / business).
>
> This is a reference guide only.

| Item | Content |
| :--- | :--- |
| **Audience** | People writing business logic in C++ at the app / system_cpp layer |
| **Prereq** | [coding_style.md](coding_style.md) · [service_spec.md](service_spec.md) |
| **Related** | [fast_path.md](fast_path.md) · [runtime_services.md](runtime_services.md) |

---

## Contents

1. [Language Choice](#1-language-choice)
2. [Recommended ETL Containers (instead of std)](#2-recommended-etl-containers-instead-of-std)
3. [Coding Rules by Tier](#3-coding-rules-by-tier)
   - [Recommended](#recommended)
   - [Use Sparingly](#use-sparingly)
   - [Avoid Unless Necessary](#avoid-unless-necessary)
   - [Forbidden (repo rules)](#forbidden-repo-rules)

---

## 1. Language Choice

Prefer C++ over C in the upper layers: C exposes more problems and friction at the business layer than the language itself, and C++ brings many implicit advantages (containers, type safety, RAII-style resource management).

---

## 2. Recommended ETL Containers (instead of std)

Use ETL fixed-capacity containers in the upper layer (no heap, compile-time-known size) to avoid the heap allocation and Flash bloat of `std::` containers.

| # | Container | Example | Advantage |
|---|-----------|---------|-----------|
| 1 | `etl::vector` | `etl::vector<int,16> v; v.push_back(1); for(auto x:v){}` | Fixed-capacity, no heap, O(1) random access; replaces std::vector |
| 2 | `etl::map` | `etl::map<int,int,8> m; m.insert({1,10}); auto it=m.find(1);` | Ordered key-value, fixed-capacity lookup, no heap |
| 3 | `etl::set` | `etl::set<int,8> s; s.insert(3); bool ok=s.contains(3);` | Ordered unique set, fixed-capacity; good for enums/flag sets |
| 4 | `etl::list` | `etl::list<int,8> l; l.push_back(1); l.insert(l.begin(),0);` | Stable insert/erase anywhere, no heap node pool |
| 5 | `etl::queue` | `etl::queue<int,8> q; q.push(1); int x=q.front(); q.pop();` | FIFO buffer, fixed-capacity; good for task/message queues |
| 6 | `etl::stack` | `etl::stack<int,8> st; st.push(1); int x=st.top(); st.pop();` | LIFO, fixed-capacity; good for backtracking/nested state |
| 7 | `etl::deque` | `etl::deque<int,8> d; d.push_front(1); d.push_back(2);` | Double-ended, fixed-capacity; less pointer overhead than list |
| 8 | `etl::priority_queue` | `etl::priority_queue<int,8> pq; pq.push(3); int t=pq.top();` | Auto max/min, fixed-capacity heap; good for priority scheduling |
| 9 | `etl::optional` | `etl::optional<int> o; o=42; if(o){int v=*o;}` | Express maybe-value, no heap; replaces sentinel/-1 |
| 10 | `etl::variant` | `etl::variant<int,float> u=1; float f=etl::get<float>(u);` | Fixed-size tagged union, type-safe, no heap |
| 11 | `etl::function` | `void f(int); etl::function<void,int> cb(f); cb(1);` | Fixed-capacity callback/member-fn wrapper, no std::function heap |
| 12 | `etl::array` | `etl::array<int,4> a={1,2,3,4}; int x=a[0];` | Fixed length, constexpr-friendly; replaces C arrays |
| 13 | `etl::span` | `int buf[4]; etl::span<int> sp(buf); sp[0]=1;` | Non-owning view, zero-copy slices of arrays/vectors |
| 14 | `etl::string` | `etl::string<32> s="ok"; s += "!"; size_t n=s.size();` | Fixed-length string, no heap; replaces std::string |
| 15 | `etl::string_view` | `constexpr etl::string_view sv="hi"; size_t n=sv.size();` | Zero-copy read-only view, constexpr, no ownership |
| 16 | `etl::algorithm` | `etl::sort(a.begin(),a.end()); auto it=etl::find(a.begin(),a.end(),3);` | Algorithms for fixed containers; no STL heap dependency |
| 17 | `etl::exception` | `throw etl::exception("reason",__FILE__,__LINE__);` | Lightweight exception base; **exceptions are not allowed embedded — use error codes; reference only** |
| 18 | `etl::bitset` | `etl::bitset<8> bs; bs.set(0); bool ok=bs.test(0);` | Fixed-capacity bit set, no heap; good for flags/boolean state |
| 19 | `etl::chrono` | `etl::chrono::seconds(1); etl::chrono::milliseconds(1000);` | Fixed-capacity time/intervals, no heap; replaces std::chrono |
| 20 | `etl::ratio` | `etl::ratio<1,1000>; etl::ratio<1,1000>::num;` | Fixed-capacity ratio, no heap; replaces std::ratio |
| 21 | `etl::numeric` | `etl::numeric<int,8> n; n.add(1); int x=n.value();` | Fixed-capacity numeric, no heap; replaces std::numeric |
| 22 | `etl::tuple` | `etl::tuple<int,float> t={1,2.0f}; int x=etl::get<0>(t);` | Fixed-capacity tuple, no heap; replaces std::tuple |

---

## 3. Coding Rules by Tier

### Recommended

- **Declare constants with `constexpr`** — compile-time eval, less mutable RAM, config errors surface earlier
- **Use references heavily** — avoid needless copies and fuzzy ownership; prefer `T&` / `const T&` for params
- **Use lambda expressions** — clear local callbacks, can capture context; keep capture lists short, avoid large by-value captures
- **Prefer containers over raw `char*` / `uint8_t*` in upper cpp** — containers carry length/bounds semantics, fewer overflows; prefer span/string_view for buffers
- **Prefer `etl::optional<T>` return values over plain types** — explicit maybe-value, no -1/NULL sentinel ambiguity
- **Error codes / status, not exceptions** — consistent with middleware `VFS_ERR_*`, checkable, no EH dependency
- **Pass buffers as span/string_view** — length-carrying, zero-copy, no raw-pointer length loss
- **Use array or constexpr for fixed tables** — data in Flash, no heap, ready at boot
- **`enum class` over macro enums** — strong typing, scoped, less pollution
- **Uniform `{}` initialization** — less uninitialized UB, clear intent
- **ISR only sets flags / enqueues; business in bottom-half** — aligns with fast_path, no heavy work in interrupts
- **Explicit move semantics over copy when capable** — avoids needless copies; note: only when capable, don't overuse moves (high skill bar, easy leaks)

### Use Sparingly

- **Sparingly use template metaprogramming** — slow compiles, symbol bloat, hard to read/debug; only when reuse payoff is real
- **Sparingly use macros** — no type checking, hard to debug, hygiene issues; prefer constants and inline functions
- **Sparingly use virtual functions** — vtable costs Flash/RAM, hard to inline; prefer static dispatch/templates or function tables
- **Sparingly use std containers and std::string** — heap allocation and Flash bloat; use ETL fixed containers
- **Sparingly use iostream/locale** — large, pointless on embedded
- **Sparingly use floats; prefer fixed-point** — slow and large without an FPU
- **Sparingly use global mutable state** — race-prone; module-static + explicit API
- **Sparingly use multiple/virtual inheritance** — complex layout, size and ambiguity

### Avoid Unless Necessary

- **Avoid function + lambda unless necessary** — `etl::function` is fixed-capacity and captures can blow the budget; prefer function pointers for simple callbacks
- **Avoid recursion unless necessary** — small MCU stack, uncontrolled depth risks overflow; use iteration or explicit stack
- **Avoid deep copies unless necessary** — stack/time heavy; prefer refs, moves, string_view/span
- **Avoid overly complex cpp syntax unless necessary** — poor readability, toolchain variance, bloat; keep a simple, reviewable subset
- **Avoid shared_ptr and other heap smart pointers unless necessary** — can still heap; use refs/handles/object pools
- **Avoid std::function unless necessary** — easy heap/over-capacity; prefer function pointers or short lambdas
- **Avoid log spamming hot paths unless necessary** — jitter; ISR forbids heavy work and formatting
- **Avoid large thread_local objects unless necessary** — BSS/stack pressure, hard-to-control lifetime

### Forbidden (repo rules)

- **No exceptions** — exception tables cost Flash, embedded usually disables EH; use status/error codes uniformly
- **No rtti** — `typeid`/`dynamic_cast` cost space; use static types and variant/enums
- **No `goto` in application layer** — breaks structured control flow; use function拆分 and state machines
- **No `new`/`delete`** — uncertain heap, fragmentation; use static/fixed-capacity ETL and object pools
- **No `malloc`/`free`/`realloc`** — same heap issues; all static or pooled
- **No direct `hal_*` or vendor SDK calls in application layer** — service_spec: only device/VFS/EventBus/OSAL
- **No mutex/malloc/print/heavy logic in ISR** — fast_path red line, deadlock and jitter prone
- **No ignoring `device_*` return values once `DEVICE_WARN_UNUSED_RESULT` is enabled** — off by default for a relaxed app style; when enabled, ignoring `device_open/read/write/ioctl` warns. Internal HAL/bus `COMPAT_WARN_UNUSED_RESULT` stays on by default; use `COMPAT_IGNORE_RESULT()` if intentionally discarded
- **No C-style VLA** — runtime-unbounded stack, overflow prone
- **No relying on undefined behavior** — uninitialized/out-of-bounds is hard to reproduce
- **No non-inline redefinition entities in headers** — ODR and code bloat
- **No direct `xTaskCreate` etc. kernel API in business** — use `osal_task_*` uniformly, eases OSAL backend switch
- **No scattered magic numbers** — use Kconfig/config.h or named constants

---

## Related Documents

- [coding_style.md](coding_style.md) · [service_spec.md](service_spec.md) · [fast_path.md](fast_path.md)
