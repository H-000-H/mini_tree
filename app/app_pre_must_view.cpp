/* SPDX-License-Identifier: Apache-2.0 */
#include "etl/algorithm.h"
#include "etl/array.h"
#include "etl/bitset.h"
#include "etl/chrono.h"
#include "etl/deque.h"
#include "etl/exception.h"
#include "etl/function.h"
#include "etl/list.h"
#include "etl/map.h"
#include "etl/numeric.h"
#include "etl/optional.h"
#include "etl/priority_queue.h"
#include "etl/queue.h"
#include "etl/ratio.h"
#include "etl/set.h"
#include "etl/span.h"
#include "etl/stack.h"
#include "etl/string.h"
#include "etl/string_view.h"
#include "etl/tuple.h"
#include "etl/utility.h"
#include "etl/variant.h"
#include "etl/vector.h"
namespace cpp_limitation_recommendations
{
/*===============================================================================================================================================================================================*/
const etl::string<100> k_preview_str = "该文档的存在只是作者建议基于嵌入式的条件下对cpp的限制性建议"
                                       ",不要将该文件链接与cmake接入实际的项目里面";
const etl::string<100> k_file_write_limits_str =
    "该文件不允许使用非隔离分隔符进行文件的写入,请使用etl/"
    "stl标准库容器或者cpp正常语法进行文件的写入";
/*===============================================================================================================================================================================================*/

etl::optional<etl::string<100>> get_recommend_grammer_str1(etl::string<100> recommend_grammer_str)
{
    if (recommend_grammer_str == "空实现分割作用")
        return etl::optional<etl::string<100>>(recommend_grammer_str);
    return etl::nullopt;
}

/*===============================================================================================================================================================================================*/
const etl::vector<etl::string<200>, 20> k_recommend_grammer_str = {
    "1.etl::vector | 例:etl::vector<int,16> v; v.push_back(1); for(auto x:v){} | "
    "优势:定容无堆、随机访问O(1)、替代std::vector",
    "2.etl::map | 例:etl::map<int,int,8> m; m.insert({1,10}); auto it=m.find(1); | "
    "优势:有序键值、定容查找、无堆分配",
    "3.etl::set | 例:etl::set<int,8> s; s.insert(3); bool ok=s.contains(3); | "
    "优势:有序去重集合、定容、适合枚举/标志集合",
    "4.etl::list | 例:etl::list<int,8> l; l.push_back(1); l.insert(l.begin(),0); | "
    "优势:任意位置插入删除稳定、无堆节点池内分配",
    "5.etl::queue | 例:etl::queue<int,8> q; q.push(1); int x=q.front(); q.pop(); | "
    "优势:FIFO缓冲、定容、适合任务/消息排队",
    "6.etl::stack | 例:etl::stack<int,8> st; st.push(1); int x=st.top(); st.pop(); | "
    "优势:LIFO、定容、适合回溯/嵌套状态",
    "7.etl::deque | 例:etl::deque<int,8> d; d.push_front(1); d.push_back(2); | "
    "优势:双端进出、定容、比list更省指针开销",
    "8.etl::priority_queue | 例:etl::priority_queue<int,8> pq; pq.push(3); int t=pq.top(); | "
    "优势:自动取最值、定容堆、适合优先级调度",
    "9.etl::optional | 例:etl::optional<int> o; o=42; if(o){int v=*o;} | "
    "优势:表达可有可无、无堆、替代哨兵值/-1",
    "10.etl::variant | 例:etl::variant<int,float> u=1; float f=etl::get<float>(u); | "
    "优势:定长多类型联合、类型安全、无堆",
    "11.etl::function | 例:void f(int); etl::function<void,int> cb(f); cb(1); | "
    "优势:定容回调/成员函数包装、无std::function堆",
    "12.etl::array | 例:etl::array<int,4> a={1,2,3,4}; int x=a[0]; | "
    "优势:固定长度、可constexpr友好、替代C数组",
    "13.etl::span | 例:int buf[4]; etl::span<int> sp(buf); sp[0]=1; | "
    "优势:非拥有视图、统一传数组/vector切片、零拷贝",
    "14.etl::string | 例:etl::string<32> s=\"ok\"; s += \"!\"; size_t n=s.size(); | "
    "优势:定长字符串、无堆、替代std::string",
    "15.etl::string_view | 例:constexpr etl::string_view sv=\"hi\"; size_t n=sv.size(); | "
    "优势:零拷贝只读视图、可constexpr、不拥有内存",
    "16.etl::algorithm | 例:etl::sort(a.begin(),a.end()); auto it=etl::find(a.begin(),a.end(),3); "
    "| 优势:配套定容容器的算法、不引入STL堆依赖",
    "17.etl::exception | 例:throw etl::exception(\"reason\",__FILE__,__LINE__); | "
    "优势:轻量异常信息基类；也可关异常改错误码风格 注意: "
    "嵌入式是不允许异常的所以请使用错误码风格这里只做参考",
    "18.etl::bitset | 例:etl::bitset<8> bs; bs.set(0); bool ok=bs.test(0); | "
    "优势:定容位集合、无堆、适合标志位/布尔状态",
    "19.etl::chrono | 例:etl::chrono::seconds(1); etl::chrono::milliseconds(1000); | "
    "优势:定容时间/间隔、无堆、替代std::chrono",
    "20.etl::ratio | 例:etl::ratio<1,1000>; etl::ratio<1,1000>::num; | "
    "优势:定容比例、无堆、替代std::ratio",
    "21.etl::numeric | 例:etl::numeric<int,8> n; n.add(1); int x=n.value(); | "
    "优势:定容数值、无堆、替代std::numeric",
    "22.etl::tuple | 例:etl::tuple<int,float> t={1,2.0f}; int x=etl::get<0>(t); | "
    "优势:定容元组、无堆、替代std::tuple",
    "23.etl::bitset | 例:etl::bitset<8> bs; bs.set(0); bool ok=bs.test(0); | "
    "优势:定容位集合、无堆、适合标志位/布尔状态",
    "24.etl::chrono | 例:etl::chrono::seconds(1); etl::chrono::milliseconds(1000); | "
    "优势:定容时间/间隔、无堆、替代std::chrono",
    "25.etl::ratio | 例:etl::ratio<1,1000>; etl::ratio<1,1000>::num; | "
    "优势:定容比例、无堆、替代std::ratio",
};
/*===============================================================================================================================================================================================*/

etl::optional<etl::string<100>> get_recommend_grammer_str2(etl::string<100> recommend_grammer_str)
{
    if (recommend_grammer_str == "空实现分割作用")
        return etl::optional<etl::string<100>>(recommend_grammer_str);
    return etl::nullopt;
}

/*===============================================================================================================================================================================================*/
/* first=条目, second=原因；分档: 推荐 / 少用 / 非必要 / 禁止(+仓规) */
const etl::array<etl::pair<etl::string<128>, etl::string<128>>, 41> k_limit_or_banned_str = {{
    /*===============================================================================================================================================================================================*/
    {"推荐用constexpr声明常量", "原因:编译期求值、少占可变RAM、配置错误更早暴露"},
    {"推荐大量使用引用", "原因:避免无意义拷贝与所有权模糊，接口传参优先T&/const T&"},
    {"推荐使用lambda表达式", "原因:局部回调清晰、可捕获上下文；捕获列表要短，避免大对象按值捕获"},
    {"上层cpp推荐走容器而不是普通的char*,uint8_t*等裸指针",
     "原因:容器带长度与边界语义，减少越界/长度丢失；缓冲优先span/string_view"},
    {"返回值优先使用etl::optional<T>而不是一般类型",
     "原因:显式表达有无值，杜绝-1/NULL等哨兵歧义，调用方可强制检查"},
    {"推荐错误码/status而非异常", "原因:与中间件VFS_ERR_*一致、可检查、不依赖EH"},
    {"推荐缓冲传参用span/string_view", "原因:带长度零拷贝，减少裸指针长度丢失"},
    {"推荐固定表用array或constexpr", "原因:数据进Flash、无堆、启动即可用"},
    {"推荐enum class替代宏枚举", "原因:强类型、作用域清晰、少污染"},
    {"推荐{}统一初始化", "原因:减少未初始化UB，意图明确"},
    {"推荐ISR只置位/入队、业务下半部处理", "原因:对齐fast_path，避免中断里重活"},
    {"推荐有能力可以显示使用移动语义而不是拷贝语义",
     "原因:移动语义可以避免不必要的拷贝，提高性能 "
     "注意!!!:"
     "这里说的是有能力可以显示使用移动语义而不是拷贝语义，而不是说推荐你大量显示使用移动语义,"
     "因为移动语义对水平要求比较高，如果水平不够容易出现内存泄漏等问题"},
    /*===============================================================================================================================================================================================*/
    {"少用模板元编程", "原因:编译慢、符号膨胀、难读难调；仅在确有复用收益时使用"},
    {"少用宏定义", "原因:无类型检查、调试困难、易卫生问题；常量与内联函数优先"},
    {"少用虚函数", "原因:虚表占Flash/RAM、难内联；优先静态分派/模板或函数表"},
    {"少用std容器与std::string", "原因:堆分配与Flash膨胀；改用ETL定容容器"},
    {"少用iostream/locale", "原因:体积大、嵌入式无意义"},
    {"少用浮点能定点则定点", "原因:无FPU时慢且代码大"},
    {"少用全局可变状态", "原因:易竞态；模块内static+明确API"},
    {"少用多重继承/虚继承", "原因:布局复杂难审、体积与歧义"},
    /*===============================================================================================================================================================================================*/
    {"非必要不用function加lambda", "原因:etl::function定容且捕获易撑爆；简单回调优先函数指针"},
    {"非必要禁止递归", "原因:MCU栈小，递归深度不可控易栈溢出；改为迭代或显式栈"},
    {"非必要不用深度拷贝", "原因:深拷贝耗栈/耗时；优先引用、移动、string_view/span"},
    {"非必要别走巨复杂cpp语法", "原因:可读性差、工具链差异大、体积膨胀；保持简单可审查子集"},
    {"非必要不用shared_ptr等堆智能指针", "原因:仍可能堆；用引用/句柄/对象池"},
    {"非必要不用std::function", "原因:易堆/超容量；优先函数指针或短lambda"},
    {"非必要不做热路径狂打日志", "原因:抖动；ISR禁止重活与格式化"},
    {"非必要不用thread_local大对象", "原因:BSS/栈压力，生命周期难控"},
    /*===============================================================================================================================================================================================*/
    {"禁止使用异常", "原因:异常表占Flash，嵌入式常关EH；统一用status/错误码"},
    {"禁止使用rtti", "原因:typeid/dynamic_cast占空间；用静态类型与variant/枚举"},
    {"禁止应用层goto", "原因:破坏结构化控制流；用函数拆分与状态机"},
    {"禁止使用new/delete", "原因:堆不确定、碎片化；用静态/定容ETL与对象池"},
    {"禁止malloc/free/realloc", "原因:同堆问题；全部静态或池化"},
    {"禁止应用层直接调hal_*或厂商SDK", "原因:service_spec：只走device/VFS/EventBus/OSAL"},
    {"禁止ISR里mutex/malloc/打印/重逻辑", "原因:fast_path红线，易死锁与抖动"},
    {"禁止忽略WARN_UNUSED_RESULT返回值", "原因:错误被吞，故障难追踪"},
    {"禁止C风格VLA", "原因:栈大小运行期不定，易溢出"},
    {"禁止依赖未定义行为", "原因:未初始化/越界难复现"},
    {"禁止头文件放非inline重定义实体", "原因:ODR与代码膨胀"},
    {"禁止业务直调xTaskCreate等内核API", "原因:统一osal_task_*，便于换OSAL后端"},
    {"禁止散落magic number", "原因:走Kconfig/config.h或命名常量"},
}};
/*===============================================================================================================================================================================================*/
etl::optional<etl::string<100>> get_recommend_grammer_str(etl::string<100> recommend_grammer_str)
{
    if (recommend_grammer_str == "空实现分割作用")
        return etl::optional<etl::string<100>>(recommend_grammer_str);
    return etl::nullopt;
}
/*===============================================================================================================================================================================================*/
const etl::string<100> k_language_selection_recommend_str =
    "个人是比较推荐上层使用cpp而不是c的因为c在业务层暴露的问题和麻烦远大于cpp语言本身的问题 "
    "而且cpp写业务层有大量隐性优势(不细说)";
/*===============================================================================================================================================================================================*/
} // namespace cpp_limitation_recommendations
