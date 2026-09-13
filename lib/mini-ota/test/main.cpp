/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file main.cpp
 * @brief PC 端 OTA 综合测试台（宿主机编译运行，与固件工程解耦）。
 *
 * 设计要点
 *  1) 单入口 + 运行时菜单：一个可执行文件，启动后选择要跑的测试项
 *     （布局/读写保真/镜像校验/下载/OTA 全流程/掉电异常…）。
 *  2) 跑真实代码路径：调用的是 bootutil 的 start.c / read.c / flash.c /
 *     ota_state*.c，不是复刻逻辑；介质用 RAM 模拟 NOR flash
 *     （write 按位相与、erase 置 0xFF）。
 *  3) setjmp 模拟启动跳转：Cortex-M 上是「boot 校验向量表后切栈跳 app」，
 *     PC 上没有真正的栈切换，这里用 setjmp/longjmp 表示同一次上电里
 *     「bootloader → 当前分区 app」的跳转。跳哪片由状态字 bit6 决定，
 *     boot 阶段仍真实执行 mini_boot_state_load()（含 pending 回滚判定）。
 *
 * 构建:
 *   cmake -S test -B test/build -G Ninja
 *   cmake --build test/build
 * 运行(文件类测试按相对路径读 bin, 建议在 test 目录下运行):
 *   .\build\main.exe
 */
#include "boot_config.h"
#include "crc.h"
#include "err.h"
#include "flash.h"
#include "ota_state.h"
#include "read.h"
#include "start.h"
#if IMAGE_CRYPTO_ENABLE
#include "algorithm.h" /* 测试自造密文/摘要（仅开加密时） */
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <setjmp.h>
#include <string>
#include <string_view>
#include <vector>

/* ====================================================================== */
/* 常量：flash 容量与区域布局参数                                          */
/* ====================================================================== */
constexpr uint32_t kSize_4K  = 1u << 12;        /* 4KB */
constexpr uint32_t kSize_16K = 1u << 14;        /* 16KB */
constexpr uint32_t kSize_1K  = 1u << 10;        /* 1KB */

constexpr uint32_t kStateSize      = kSize_1K;  /* 状态区大小 */
constexpr uint32_t kBootloaderSize = kSize_4K;  /* 引导加载程序大小 */
constexpr uint32_t kImage0Size     = kSize_16K; /* 分区1(image_0)大小 */
constexpr uint32_t kImage1Size     = kSize_16K; /* 分区2(image_1)大小 */
constexpr uint32_t kFlashSize      = 128u * 1024u; /* 模拟 flash 总容量 128KB */
constexpr uint32_t kGapMinSize     = 4u * 1024u;   /* 相邻区域最小随机偏移 4KB */
constexpr uint32_t kGapMaxSize     = 32u * 1024u;  /* 相邻区域最大随机偏移 32KB */
constexpr uint32_t kLayoutSeed     = 0x27A5u;      /* 固定种子：随机布局可复现 */

constexpr std::string_view kProgrom_name = "pc simulation test program";

/* ====================================================================== */
/* 平台层：单块连续 RAM 模拟 flash，各区域靠 fa_offset 落到这里            */
/* ====================================================================== */
static std::vector<uint8_t> g_flash(kFlashSize, 0xFFu);
static std::array<flash_area_t, 4> g_areas{};
static bool g_layout_compact = false; /* true 表示随机 gap 放不下，退回了紧凑布局 */

static int memory_open(uint32_t fa_id, const flash_area_t **area)
{
    if (area == nullptr)
    {
        return ERR_ARG;
    }
    for (const auto &a : g_areas)
    {
        if (a.fa_id == fa_id)
        {
            *area = &a;
            return ERR_OK;
        }
    }
    return ERR_NOT_SUPPORTED;
}

static int memory_erase(const flash_area_t *area, uint32_t off, uint32_t len)
{
    if ((area == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    std::fill_n(g_flash.begin() + area->fa_offset + off, len, 0xFFu);
    return ERR_OK;
}

static int memory_write(const flash_area_t *area, uint32_t off, const void *buf, uint32_t len)
{
    const auto *p = static_cast<const uint8_t *>(buf);

    if ((area == nullptr) || (p == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    for (uint32_t i = 0u; i < len; i++)
    {
        g_flash[area->fa_offset + off + i] &= p[i]; /* NOR：只能把 1 写成 0 */
    }
    return ERR_OK;
}

static int memory_read(const flash_area_t *area, uint32_t off, void *buf, uint32_t len)
{
    auto *p = static_cast<uint8_t *>(buf);

    if ((area == nullptr) || (p == nullptr) || ((off + len) > area->fa_size))
    {
        return ERR_ARG;
    }
    std::copy_n(g_flash.begin() + area->fa_offset + off, len, p);
    return ERR_OK;
}

static int memory_get_sectors(const flash_area_t *area, uint32_t max_count,
                              flash_sector_t *sectors, uint32_t *count)
{
    if ((area == nullptr) || (sectors == nullptr) || (count == nullptr) || (max_count < 1u))
    {
        return ERR_ARG;
    }
    sectors[0].fs_off = 0u;
    sectors[0].fs_size = area->fa_size;
    *count = 1u;
    return ERR_OK;
}

static const flash_ops_t g_flash_ops = { memory_open, memory_erase, memory_write,
                                         memory_read, memory_get_sectors };

/* ====================================================================== */
/* 区域布局：各区域按 4KB 对齐，除首个区域外带随机 gap（模拟不连续 flash） */
/* ====================================================================== */
static void flash_layout_build(uint32_t seed)
{
    static const uint32_t ids[] =
    {
        static_cast<uint32_t>(FLASH_AREA_ID_BOOTLOADER),
        static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0),
        static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1),
        static_cast<uint32_t>(FLASH_AREA_ID_STATE),
    };
    static const uint32_t sizes[] =
    {
        kBootloaderSize, kImage0Size, kImage1Size, kStateSize,
    };

    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> gap_sectors(kGapMinSize / kSize_4K,
                                                        kGapMaxSize / kSize_4K);
    uint32_t cursor = 0u;
    bool ok = true;

    for (size_t i = 0u; i < g_areas.size(); ++i)
    {
        if (i != 0u)
        {
            cursor += gap_sectors(rng) * kSize_4K; /* 在上一区域末尾基础上随机偏移 */
        }
        if ((cursor + sizes[i]) > kFlashSize)
        {
            ok = false;
            break;
        }
        g_areas[i] = flash_area_t{ ids[i], 0u, cursor, sizes[i] };
        cursor += sizes[i];
    }

    if (!ok)
    {
        /* 该种子下 128KB 放不下：退回紧凑排列，保证测试台总能跑起来 */
        cursor = 0u;
        for (size_t i = 0u; i < g_areas.size(); ++i)
        {
            g_areas[i] = flash_area_t{ ids[i], 0u, cursor, sizes[i] };
            cursor += sizes[i];
        }
        g_layout_compact = true;
    }
}

static const char *area_name(uint32_t fa_id)
{
    switch (fa_id)
    {
    case static_cast<uint32_t>(FLASH_AREA_ID_BOOTLOADER): return "BOOTLOADER";
    case static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0):    return "IMAGE_0   ";
    case static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1):    return "IMAGE_1   ";
    case static_cast<uint32_t>(FLASH_AREA_ID_STATE):      return "STATE     ";
    default:                                              return "?         ";
    }
}

static void flash_layout_dump()
{
    std::cout << "flash 总容量: " << kFlashSize << " B (" << (kFlashSize / 1024u) << " KB)"
              << (g_layout_compact ? "（随机 gap 放不下, 已退回紧凑布局）" : "") << "\n";
    for (const flash_area_t &a : g_areas)
    {
        std::cout << "  " << area_name(a.fa_id)
                  << " id=" << a.fa_id
                  << " offset=0x" << std::hex << a.fa_offset
                  << " size=0x" << a.fa_size
                  << " [0x" << a.fa_offset << ", 0x" << (a.fa_offset + a.fa_size) << ")"
                  << std::dec << "\n";
    }
}

/* ====================================================================== */
/* 工具：CRC / 固件 / 镜像构造 / 读取回调 / 区域读写                        */
/* ====================================================================== */
static uint32_t crc32_of(const std::vector<uint8_t> &data)
{
    crc_stream_t s;

    crc_stream_start(&s, CRC_MODEL_INIT, CRC_MODEL_REFIN, CRC_MODEL_REFOUT,
                     CRC_MODEL_XOR_OUT, CRC_MODEL_POLY, CRC_MODEL_WIDTH);
    if (!data.empty())
    {
        crc_stream_feed(&s, data.data(), data.size());
    }
    return crc_stream_finish(&s);
}

static std::vector<uint8_t> make_firmware(uint8_t seed, size_t len)
{
    std::vector<uint8_t> fw(len);

    for (size_t i = 0u; i < len; i++)
    {
        fw[i] = static_cast<uint8_t>(seed + i);
    }
    return fw;
}

/* 按 read.h 的布局打包一份 CRC 模式镜像（等价于 tools/main.py 的 CRC 产物） */
static std::vector<uint8_t> build_crc_image(const std::vector<uint8_t> &payload,
                                            std::string_view version,
                                            std::string_view tag,
                                            bool is_front)
{
    const uint32_t crc = crc32_of(payload);
    std::vector<uint8_t> img;

    auto put_le32 = [&img](uint32_t v)
    {
        img.push_back(static_cast<uint8_t>(v & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    };

    if (is_front) /* crc | version | tag | payload | meta */
    {
        put_le32(crc);
        img.insert(img.end(), version.begin(), version.end());
        img.insert(img.end(), tag.begin(), tag.end());
        img.insert(img.end(), payload.begin(), payload.end());
    }
    else /* payload | version | tag | crc | meta */
    {
        img.insert(img.end(), payload.begin(), payload.end());
        img.insert(img.end(), version.begin(), version.end());
        img.insert(img.end(), tag.begin(), tag.end());
        put_le32(crc);
    }

    /* 末尾自描述 meta: magic | mode|0x80(is_front) | version_len | tag_len */
    img.push_back(static_cast<uint8_t>(IMAGE_META_MAGIC));
    img.push_back(static_cast<uint8_t>(static_cast<uint8_t>(IMAGE_CHECK_CRC) |
                                       (is_front ? 0x80u : 0u)));
    img.push_back(static_cast<uint8_t>(version.size()));
    img.push_back(static_cast<uint8_t>(tag.size()));
    return img;
}

/* 构造任意模式的镜像（is_front=1）：crc | version | tag | aux | payload | meta */
static std::vector<uint8_t> build_image(image_check_t mode,
                                        const std::vector<uint8_t> &aux,
                                        const std::vector<uint8_t> &payload,
                                        std::string_view version,
                                        std::string_view tag,
                                        uint32_t crc)
{
    std::vector<uint8_t> img;

    auto put_le32 = [&img](uint32_t v)
    {
        img.push_back(static_cast<uint8_t>(v & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
        img.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    };

    put_le32(crc);
    img.insert(img.end(), version.begin(), version.end());
    img.insert(img.end(), tag.begin(), tag.end());
    img.insert(img.end(), aux.begin(), aux.end());
    img.insert(img.end(), payload.begin(), payload.end());

    img.push_back(static_cast<uint8_t>(IMAGE_META_MAGIC));
    img.push_back(static_cast<uint8_t>(static_cast<uint8_t>(mode) | 0x80u));
    img.push_back(static_cast<uint8_t>(version.size()));
    img.push_back(static_cast<uint8_t>(tag.size()));
    return img;
}

/* 从内存镜像读取（image_read_fn） */
static int vector_read_fn(void *ctx, uint32_t offset, uint8_t *buf, uint32_t len)
{
    const auto *v = static_cast<const std::vector<uint8_t> *>(ctx);

    if ((v == nullptr) || (buf == nullptr) ||
        ((static_cast<size_t>(offset) + len) > v->size()))
    {
        return ERR_ARG;
    }
    std::memcpy(buf, v->data() + offset, len);
    return ERR_OK;
}

/* 下载钩子：从内存镜像流式喂数据（可模拟传输失败 / 中途掉电） */
struct stream_src_t
{
    const std::vector<uint8_t> *img = nullptr;
    size_t pos = 0u;
    bool fail_now = false;
    int fail_after_chunks = -1; /* <0：从不失败；>=0：成功喂完这么多块后失败 */
    int chunks_done = 0;
};

static int stream_feed_hook(void *param, uint8_t *buf, uint32_t want, int *out_len)
{
    auto *s = static_cast<stream_src_t *>(param);

    if ((s == nullptr) || (s->img == nullptr) || (buf == nullptr) || (out_len == nullptr))
    {
        return ERR_ARG;
    }
    if (s->fail_now || ((s->fail_after_chunks >= 0) && (s->chunks_done >= s->fail_after_chunks)))
    {
        return ERR_TRANSMIT; /* 模拟传输中断 / 掉电 */
    }

    const size_t left = s->img->size() - s->pos;
    const uint32_t n = static_cast<uint32_t>(std::min<size_t>(want, left));

    if (n == 0u)
    {
        return ERR_ARG;
    }
    std::memcpy(buf, s->img->data() + s->pos, n);
    s->pos += n;
    ++s->chunks_done;
    *out_len = static_cast<int>(n);
    return ERR_OK;
}

static std::vector<uint8_t> area_read(uint32_t fa_id, size_t len)
{
    const flash_area_t *a = nullptr;
    std::vector<uint8_t> out(len);

    if ((flash_area_open(fa_id, &a) != ERR_OK) ||
        (flash_area_read_operation(a, 0u, out.data(), static_cast<uint32_t>(len)) != ERR_OK))
    {
        return {};
    }
    return out;
}

static bool area_program(uint32_t fa_id, const std::vector<uint8_t> &data)
{
    const flash_area_t *a = nullptr;

    if (flash_area_open(fa_id, &a) != ERR_OK)
    {
        return false;
    }
    if (flash_area_erase_operation(a, 0u, a->fa_size) != ERR_OK)
    {
        return false;
    }
    return flash_area_write_operation(a, 0u, data.data(),
                                      static_cast<uint32_t>(data.size())) == ERR_OK;
}

/* 全片擦除 = 恢复出厂（区域布局本身不动） */
static void reset_flash()
{
    std::fill(g_flash.begin(), g_flash.end(), 0xFFu);
}

/* 只擦状态区 = 模拟“全新设备 / 状态丢失” */
static void reset_state_medium()
{
    const flash_area_t *a = nullptr;

    if (flash_area_open(FLASH_AREA_ID_STATE, &a) == ERR_OK)
    {
        (void)flash_area_erase_operation(a, 0u, a->fa_size);
    }
}

static std::string hex_byte(uint8_t v)
{
    char buf[8];

    std::snprintf(buf, sizeof(buf), "0x%02x", static_cast<unsigned>(v));
    return std::string(buf);
}

/* ====================================================================== */
/* 启动模拟：setjmp/longjmp 表示「bootloader → 分区 app」的跳转            */
/* ====================================================================== */
using pc_app_fn = void (*)(void *arg, uint32_t partition);

static jmp_buf s_app_ctx[2];      /* 两片镜像区各自的“复位入口” */
static jmp_buf s_boot_ctx;        /* bootloader 的“复位点” */
static pc_app_fn s_app_fn = nullptr;
static void *s_app_arg = nullptr;
static uint32_t s_jump_partition = 0u;
static bool s_app_ran = false;

/* boot 按状态字 bit6 决定跳哪片（与平台侧组装 app_area 的判定同款） */
static uint32_t boot_target_partition()
{
    return (ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1) ? 1u : 0u;
}

/**
 * @brief 模拟一次上电：bootloader 恢复状态 → 跳转当前分区 app → app 跑完回到调用者
 * @param app_fn 分区 app 的行为（被“跳转”后执行），可为 nullptr
 * @param arg    透传给 app_fn
 * @return 本次实际启动的分区（0=image_0, 1=image_1）
 * @note  与真实复位的差异：进程内 static 不会被清零，所以 boot 阶段显式把
 *        开关位（OTA/回滚）清零，再由 mini_boot_state_load() 重载持久位
 */
static uint32_t pc_power_on(pc_app_fn app_fn, void *arg)
{
    s_app_fn = app_fn;
    s_app_arg = arg;
    s_app_ran = false;
    s_jump_partition = 0u;

    /* 安装两片镜像区的“复位入口”：setjmp 首返回 0 = 只安装；
     * 被 boot 的 longjmp 打进来（返回非 0）= 该分区 app 被启动 */
    if (setjmp(s_app_ctx[0]) != 0)
    {
        if (!s_app_ran)
        {
            s_app_ran = true;
            if (s_app_fn != nullptr)
            {
                s_app_fn(s_app_arg, 0u);
            }
        }
        longjmp(s_boot_ctx, 1);
    }
    if (setjmp(s_app_ctx[1]) != 0)
    {
        if (!s_app_ran)
        {
            s_app_ran = true;
            if (s_app_fn != nullptr)
            {
                s_app_fn(s_app_arg, 1u);
            }
        }
        longjmp(s_boot_ctx, 1);
    }

    /* bootloader 阶段：模拟上电 RAM 归零 + 恢复持久状态（pending 则回滚） */
    if (setjmp(s_boot_ctx) == 0)
    {
        ota_close();
        ota_rollback_close();
        (void)mini_boot_state_load();

        s_jump_partition = boot_target_partition();
        longjmp(s_app_ctx[s_jump_partition], 1); /* 模拟向量表跳转，不返回 */
    }

    /* 走到这里 = 分区 app 已跑完并“复位”回到 boot 之后 */
    return s_jump_partition;
}

/* app 上下文：被“跳转”后执行的行为（打印 + 可选开 OTA / 确认新镜像） */
struct app_ctx_t
{
    std::string tag;
    bool do_open = false;
};

static void app_report(void *arg, uint32_t partition)
{
    auto *ctx = static_cast<app_ctx_t *>(arg);

    std::cout << "    [app@image_" << partition << "] 运行中";
    if ((ctx != nullptr) && !ctx->tag.empty())
    {
        std::cout << " (" << ctx->tag << ")";
    }
    std::cout << "\n";

    if (ctx != nullptr)
    {
        if (ctx->do_open)
        {
            ota_open();
            ota_rollback_open();
            std::cout << "    [app@image_" << partition << "] 打开 OTA + 回滚开关\n";
        }
    }
}

/* 上电一次并打印流程（返回实际启动的分区） */
static uint32_t power_on_boot(std::string_view tag, bool app_open_ota = false)
{
    app_ctx_t ctx;
    ctx.tag = std::string(tag);
    ctx.do_open = app_open_ota;

    std::cout << "  · " << tag << " : bootloader 恢复状态并选择分区\n";
    const uint32_t part = pc_power_on(app_report, &ctx);
    std::cout << "  · " << tag << " : 本轮到 image_" << part << " 运行\n";
    return part;
}

/* ====================================================================== */
/* 断言统计                                                                */
/* ====================================================================== */
static int s_fail = 0;

static void check(bool cond, const std::string &msg)
{
    std::cout << (cond ? "  [ok]   " : "  [FAIL] ") << msg << "\n";
    if (!cond)
    {
        ++s_fail;
    }
}

static int test_result(std::string_view name)
{
    if (s_fail != 0)
    {
        std::cout << name << ": FAIL (" << s_fail << " 项未通过)\n";
        return 1;
    }
    std::cout << name << ": PASS\n";
    return 0;
}

/* ====================================================================== */
/* 平台初始化                                                              */
/* ====================================================================== */
static void platform_init()
{
    flash_layout_build(kLayoutSeed);
    reset_flash();

    const int rc_ops = flash_ops_register(&g_flash_ops);
    const int rc_state = ota_state_flash_register();

    if (rc_ops != ERR_OK || rc_state != ERR_OK)
    {
        std::cerr << "[平台] 初始化失败: flash_ops=" << err_str(rc_ops)
                  << " state=" << err_str(rc_state) << "\n";
    }
}

/* ====================================================================== */
/* 测试 1：flash 区域布局                                                  */
/* ====================================================================== */
static void test_layout()
{
    std::cout << "\n=== 1. flash 区域布局 ===\n";
    s_fail = 0;

    flash_layout_dump();

    uint32_t prev_end = 0u;
    for (size_t i = 0u; i < g_areas.size(); ++i)
    {
        const flash_area_t &a = g_areas[i];

        check((a.fa_offset % kSize_4K) == 0u,
              std::string(area_name(a.fa_id)) + " 起始地址 4KB 对齐");
        check((a.fa_offset + a.fa_size) <= kFlashSize,
              std::string(area_name(a.fa_id)) + " 落在 flash 容量内");
        if (i != 0u)
        {
            check(a.fa_offset >= prev_end, std::string(area_name(a.fa_id)) + " 不与前一区域重叠");
        }
        prev_end = a.fa_offset + a.fa_size;
    }

    test_result("区域布局测试");
}

/* ====================================================================== */
/* 测试 2：flash 读写擦除保真（NOR 语义）                                   */
/* ====================================================================== */
static void test_flash_fidelity()
{
    std::cout << "\n=== 2. flash 读写擦除保真 ===\n";
    s_fail = 0;

    const flash_area_t *a = nullptr;
    constexpr uint32_t kLen = 256u;

    check(flash_area_open(FLASH_AREA_ID_IMAGE_0, &a) == ERR_OK, "打开 image_0 区域");

    std::vector<uint8_t> buf(kLen, 0x00u);

    check(flash_area_erase_operation(a, 0u, kLen) == ERR_OK, "擦除 256B");
    check(flash_area_read_operation(a, 0u, buf.data(), kLen) == ERR_OK, "读回擦除结果");
    check(std::all_of(buf.begin(), buf.end(), [](uint8_t v) { return v == 0xFFu; }),
          "擦除后全部为 0xFF");

    std::vector<uint8_t> pattern(kLen);
    for (uint32_t i = 0u; i < kLen; ++i)
    {
        pattern[i] = static_cast<uint8_t>(i);
    }
    check(flash_area_write_operation(a, 0u, pattern.data(), kLen) == ERR_OK, "写入 pattern");
    check(flash_area_read_operation(a, 0u, buf.data(), kLen) == ERR_OK, "读回 pattern");
    check(buf == pattern, "写入内容逐字节一致");

    /* NOR 语义：只能 1→0，再写 0x00 等价于按位相与 */
    std::vector<uint8_t> zeros(kLen, 0x00u);
    check(flash_area_write_operation(a, 0u, zeros.data(), kLen) == ERR_OK, "重复写入 0x00");
    check(flash_area_read_operation(a, 0u, buf.data(), kLen) == ERR_OK, "读回覆盖结果");
    check(std::all_of(buf.begin(), buf.end(), [](uint8_t v) { return v == 0x00u; }),
          "NOR 只能把 1 写成 0（覆盖写入生效）");

    check(flash_area_read_operation(a, a->fa_size - 1u, buf.data(), 4u) == ERR_ARG,
          "越界读取被拒绝 (ERR_ARG)");
    check(flash_area_write_operation(a, a->fa_size - 1u, pattern.data(), 4u) == ERR_ARG,
          "越界写入被拒绝 (ERR_ARG)");

    test_result("flash 保真测试");
}

/* ====================================================================== */
/* 测试 3：镜像解析 / 校验（内存构造 + 篡改检测）                            */
/* ====================================================================== */
static void test_image_verify()
{
    std::cout << "\n=== 3. 镜像解析与校验（内存） ===\n";
    s_fail = 0;

    const std::vector<uint8_t> fw = make_firmware(0x30u, 700u);
    std::vector<uint8_t> img = build_crc_image(fw, "1.0.0", "pc-mem", true);
    image_read_cfg_t cfg = {};

    image_view_t view = {};
    check(image_parse(img.data(), img.size(), &view) == ERR_OK, "镜像 meta 解析成功");
    check(view.mode == IMAGE_CHECK_CRC, "模式 = CRC");
    check(view.payload_len == fw.size(), "payload 长度与固件一致");
    check(view.crc_stored == crc32_of(fw), "镜像记录的 CRC 与实算一致");
    std::cout << "         version=\"" << std::string(reinterpret_cast<const char *>(view.version), view.version_len)
              << "\" tag=\"" << std::string(reinterpret_cast<const char *>(view.tag), view.tag_len)
              << "\" payload=" << view.payload_len << "B\n";

    /* 流式校验（boot 校验新镜像走的路径） */
    std::vector<uint8_t> scratch(MINI_BOOT_LOAD_MAX);
    uint32_t crc = 0u;
    check(image_verify_stream(vector_read_fn, &img, static_cast<uint32_t>(img.size()), &cfg,
                              scratch.data(), static_cast<uint32_t>(scratch.size()), &crc) == ERR_OK,
          "流式校验通过");
    check(crc == view.crc_stored, "流式校验读回的 CRC 与镜像记录一致");

    /* 一次性接口取明文并比对 */
    std::vector<uint8_t> plain(img.size());
    size_t plain_len = 0u;
    uint32_t crc_calc = 0u;
    check(image_read_payload(img.data(), img.size(), &cfg, plain.data(), plain.size(),
                             &view, &plain_len, &crc_calc) == ERR_OK, "解析并取明文成功");
    check(plain_len == fw.size(), "明文长度与固件一致");
    check(std::equal(fw.begin(), fw.end(), plain.begin()), "明文内容与固件逐字节一致");

    /* 后置布局（is_front=false）同样应通过 */
    const std::vector<uint8_t> img_behind = build_crc_image(fw, "1.0.0", "behind", false);
    check(image_verify_stream(vector_read_fn, const_cast<std::vector<uint8_t> *>(&img_behind),
                              static_cast<uint32_t>(img_behind.size()), &cfg,
                              scratch.data(), static_cast<uint32_t>(scratch.size()), nullptr) == ERR_OK,
          "元数据后置布局同样校验通过");

    /* 篡改 payload 尾字节：校验必须失败 */
    std::vector<uint8_t> bad = img;
    bad[bad.size() - IMAGE_META_LEN - 1u] ^= 0xFFu;
    check(image_verify_stream(vector_read_fn, &bad, static_cast<uint32_t>(bad.size()), &cfg,
                              scratch.data(), static_cast<uint32_t>(scratch.size()), nullptr) == ERR_CRC_MISMATCH,
          "篡改 payload → ERR_CRC_MISMATCH");
    check(image_read_payload(bad.data(), bad.size(), &cfg, plain.data(), plain.size(),
                             nullptr, nullptr, nullptr) == ERR_CRC_MISMATCH,
          "篡改 payload → 一次性接口同样拒绝");

    /* 非法 meta */
    std::vector<uint8_t> junk = { 0x00u, 0x01u, 0x02u, 0x03u };
    check(image_parse(junk.data(), junk.size(), &view) == ERR_ARG, "非镜像数据 → ERR_ARG");

    test_result("镜像校验测试");
}

/* 去掉 UTF-8 BOM 与首尾空白（含 Windows 的 '\r'），避免管道/终端带入脏字符 */
static void sanitize_line(std::string &s)
{
    if ((s.size() >= 3u) &&
        (static_cast<unsigned char>(s[0]) == 0xEFu) &&
        (static_cast<unsigned char>(s[1]) == 0xBBu) &&
        (static_cast<unsigned char>(s[2]) == 0xBFu))
    {
        s.erase(0u, 3u);
    }

    const size_t b = s.find_first_not_of(" \t\r\n");

    if (b == std::string::npos)
    {
        s.clear();
        return;
    }
    s = s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1u);
}

/* ====================================================================== */
/* 交互：单步模式 + 精简状态快照                                           */
/* ====================================================================== */
static bool g_step_mode = true; /* true=流程测试逐步暂停；false=一键跑完 */

/* 精简快照：状态字分解 + 各镜像区是否已烧录 */
static void dump_state()
{
    const uint8_t status = mini_boot_get_ota_status();

    std::cout << "     状态字 open=" << static_cast<int>(ota_is_open())
              << " rollback=" << static_cast<int>(ota_is_rollback())
              << " fail=" << static_cast<int>(ota_fail_get())
              << " current=image_" << static_cast<int>(ota_current_partition_get())
              << " pending=" << static_cast<int>(ota_is_pending())
              << " trial=" << static_cast<int>(ota_is_trial())
              << " [0x" << hex_byte(status) << "]\n";

    const uint32_t ids[2] = {
        static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_0),
        static_cast<uint32_t>(FLASH_AREA_ID_IMAGE_1),
    };
    for (const uint32_t id : ids)
    {
        const flash_area_t *a = nullptr;
        uint8_t head[8] = { 0u };

        if (flash_area_open(id, &a) != ERR_OK)
        {
            continue;
        }
        if (flash_area_read_operation(a, 0u, head, static_cast<uint32_t>(sizeof(head))) != ERR_OK)
        {
            continue;
        }
        const bool blank = std::all_of(head, head + sizeof(head),
                                       [](uint8_t v) { return v == 0xFFu; });
        std::cout << "     flash " << area_name(id) << (blank ? "空白" : "已烧录");
        if (!blank)
        {
            std::cout << "  头=";
            for (uint8_t b : head)
            {
                std::cout << hex_byte(b) << ' ';
            }
        }
        std::cout << "\n";
    }
}

/* 关键步骤之间暂停：回车继续；输入 q/Q 关闭单步（本次测试剩余部分连续跑完） */
static void step(const std::string &title)
{
    if (!g_step_mode)
    {
        return;
    }
    std::cout << "\n  >>> " << title << "\n";
    dump_state();
    std::cout << "      [回车=继续下一步 | q=本次连续跑完]\n";

    std::string line;
    if (!std::getline(std::cin, line))
    {
        return;
    }
    sanitize_line(line);
    if (line == "q" || line == "Q")
    {
        g_step_mode = false;
    }
}

/* ====================================================================== */
/* 测试 4：下载镜像（boot → app 开 OTA → 流式写入非当前分区）                */
/* ====================================================================== */
static void test_download()
{
    std::cout << "\n=== 4. 下载镜像测试 ===\n";
    s_fail = 0;

    reset_flash();
    reset_state_medium();

    const std::vector<uint8_t> fw_old = make_firmware(0x10u, 700u);
    const std::vector<uint8_t> img_old = build_crc_image(fw_old, "1.0.0", "old", true);
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "出厂烧录 image_0 (1.0.0)");
    step("出厂已烧录 image_0 (1.0.0)");

    /* 上电进入 app，app 里打开 OTA（真实场景 app 启动后才允许升级） */
    const uint32_t part = power_on_boot("boot", true);
    check(part == 0u, "启动跳转到 image_0");
    step("上电进入 app 并打开 OTA");

    const std::vector<uint8_t> fw_new = make_firmware(0x40u, 700u);
    const std::vector<uint8_t> img_new = build_crc_image(fw_new, "2.0.0", "new", true);

    stream_src_t src;
    src.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &src,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "流式下载成功 (payload 700B > MINI_BOOT_LOAD_MAX, 分多块)");
    check(src.chunks_done > 1, "确认走了多块传输 (chunks=" + std::to_string(src.chunks_done) + ")");

    check(area_read(FLASH_AREA_ID_IMAGE_1, img_new.size()) == img_new,
          "image_1 内容与下载镜像逐字节一致");
    check(area_read(FLASH_AREA_ID_IMAGE_0, img_old.size()) == img_old,
          "image_0 未被改写");
    step("下载完成：image_1 已写入新镜像");

    /* 下载后校验 + 激活 → 下一次启动应跳到 image_1 */
    check(mini_boot_start_ota() == ERR_OK, "校验通过并激活 image_1");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "状态字 bit6 = image_1");
    check(ota_is_pending() == 1u, "激活后 pending == 1");
    step("校验通过并激活 image_1（pending 置位）");

    /* app 运行中确认新镜像：不确认则下次上电会按 pending 回滚 */
    check(mini_boot_confirm_ota() == ERR_OK, "app 确认新镜像 (清 pending)");
    check(ota_is_pending() == 0u, "确认后 pending == 0");
    step("app 确认新镜像（pending 清零）");

    const uint32_t next = power_on_boot("reboot");
    check(next == 1u, "再次上电跳到 image_1");
    step("复位后跳到 image_1");

    test_result("下载测试");
}

/* ====================================================================== */
/* 测试 5：OTA 全流程（激活 / 确认 / 回滚）                                  */
/* ====================================================================== */
static void test_ota_flow()
{
    std::cout << "\n=== 5. OTA 全流程测试 ===\n";
    s_fail = 0;

    constexpr size_t kPayloadLen = 700u;

    reset_flash();
    reset_state_medium();

    /* 出厂已烧 image_0：旧固件 1.0.0 */
    const std::vector<uint8_t> fw_old = make_firmware(0x10u, kPayloadLen);
    const std::vector<uint8_t> img_old = build_crc_image(fw_old, "1.0.0", "old", true);
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "factory: image_0 programmed");
    step("出厂已烧录 image_0 (1.0.0)");

    /* ---- 1) 首次上电：无状态记录 → 默认 current=image_0 ---- */
    uint32_t part = power_on_boot("boot#1 首次上电");
    check(part == 0u, "boot#1: 跳到 image_0");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "boot#1: current == image_0");
    check(ota_is_pending() == 0u, "boot#1: pending == 0");
    check(ota_fail_get() == OTA_FAIL_NONE, "boot#1: fail == NONE");
    check(ota_is_double() == 1u, "编译期能力: 双分区");
    step("首次上电：无状态记录，默认 current=image_0");

    /* ---- 2) app 开 OTA + 回滚，下载 2.0.0 到非当前分区 ---- */
    part = power_on_boot("boot#2 app 开 OTA", true);
    check(part == 0u, "boot#2: 仍跳 image_0");
    check(ota_is_open() == 1u, "app: OTA 已开");
    check(ota_is_rollback() == 1u, "app: 回滚已开");

    const std::vector<uint8_t> fw_new = make_firmware(0x40u, kPayloadLen);
    const std::vector<uint8_t> img_new = build_crc_image(fw_new, "2.0.0", "new", true);
    stream_src_t src;
    src.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &src,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "download: 流式下载成功");
    check(area_read(FLASH_AREA_ID_IMAGE_1, img_new.size()) == img_new,
          "download: image_1 字节一致");
    check(area_read(FLASH_AREA_ID_IMAGE_0, img_old.size()) == img_old,
          "download: image_0 未被改写");
    step("下载完成：image_1 已写入 2.0.0");

    /* ---- 3) 校验 + 激活（置 pending） ---- */
    check(mini_boot_start_ota() == ERR_OK, "activate: start_ota ok");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "activate: current == image_1");
    check(ota_is_pending() == 1u, "activate: pending == 1");
    check(ota_fail_get() == OTA_FAIL_NONE, "activate: fail == NONE");
    step("校验通过并激活 image_1（pending=1）");

    /* ---- 4) 复位进新镜像试运行（pending+trial=0 → 首跳放行） ---- */
    part = power_on_boot("boot#3 复位(未确认, 试运行)");
    check(part == 1u, "trial: 首跳放行进 image_1");
    check(ota_is_pending() == 1u, "trial: pending 仍为 1");
    check(ota_is_trial() == 1u, "trial: trial 已置 1");
    check(ota_fail_get() == OTA_FAIL_NONE, "trial: fail == NONE");
    step("未确认首跳 → 放行进 image_1 试运行 (trial=1)");

    /* ---- 4b) 试运行后仍未确认就复位 → 回滚 ---- */
    part = power_on_boot("boot#3b 复位(试运行后仍未确认)");
    check(part == 0u, "rollback: 回滚到 image_0");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "rollback: current == image_0");
    check(ota_is_pending() == 0u, "rollback: pending 已清");
    check(ota_is_trial() == 0u, "rollback: trial 已清");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "rollback: fail == VERIFY");
    step("试运行未确认 → 回滚到 image_0 (fail=VERIFY)");

    /* ---- 5) 再来一轮：app 确认 → 不回滚 ---- */
    part = power_on_boot("boot#4 app 开 OTA", true);
    check(part == 0u, "cycle2: 跳 image_0");
    stream_src_t src2;
    src2.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &src2,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "cycle2: download ok");
    check(mini_boot_start_ota() == ERR_OK, "cycle2: activate ok");
    check(ota_is_pending() == 1u, "cycle2: pending == 1");

    /* app 运行中确认新镜像：不确认则下次上电会像上一段那样回滚 */
    check(mini_boot_confirm_ota() == ERR_OK, "cycle2: app 确认 ok");
    check(ota_is_pending() == 0u, "cycle2: pending 已清");
    step("app 确认新镜像（pending 清零）");

    part = power_on_boot("boot#5 复位(已确认)");
    check(part == 1u, "cycle2: 跳到 image_1");
    check(ota_fail_get() == OTA_FAIL_NONE, "cycle2: fail == NONE");
    step("复位后跳到 image_1（已确认不回滚）");

    /* ---- 6) 坏镜像：校验不过，不激活 ---- */
    part = power_on_boot("boot#6 开 OTA", true);
    check(part == 1u, "bad: 跳 image_1");
    std::vector<uint8_t> img_bad = build_crc_image(make_firmware(0x70u, kPayloadLen), "3.0.0", "bad", true);
    img_bad[img_bad.size() - IMAGE_META_LEN - 1u] ^= 0xFFu; /* 破坏 payload 尾字节 */
    stream_src_t src3;
    src3.img = &img_bad;
    check(mini_boot_source_download_stream(stream_feed_hook, &src3,
                                           static_cast<uint32_t>(img_bad.size())) == ERR_OK,
          "bad: download ok");
    check(mini_boot_start_ota() == ERR_CRC_MISMATCH, "bad: 校验失败 → ERR_CRC_MISMATCH");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "bad: fail == VERIFY");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "bad: current 未改变");
    step("坏镜像：校验失败，current 保持 image_1");

    /* ---- 7) 传输失败：ERR_TRANSMIT + fail=READ ---- */
    stream_src_t src4;
    src4.img = &img_new;
    src4.fail_now = true;
    check(mini_boot_source_download_stream(stream_feed_hook, &src4,
                                           static_cast<uint32_t>(img_new.size())) == ERR_TRANSMIT,
          "xfer: 钩子失败 → ERR_TRANSMIT");
    check(ota_fail_get() == OTA_FAIL_READ, "xfer: fail == READ");

    /* ---- 8) 未开 OTA 时拒绝下载 ---- */
    ota_close();
    stream_src_t src5;
    src5.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &src5,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OTA_OPEN,
          "closed: 未开 OTA 被拒 (ERR_OTA_OPEN)");

    /* ---- 9) app 侧读状态（不做回滚的刷新） ---- */
    check(mini_boot_state_refresh() == ERR_OK, "refresh: app 侧状态刷新 ok");
    check(ota_fail_get() == OTA_FAIL_READ, "refresh: fail 读回 == READ");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_1, "refresh: current 读回 == image_1");
    step("app 侧刷新状态（不做回滚判定）");

    test_result("OTA 全流程测试");
}

/* ====================================================================== */
/* 测试 6：掉电 / 异常场景                                                  */
/* ====================================================================== */
/* 第一条空白记录的位置（用于伪造“状态记录写一半”） */
static size_t state_next_free_off()
{
    const flash_area_t *a = nullptr;
    constexpr size_t kRec = OTA_STATE_RECORD_WORDS * sizeof(uint32_t);

    if (flash_area_open(FLASH_AREA_ID_STATE, &a) != ERR_OK)
    {
        return 0u;
    }
    for (size_t off = 0u; (off + kRec) <= a->fa_size; off += kRec)
    {
        uint32_t w[OTA_STATE_RECORD_WORDS] = { 0u, 0u };

        if (flash_area_read_operation(a, static_cast<uint32_t>(off), w,
                                      static_cast<uint32_t>(sizeof(w))) != ERR_OK)
        {
            return 0u;
        }
        if ((w[0] == 0xFFFFFFFFu) && (w[1] == 0xFFFFFFFFu))
        {
            return off;
        }
    }
    return a->fa_size;
}

/* 伪造“状态记录写一半掉电”：只写状态字，校验字留 0xFF（该条应整体作废） */
static bool torn_state_write(uint32_t state_word)
{
    const flash_area_t *a = nullptr;
    const size_t off = state_next_free_off();
    uint32_t rec[OTA_STATE_RECORD_WORDS] = { 0u, 0u };

    if ((flash_area_open(FLASH_AREA_ID_STATE, &a) != ERR_OK) || (off >= a->fa_size))
    {
        return false;
    }
    ota_state_record_build(rec, state_word);
    return flash_area_write_operation(a, static_cast<uint32_t>(off), &rec[0], 4u) == ERR_OK;
}

static void test_power_loss()
{
    std::cout << "\n=== 6. 掉电与异常场景测试 ===\n";
    s_fail = 0;

    constexpr size_t kPayloadLen = 700u;
    const std::vector<uint8_t> fw_old = make_firmware(0x10u, kPayloadLen);
    const std::vector<uint8_t> img_old = build_crc_image(fw_old, "1.0.0", "old", true);
    const std::vector<uint8_t> fw_new = make_firmware(0x40u, kPayloadLen);
    const std::vector<uint8_t> img_new = build_crc_image(fw_new, "2.0.0", "new", true);

    /* ---- 7.1 状态记录写一半掉电：半写记录作废，旧状态仍生效 ---- */
    reset_flash();
    reset_state_medium();
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "p1: 出厂烧录 image_0");

    uint32_t part = power_on_boot("p1 boot#1 无状态", true);
    check(part == 0u, "p1: 默认跳 image_0");

    stream_src_t src;
    src.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &src,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "p1: 下载 image_1");
    check(mini_boot_start_ota() == ERR_OK, "p1: 激活 image_1");
    check(ota_is_pending() == 1u, "p1: pending == 1");
    step("p1: 激活 image_1（pending=1）");

    check(torn_state_write(OTA_STATE_MASK_CURRENT), "p1: 追加一条半写状态记录");
    step("p1: 伪造半写状态记录（校验字留 0xFF）");
    part = power_on_boot("p1 boot#2 半写状态后复位");
    check(part == 1u, "p1: 半写记录作废, 首跳放行进 image_1");
    check(ota_is_trial() == 1u, "p1: trial == 1");
    step("p1: 半写记录作废，pending 生效首跳放行");

    part = power_on_boot("p1 boot#3 试运行后仍未确认");
    check(part == 0u, "p1: 回滚判定仍生效, 跳 image_0");
    check(ota_is_pending() == 0u, "p1: pending 已清");
    check(ota_is_trial() == 0u, "p1: trial 已清");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "p1: fail == VERIFY");
    step("p1: 试运行未确认 → 回滚到 image_0 (fail=VERIFY)");

    /* ---- 7.2 下载中途掉电：目标区半写，状态不受影响 ---- */
    reset_flash();
    reset_state_medium();
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "p2: 出厂烧录 image_0");

    part = power_on_boot("p2 boot#1 无状态", true);
    check(part == 0u, "p2: 默认跳 image_0");
    {
        stream_src_t s;
        s.img = &img_new;
        s.fail_after_chunks = 1; /* 成功喂 1 块后“掉电” */
        check(mini_boot_source_download_stream(stream_feed_hook, &s,
                                               static_cast<uint32_t>(img_new.size())) == ERR_TRANSMIT,
              "p2: 下载中途掉电 → ERR_TRANSMIT");
    }
    check(ota_fail_get() == OTA_FAIL_READ, "p2: fail == READ");
    step("p2: 下载中途掉电（目标区半写，状态未改）");

    part = power_on_boot("p2 boot#2 掉电后复位");
    check(part == 0u, "p2: 仍启动原镜像 image_0");
    check(ota_is_pending() == 0u, "p2: 无 pending");
    step("p2: 掉电后复位，仍启动 image_0");

    /* 再次上电进入 app 并重新打开 OTA，然后重下一次即可（写入前整体擦除） */
    part = power_on_boot("p2 boot#3 开 OTA", true);
    check(part == 0u, "p2: 重新上电仍跳 image_0");
    stream_src_t s2;
    s2.img = &img_new;
    check(mini_boot_source_download_stream(stream_feed_hook, &s2,
                                           static_cast<uint32_t>(img_new.size())) == ERR_OK,
          "p2: 恢复后重新下载成功");
    check(mini_boot_start_ota() == ERR_OK, "p2: 激活成功");
    check(ota_is_pending() == 1u, "p2: pending == 1");
    step("p2: 重新下载并激活成功");

    /* ---- 7.3 pending 未确认 → 回滚到另一分区 ---- */
    reset_flash();
    reset_state_medium();
    check(area_program(FLASH_AREA_ID_IMAGE_0, img_old), "p3: 出厂烧录 image_0");

    part = power_on_boot("p3 boot#1 无状态", true);
    check(part == 0u, "p3: 默认跳 image_0");
    {
        stream_src_t s;
        s.img = &img_new;
        check(mini_boot_source_download_stream(stream_feed_hook, &s,
                                               static_cast<uint32_t>(img_new.size())) == ERR_OK,
              "p3: 下载 image_1");
    }
    check(mini_boot_start_ota() == ERR_OK, "p3: 激活 image_1");
    check(ota_is_pending() == 1u, "p3: pending == 1");
    step("p3: 激活 image_1 后未确认");

    part = power_on_boot("p3 boot#2 复位(未确认, 试运行)");
    check(part == 1u, "p3: 首跳放行进 image_1");
    check(ota_is_trial() == 1u, "p3: trial == 1");
    step("p3: 未确认首跳放行进 image_1");

    part = power_on_boot("p3 boot#3 试运行后仍未确认");
    check(part == 0u, "p3: 回滚到 image_0");
    check(ota_is_pending() == 0u, "p3: pending 已清");
    check(ota_is_trial() == 0u, "p3: trial 已清");
    check(ota_fail_get() == OTA_FAIL_VERIFY, "p3: fail == VERIFY");
    step("p3: 未确认回滚到 image_0");

    test_result("掉电与异常场景测试");
}

/* ====================================================================== */
/* 测试 7：擦除全片（恢复出厂）                                             */
/* ====================================================================== */
static void test_erase_all()
{
    std::cout << "\n=== 7. 擦除全片 flash ===\n";
    s_fail = 0;

    reset_flash();
    reset_state_medium();

    check(std::all_of(g_flash.begin(), g_flash.end(),
                      [](uint8_t v) { return v == 0xFFu; }),
          "全片已擦除为 0xFF");

    const uint32_t part = power_on_boot("boot(空设备)");
    check(part == 0u, "空设备默认跳 image_0");
    check(ota_current_partition_get() == OTA_STATE_PARTITION_IMAGE_0, "无状态记录 → 默认 image_0");
    step("擦除后首次上电：默认跳 image_0");

    test_result("擦除测试");
}

/* ====================================================================== */
/* 测试 8：备份分区镜像校验（mini_boot_backup，复用 read）               */
/* ====================================================================== */
#if IMAGE_CRYPTO_ENABLE
static void test_backup_crypto()
{
    std::cout << "\n--- 8b. 加密模式备份校验（SHA/GCM/CBC/CBC_SHA）---\n";

    const uint8_t key[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                             0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    const uint8_t nonce[IMAGE_GCM_NONCE_LEN] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    const uint8_t iv[IMAGE_CBC_IV_LEN] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
                                          0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF};
    const std::vector<uint8_t> fw = make_firmware(0x5Au, 64u);

    mini_boot_backup_param_t p = {};
    p.partition = static_cast<int>(FLASH_AREA_ID_IMAGE_1);
    p.key = key;
    p.key_len = sizeof(key);

    /* --- SHA --- */
    {
        uint8_t digest[IMAGE_HASH_LEN];
        sha256_stream_t sh;
        check(sha256_begin(&sh) == 0, "SHA begin");
        check(sha256_feed(&sh, fw.data(), fw.size()) == 0, "SHA feed");
        check(sha256_end(&sh, digest) == 0, "SHA end");

        std::vector<uint8_t> aux(digest, digest + IMAGE_HASH_LEN);
        std::vector<uint8_t> img = build_image(IMAGE_CHECK_SHA, aux, fw, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img), "SHA 镜像烧录");
        p.size = static_cast<uint32_t>(img.size());
        check(mini_boot_backup(&p) == ERR_OK, "SHA 完好 → ERR_OK");

        aux[0] ^= 0xFFu;
        std::vector<uint8_t> img_bad = build_image(IMAGE_CHECK_SHA, aux, fw, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img_bad), "SHA 篡改摘要烧录");
        check(mini_boot_backup(&p) == ERR_HASH_MISMATCH, "SHA 摘要错 → ERR_HASH_MISMATCH");
    }

    /* --- GCM --- */
    {
        std::vector<uint8_t> ct(fw.size());
        uint8_t tag[IMAGE_GCM_TAG_LEN];
        gcm_stream_t g;
        check(aes_gcm_encrypt_stream_begin(&g, nonce, IMAGE_GCM_NONCE_LEN, key, sizeof(key)) == 0,
              "GCM 加密 begin");
        check(aes_gcm_stream_feed(&g, fw.data(), ct.data(), ct.size()) == 0, "GCM 加密 feed");
        check(aes_gcm_encrypt_stream_finish(&g, tag, IMAGE_GCM_TAG_LEN) == 0, "GCM 加密 finish");

        std::vector<uint8_t> aux;
        aux.insert(aux.end(), nonce, nonce + IMAGE_GCM_NONCE_LEN);
        aux.insert(aux.end(), tag, tag + IMAGE_GCM_TAG_LEN);

        std::vector<uint8_t> img = build_image(IMAGE_CHECK_GCM, aux, ct, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img), "GCM 镜像烧录");
        p.size = static_cast<uint32_t>(img.size());
        check(mini_boot_backup(&p) == ERR_OK, "GCM 完好 → ERR_OK");

        aux[IMAGE_GCM_NONCE_LEN] ^= 0xFFu; /* 破坏 tag */
        std::vector<uint8_t> img_bad = build_image(IMAGE_CHECK_GCM, aux, ct, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img_bad), "GCM 篡改 tag 烧录");
        check(mini_boot_backup(&p) == ERR_AUTH_FAILED, "GCM tag 错 → ERR_AUTH_FAILED");
    }

    /* 生成一份 CBC 密文，CBC / CBC_SHA 共用 */
    std::vector<uint8_t> padded = fw;
    padded.insert(padded.end(), IMAGE_CBC_BLOCK_LEN, static_cast<uint8_t>(IMAGE_CBC_BLOCK_LEN));
    std::vector<uint8_t> ct(padded.size());
    {
        cbc_stream_t c;
        check(aes_cbc_encrypt_stream_begin(&c, iv, key, sizeof(key)) == 0, "CBC 加密 begin");
        check(aes_cbc_encrypt_stream_feed(&c, padded.data(), ct.data(), ct.size()) == 0,
              "CBC 加密 feed");
        aes_cbc_stream_free(&c);
    }

    /* --- CBC --- */
    {
        std::vector<uint8_t> aux(iv, iv + IMAGE_CBC_IV_LEN);
        std::vector<uint8_t> img = build_image(IMAGE_CHECK_CBC, aux, ct, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img), "CBC 镜像烧录");
        p.size = static_cast<uint32_t>(img.size());
        check(mini_boot_backup(&p) == ERR_OK, "CBC 完好 → ERR_OK");
    }

    /* --- CBC_SHA（mac_key 回退 key）--- */
    {
        uint8_t hmac[IMAGE_HASH_LEN];
        hmac_sha256_stream_t hs;
        check(hmac_sha256_stream_begin(&hs, key, sizeof(key)) == 0, "CBC_SHA HMAC begin");
        check(hmac_sha256_stream_feed(&hs, iv, IMAGE_CBC_IV_LEN) == 0, "CBC_SHA HMAC feed IV");
        check(hmac_sha256_stream_feed(&hs, ct.data(), ct.size()) == 0, "CBC_SHA HMAC feed 密文");
        check(hmac_sha256_stream_end(&hs, hmac) == 0, "CBC_SHA HMAC end");

        std::vector<uint8_t> aux;
        aux.insert(aux.end(), iv, iv + IMAGE_CBC_IV_LEN);
        aux.insert(aux.end(), hmac, hmac + IMAGE_HASH_LEN);

        std::vector<uint8_t> img = build_image(IMAGE_CHECK_CBC_SHA, aux, ct, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img), "CBC_SHA 镜像烧录");
        p.size = static_cast<uint32_t>(img.size());
        check(mini_boot_backup(&p) == ERR_OK, "CBC_SHA 完好 → ERR_OK");

        aux[IMAGE_CBC_IV_LEN] ^= 0xFFu; /* 破坏 hmac */
        std::vector<uint8_t> img_bad = build_image(IMAGE_CHECK_CBC_SHA, aux, ct, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_1, img_bad), "CBC_SHA 篡改 hmac 烧录");
        check(mini_boot_backup(&p) == ERR_AUTH_FAILED, "CBC_SHA hmac 错 → ERR_AUTH_FAILED");
    }
}
#endif /* IMAGE_CRYPTO_ENABLE */

static void test_backup()
{
    std::cout << "\n=== 8. 备份分区镜像校验（mini_boot_backup，复用 read）===\n";
    s_fail = 0;

    reset_flash();
    reset_state_medium();

    /* 900B payload + 开销 > 512B：覆盖多块读取与末段不满一块 */
    const std::vector<uint8_t> fw = make_firmware(0x20u, 900u);
    const std::vector<uint8_t> img =
        build_image(IMAGE_CHECK_CRC, {}, fw, "1.0.0", "bk", crc32_of(fw));
    check(area_program(FLASH_AREA_ID_IMAGE_0, img), "image_0 烧录完好镜像");

    mini_boot_backup_param_t p = {};
    p.partition = static_cast<int>(FLASH_AREA_ID_IMAGE_0);
    p.size = static_cast<uint32_t>(img.size());

    /* 1) 完好镜像（CRC 模式）→ 通过 */
    check(mini_boot_backup(&p) == ERR_OK, "CRC 完好镜像 → ERR_OK");

    /* 2) 载荷篡改 → CRC 不匹配（布局：crc(4)|ver(5)|tag(2)|payload...） */
    {
        std::vector<uint8_t> bad = img;
        bad[4u + 5u + 2u + 30u] ^= 0xFFu;
        check(area_program(FLASH_AREA_ID_IMAGE_0, bad), "烧录被篡改镜像");
        check(mini_boot_backup(&p) == ERR_CRC_MISMATCH, "CRC 被篡改 → ERR_CRC_MISMATCH");
    }
    check(area_program(FLASH_AREA_ID_IMAGE_0, img), "恢复完好镜像");

    /* 3) 入参非法 / 边界 */
    check(mini_boot_backup(nullptr) == ERR_ARG, "param=NULL → ERR_ARG");
    p.size = 0u;
    check(mini_boot_backup(&p) == ERR_ARG, "size=0 → ERR_ARG");
    p.size = (IMAGE_CRC_LEN + IMAGE_META_LEN - 1u);
    check(mini_boot_backup(&p) == ERR_ARG, "size 过小 → ERR_ARG");
    const flash_area_t *a = nullptr;
    check(flash_area_open(FLASH_AREA_ID_IMAGE_0, &a) == ERR_OK, "读取 image_0 区域大小");
    p.size = a->fa_size + 1u;
    check(mini_boot_backup(&p) == ERR_ARG, "size>区域大小 → ERR_ARG");
    p.size = static_cast<uint32_t>(img.size());

    /* 4) 未知分区 */
    mini_boot_backup_param_t p_bad = p;
    p_bad.partition = 0x7F;
    check(mini_boot_backup(&p_bad) == ERR_NOT_SUPPORTED, "未知分区 → ERR_NOT_SUPPORTED");

    /* 5) meta 非法（magic 改错）→ read 解析失败 */
    {
        std::vector<uint8_t> nomagic = img;
        nomagic[nomagic.size() - 4u] = 0x00u;
        check(area_program(FLASH_AREA_ID_IMAGE_0, nomagic), "烧录坏 meta 镜像");
        check(mini_boot_backup(&p) == ERR_ARG, "meta 非法 → ERR_ARG");
    }

#if !IMAGE_CRYPTO_ENABLE
    /* 未编加密：SHA/GCM/CBC_SHA 镜像一律不支持（CBC 在 read 层是无认证的例外） */
    {
        std::vector<uint8_t> aux(IMAGE_HASH_LEN, 0u);
        std::vector<uint8_t> img_sha = build_image(IMAGE_CHECK_SHA, aux, fw, "1.0.0", "bk", 0u);
        check(area_program(FLASH_AREA_ID_IMAGE_0, img_sha), "SHA 镜像烧录(未编加密)");
        p.size = static_cast<uint32_t>(img_sha.size());
        check(mini_boot_backup(&p) == ERR_NOT_SUPPORTED, "SHA(未编加密) → ERR_NOT_SUPPORTED");
    }
#endif

    test_result("备份分区镜像校验");

#if IMAGE_CRYPTO_ENABLE
    test_backup_crypto();
    test_result("加密模式备份校验");
#endif
}

/* ====================================================================== */
/* 菜单                                                                    */
/* ====================================================================== */
static void print_menu()
{
    std::cout <<
        "\n================ PC OTA 测试台 ================\n"
        "  1  查看 flash 区域布局\n"
        "  2  flash 读写擦除保真测试\n"
        "  3  镜像解析/校验（内存构造 + 篡改检测）\n"
        "  4  下载镜像测试（boot → app 开 OTA → 流式下载）\n"
        "  5  OTA 全流程测试（激活/确认/回滚, setjmp 模拟跳分区）\n"
        "  6  掉电与异常场景测试\n"
        "  7  擦除全片 flash（恢复出厂）\n"
        "  8  分区摘要校验（mini_boot_backup）\n"
        "  9  单步模式：" << (g_step_mode ? "开（回车逐步）" : "关（一键跑完）") << "\n"
        "  0  退出\n"
        "===============================================\n";
}

static int prompt_int(int fallback)
{
    std::string line;

    std::cout << "\n选择> " << std::flush;
    if (!std::getline(std::cin, line))
    {
        return fallback;
    }
    sanitize_line(line);
    if (line.empty())
    {
        return fallback;
    }
    try
    {
        return std::stoi(line);
    }
    catch (...)
    {
        return fallback;
    }
}

/* 执行一个菜单项；返回非 0 表示应退出程序 */
static int run_choice(int choice)
{
    switch (choice)
    {
    case 0:
        std::cout << "退出。\n";
        return 1;
    case 1:
        test_layout();
        break;
    case 2:
        test_flash_fidelity();
        break;
    case 3:
        test_image_verify();
        break;
    case 4:
        test_download();
        break;
    case 5:
        test_ota_flow();
        break;
    case 6:
        test_power_loss();
        break;
    case 7:
        test_erase_all();
        break;
    case 8:
        test_backup();
        break;
    case 9:
        g_step_mode = !g_step_mode;
        std::cout << "单步模式已" << (g_step_mode ? "开启（流程测试逐步暂停）" : "关闭（一键跑完）")
                  << "。\n";
        break;
    default:
        std::cout << "无效选择，请输入 0~9。\n";
        break;
    }
    return 0;
}

int main(int argc, char **argv)
{
    platform_init();

    std::cout << "[" << kProgrom_name << "] PC OTA 测试台已启动\n";
    std::cout << "双分区=" << (ota_is_double() ? "开" : "关")
              << "  加密=" << (IMAGE_CRYPTO_ENABLE ? "开(CRC/SHA/GCM/CBC)" : "关(仅 CRC)")
              << "  MINI_BOOT_LOAD_MAX=" << MINI_BOOT_LOAD_MAX << "\n";
    flash_layout_dump();

    /* 命令行模式：main.exe 2 3 8 依次跑完即退出，便于脚本/CI；退出码 = 是否有失败项 */
    if (argc > 1)
    {
        g_step_mode = false; /* 非交互：流程测试不要停下来等回车 */
        int failures = 0;

        for (int i = 1; i < argc; ++i)
        {
            int choice = -1;
            try
            {
                choice = std::stoi(argv[i]);
            }
            catch (...)
            {
                std::cout << "忽略非法测试项: " << argv[i] << "\n";
                continue;
            }

            s_fail = 0;
            if (run_choice(choice) != 0)
            {
                break;
            }
            failures += s_fail;
        }
        return (failures == 0) ? 0 : 1;
    }

    for (;;)
    {
        print_menu();
        if (run_choice(prompt_int(-1)) != 0)
        {
            break;
        }
    }
    return (s_fail == 0) ? 0 : 1;
}
