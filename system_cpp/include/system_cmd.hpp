/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file system_cmd.h
 * @brief 系统命令注册与分发器 (System Command Dispatcher)
 *
 * @details
 * 核心架构特性与设计权衡：
 *   . 零堆分配类型擦除 (Heap-Free Type Erasure)：
 *     - 基于 etl::inplace_function (Small Buffer Optimization)，在栈与固定内联缓冲区
 *       内原地构造闭包，严格杜绝 malloc/new 动态内存碎片。
 *     - 支持原生函数指针、无捕获 Lambda、有状态捕获 Lambda 以及各类仿函数。
 *   . 轻量类型安全校验 (Zero-Overhead RTTI)：
 *     - 利用模板静态局部变量的唯一地址生成 TypeIdToken 令牌，在兼容 -fno-rtti
 *       的同时实现入参与上下文的强类型校验，阻断非法的非法类型强转。
 */

#pragma once

#include "mini_critical.h"
#include "status.h"
#include <etl/char_traits.h>
#include <etl/inplace_function.h>
#include <etl/type_traits.h>
#include <etl/utility.h>

#ifndef SYS_CMD_MAX_NAME_LEN
#define SYS_CMD_MAX_NAME_LEN 16 /**< 命令字符串最大长度限制 (包含末尾 '\0' 截断符) */
#endif

#ifndef SYS_CMD_MAX_COUNT
#define SYS_CMD_MAX_COUNT 8    /**< 静态命令查找表最大容量限制 */
#endif

/**
 * @def SYS_CMD_WRAPPER_FN_SZ
 * @brief handler 内联存储容量 (字节)。
 * @note 必须足以容纳被注册 callable 对象的闭包捕获状态与内部结构。
 *       若捕获内容超限，etl::inplace_function 将在编译期触发 static_assert 报错，
 *       此时按需调大此宏即可。
 */
#ifndef SYS_CMD_WRAPPER_FN_SZ
#define SYS_CMD_WRAPPER_FN_SZ 32
#endif

/**
 * @class SystemCmd
 * @brief 单例系统命令路由与分发控制器
 */
class SystemCmd
{
public:
    static constexpr size_t k_max_cmd_name_len = SYS_CMD_MAX_NAME_LEN; /**< 命令名最大字符数 (含 '\0') */
    static constexpr size_t k_max_commands     = SYS_CMD_MAX_COUNT;    /**< 最大可注册命令节点总数 */

    /* ---------------------------------------------------------------------- */
    /* 轻量 RTTI: 基于静态单例地址实现的编译期类型指纹                        */
    /* ---------------------------------------------------------------------- */
    
    /**
     * @brief 类型身份令牌指针
     * @note 指向全局唯一的静态标志变量，其地址作为类型的唯一指纹
     */
    using TypeIdToken = const void*;

    /**
     * @brief 获取指定类型的全局唯一身份令牌 (轻量 RTTI)
     * @tparam T 目标类型
     * @return TypeIdToken 对应类型的静态地址标识
     * @note 即使全局禁用 RTTI (-fno-rtti)，编译器仍能为每个特化生成独立的变量地址。
     */
    template <typename T>
    static TypeIdToken get_type_id()
    {
        static const char k_type_marker = 0;
        return static_cast<TypeIdToken>(&k_type_marker);
    }

    /**
     * @brief 经类型擦除后的统一函数入口
     * @note 签名固定为 bool(const void* arg, size_t len, void* ctx)，
     *       并在 SYS_CMD_WRAPPER_FN_SZ 字节内联空间中完成原地存储与调用。
     */
    using RawHandler =etl::inplace_function<bool(const void*, size_t, void*), SYS_CMD_WRAPPER_FN_SZ>;

    /**
     * @brief 命令分发节点
     */
    struct HandlerNode
    {
        RawHandler  wrapper; /**< 类型擦除后的闭包包装体 */
        TypeIdToken args_id; /**< 注册时绑定的入参类型令牌 (分发时做强一致校验) */
        TypeIdToken ctx_id;  /**< 注册时绑定的上下文类型令牌 (分发时做强一致校验) */
    };

    /**
     * @brief 获取命令分发器单例实例
     * @return SystemCmd& 单例对象的引用
     */
    static SystemCmd& get_instance();

    /**
     * @brief 注册强类型系统命令
     * @tparam Args 入参结构体类型 (传入 void 代表该命令无输入参数)
     * @tparam Ctx  上下文对象类型 (传入 void 代表无需外部环境指针)
     * @tparam F    可调用对象类型 (函数指针 / 泛型 Lambda / 仿函数)
     * @param[in] name 命令标识名 (必须以 '\0' 结尾，长度受限于 k_max_cmd_name_len)
     * @param[in] f    用户处理函数：
     *                 - 若 Args=void, Ctx=void: 签名需兼容 bool()
     *                 - 若 Args!=void, Ctx=void: 签名需兼容 bool(const Args&) 或 bool(const Args&, void*)
     *                 - 若 Args!=void, Ctx!=void: 签名需兼容 bool(const Args&, Ctx*)
     *                 - 闭包捕获体积严禁超出 SYS_CMD_WRAPPER_FN_SZ 字节
     * @retval MINI_OK           注册成功
     * @retval MINI_ERR_INVAL    入参非法 (空指针或命令名字节数超标)
     * @retval MINI_ERR_NOSPC    命令表已满 (超出 k_max_commands 预设容量)
     * @retval MINI_ERR_BUSY     该命令名已被注册 (禁止重复注册同名命令)
     */
    template <typename Args, typename Ctx = void, typename F>
    int register_cmd(const char* name, F&& f)
    {
        RawHandler wrapper = make_wrapper<Args, Ctx>(etl::forward<F>(f));
        return register_raw(name, wrapper, get_type_id<Args>(), get_type_id<Ctx>());
    }

    /* ---------------------------------------------------------------------- */
    /* 命令分发 / 查询接口                                                   */
    /* ---------------------------------------------------------------------- */

    /**
     * @brief 原始底层命令分发 (支持手动跳过部分类型检查)
     * @param[in] name             命令标识名
     * @param[in] arg              输入参数二进制数据指针 (可为 nullptr)
     * @param[in] arg_len          输入参数数据长度 (字节数)
     * @param[in] ctx              运行时上下文指针 (可为 nullptr)
     * @param[in] expected_args_id 预期的入参类型令牌 (为 nullptr 时跳过参数类型校验)
     * @param[in] expected_ctx_id  预期的上下文类型令牌 (为 nullptr 时跳过上下文类型校验)
     * @retval MINI_OK           命令执行成功且闭包返回 true
     * @retval MINI_ERR_INVAL    入参指针非法
     * @retval MINI_ERR_NOTSUPP  命令未找到，或类型令牌校验失败 (类型不匹配)
     * @retval MINI_ERR_FAULT    命令处理逻辑返回失败 (用户回调返回 false)
     */
    int dispatch(const char* name, const void* arg, size_t arg_len, void* ctx = nullptr,
                 TypeIdToken expected_args_id = nullptr,
                 TypeIdToken expected_ctx_id = nullptr) const;

    /**
     * @brief 类型安全命令分发 (携带强类型参数 + 可选上下文)
     * @tparam Args 匹配的参数类型
     * @tparam Ctx  匹配的上下文类型
     * @param[in] name 命令名
     * @param[in] arg  强类型入参对象常量引用 (内部按字节取地址分发)
     * @param[in] ctx  强类型上下文指针 (默认为 nullptr)
     * @return 状态码 (参见 dispatch)
     */
    template <typename Args, typename Ctx = void>
    int dispatch_secure(const char* name, const Args& arg, Ctx* ctx = nullptr) const
    {
        return dispatch(name, &arg, sizeof(Args), ctx, get_type_id<Args>(), get_type_id<Ctx>());
    }

    /**
     * @brief 类型安全命令分发 (无输入参数，仅携带可选上下文)
     * @tparam Ctx  匹配的上下文类型
     * @param[in] name 命令名
     * @param[in] ctx  强类型上下文指针 (默认为 nullptr)
     * @return 状态码 (参见 dispatch)
     */
    template <typename Ctx = void>
    int dispatch_secure(const char* name, Ctx* ctx = nullptr) const
    {
        return dispatch(name, nullptr, 0, ctx, get_type_id<void>(), get_type_id<Ctx>());
    }

    /**
     * @brief 注销指定名称的命令
     * @param[in] name 待注销的命令名称
     * @retval MINI_OK           注销成功
     * @retval MINI_ERR_INVAL    命令名指针为空
     * @retval MINI_ERR_NOTSUPP  该命令未在表中，无需注销
     */
    int unregister_cmd(const char* name);

    /**
     * @brief 查询指定命令是否已被注册
     * @param[in] name 命令名
     * @return true 已注册; false 未注册
     */
    bool has_cmd(const char* name) const;

    /**
     * @brief 获取当前系统中已注册的命令总数
     * @return size_t 已注册命令条目数
     */
    size_t count() const;

private:
    SystemCmd();                                     /**< 构造函数私有化 (纯单例模式) */
    ~SystemCmd() = default;                          /**< 默认析构函数 */
    SystemCmd(const SystemCmd&) = delete;            /**< 禁用拷贝构造 */
    SystemCmd& operator=(const SystemCmd&) = delete; /**< 禁用拷贝赋值 */

    /**
     * @brief 底层统一注册实现
     * @param[in] name     命令名
     * @param[in] wrapper  已完成类型擦除的闭包对象
     * @param[in] args_id  参数类型令牌
     * @param[in] ctx_id   上下文类型令牌
     * @return 状态码 (处理临界区加锁、重名检测、静态表扩容检查)
     */
    int register_raw(const char* name, const RawHandler& wrapper, TypeIdToken args_id,
                     TypeIdToken ctx_id);

    /**
     * @brief 闭包适配器工厂模板
     * @details 利用 C++17 `if constexpr` 编译期静态分支，将任意形态的 Callable 对象
     *          自适应包装为统一的 RawHandler 签名。
     * @tparam Args 参数类型
     * @tparam Ctx  上下文类型
     * @tparam F    原始 Callable 对象的类型
     * @param[in] f 外部传入的可调用对象 (通过右值引用完美转发)
     * @return RawHandler 擦除具体类型后的统一包装闭包
     * @note 针对入参 `Args` 强制要求必须是平凡可复制类型 (Trivially Copyable)。
     *       内部采用栈上对齐缓冲区与字节拷贝提取入参，避免外部未对齐内存直接解引用导致硬件异常。
     */
    template <typename Args, typename Ctx, typename F>
    static RawHandler make_wrapper(F&& f)
    {
        auto wrapper = [fn = etl::forward<F>(f)](const void* arg, size_t len, void* ctx) -> bool
        {
            /* 分支 1: 无参数命令 (Args == void) */
            if constexpr (etl::is_same_v<Args, void>)
            {
                if constexpr (etl::is_same_v<Ctx, void>)
                {
                    /* 纯无参调用: fn() */
                    return fn();
                }
                else
                {
                    /* 仅上下文调用: fn(Ctx*) */
                    return (ctx != nullptr) && fn(static_cast<Ctx*>(ctx));
                }
            }
            /* 分支 2: 带参数命令 (Args != void) */
            else
            {
                /* 编译期约束: 入参类型必须满足平凡可复制 (POD 特性) */
                static_assert(etl::is_trivially_copyable_v<Args>,
                              "Args must be trivially copyable");

                /* 运行时基础安全校验: 严禁空指针，且传入字节流长度必须覆盖结构体大小 */
                if ((arg == nullptr) || (len < sizeof(Args)))
                    return false;

                /*
                 * 直接从原始字节流强转指针可能遭遇未对齐访问 (Unaligned Access)。
                 * 此处通过声明与 Args 对齐边界完全一致的栈缓冲区，完成提取。
                 */
                alignas(Args) uint8_t arg_buf[sizeof(Args)];
                MINI_MEM_COPY(arg_buf, arg, sizeof(Args));
                const Args& typed_arg = *reinterpret_cast<const Args*>(arg_buf);

                if constexpr (etl::is_same_v<Ctx, void>)
                {
                    /* 无上下文模式: 传递参数，上下文置空 */
                    return fn(typed_arg, nullptr);
                }
                else
                {
                    /* 完整模式: 校验非空后同时传入参数和转换后的上下文指针 */
                    return (ctx != nullptr) && fn(typed_arg, static_cast<Ctx*>(ctx));
                }
            }
        };

        return RawHandler(wrapper);
    }

    /* ---------------------------------------------------------------------- */
    /* 静态分配的命令物理存储表                                               */
    /* ---------------------------------------------------------------------- */
    
    /**
     * @brief 命令实体存储单元
     */
    struct CmdEntry
    {
        char        name[k_max_cmd_name_len]; /**< 拷贝保存的命令名字符串 (零堆分配) */
        HandlerNode node;                     /**< 对应的处理节点 (包装函数与类型令牌) */
    };

    CmdEntry m_entries[k_max_commands]; /**< 连续内存的命令节点静态数组 */
    size_t   m_count;                   /**< 当前数组中已登记的有效命令节点数 */
};