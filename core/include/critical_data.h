#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef __cplusplus
extern "C" 
{
#endif

/*
 * 关键安全变量的双重反码存储 (IEC 60601-1 §14.6.1 / IEC 61508 §7.4.3.2)
 *
 * 应对 Brown-Out / 电压跌落 / 宇宙射线位翻转.
 * 每个关键变量存储正码 + 反码两份副本, 每次读取自动校验.
 *
 * volatile 强制每次从物理 RAM 重读, 防止 GCC -O2/-Os 将
 * 优化删除 (编译器可静态证明此恒真).
 *
 * 用法:
 *   CRITICAL_VAR_DECL(int32_t, g_infusion_rate_ml_h);
 *   CRITICAL_VAR_WRITE(g_infusion_rate_ml_h, 50);
 *
 *   int32_t rate;
 *   if (CRITICAL_VAR_READ(g_infusion_rate_ml_h, &rate))
 {
 *       // 校验通过
 *   } else {
 *       enter_safe_state("CRITICAL_VAR corruption");
 *   }
 */

#define CRITICAL_VAR_DECL(type, name)  \
    volatile type name;                 \
    volatile type name##_inv

#define CRITICAL_VAR_WRITE(name, val)  \
    do {                                \
        (name) = (val);                 \
        (name##_inv) = ~(val);          \
    } while (0)

#define CRITICAL_VAR_READ(name, out)    \
    (((name) == ~(name##_inv))          \
     ? ((void)(*(out) = (name)), true)  \
     : (false))

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

/*
 * C++ 类型安全关键数据防护容器
 *
 * 将正码与 32 位反码分离存储, 检测总线/寄存器级位翻转.
 * 适用于 ≤ 32 位的 POD 类型 (int, float, enum, 小结构体).
 *
 * 用法:
 *   CriticalStorage<uint16_t> target_voltage(0);
 *   target_voltage.set(3300);
 *
 *   bool corrupted;
 *   uint16_t v = target_voltage.get_secure(&corrupted);
 *   if (corrupted) enter_safe_state("voltage corrupted");
 */
template <typename T>
class CriticalStorage {
    static_assert(sizeof(T) <= sizeof(uint32_t),
                  "CriticalStorage: type must fit in 32 bits");
public:
    explicit CriticalStorage(T init_val = T())
    {
        set(init_val);
    }

    void set(T val)
    {
        m_data = val;
        uint32_t raw = 0;
        std::memcpy(&raw, &val, sizeof(T));
        m_inv_data = ~raw;
    }

    bool validate() const
    {
        uint32_t raw = 0;
        std::memcpy(&raw, &m_data, sizeof(T));
        return raw == ~m_inv_data;
    }

    T get_secure(bool* is_corrupted = nullptr) const
    {
        if (!validate())
        {
            if (is_corrupted) *is_corrupted = true;
            return T(0);
        }
        if (is_corrupted) *is_corrupted = false;
        return m_data;
    }

private:
    volatile T          m_data;
    volatile uint32_t   m_inv_data;
};

#endif /* __cplusplus */


