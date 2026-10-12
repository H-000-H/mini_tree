#! /usr/bin/env python3
"""
@brief : bin 文件查看与对比工具
功能 :
  dump : 十六进制查看 bin 原文 (--offset 支持负数, 从文件尾倒数)
  diff : 对比两个 bin, 邻近差异合并为差异块并展示两侧内容, 报告自动生成 txt
         (默认写入当前目录 diff_report.txt, 第三个参数为目录时写入该目录并额外导出差异片段)
  image_diff : 解析 main.py 打包的 OTA 镜像 (模式/变长长度由末尾 meta 自描述), 解密 payload 后对比
               (--plain_a/--plain_b 指定某侧为未打包原始 bin)
用法示例 :
  python tools/check_bin_file.py dump image_test.bin --length 64          # 查看前 64 字节
  python tools/check_bin_file.py dump image_test.bin --offset -64         # 查看末尾 64 字节
  python tools/check_bin_file.py diff image_test.bin bootloader.bin       # 全文对比, 生成 diff_report.txt
  python tools/check_bin_file.py diff a.bin b.bin ./diff_out(可改)         # 对比并导出差异片段
  python tools/check_bin_file.py image_diff a.bin b.bin --key 0011..ff             # 两个加密镜像解密对比
  python tools/check_bin_file.py image_diff image.bin app.bin --key 0011..ff --plain_b   # 镜像解密后与原始 bin 对比
退出码 : diff/image_diff 完全一致返回 0, 存在差异返回 1, 可用于脚本判断
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import sys

HEX_WIDTH = 16
DEFAULT_CONTEXT = 8
DEFAULT_MERGE_GAP = 8
# 对应 main.py 打包各模式的 aux 布局: GCM=nonce(12B)+tag(16B) CBC=iv(16B) CBC_SHA=iv(16B)+hmac(32B) SHA=sha256(32B)
_IMAGE_AUX_LEN = {"GCM": 12 + 16, "CBC": 16, "CBC_SHA": 16 + 32, "SHA": 32, "CRC": 0}
# 设备端 read.h 的 image_check_t 枚举值, 镜像末尾 meta 里存的是这套编号
_IMAGE_MODE_CODE = {"CRC": 0, "SHA": 1, "GCM": 2, "CBC": 3, "CBC_SHA": 4}
_MODE_BY_CODE = {v: k for k, v in _IMAGE_MODE_CODE.items()}
_IMAGE_META_MAGIC = 0xA5


def _parse_int(text: str) -> int:
    """命令行数值参数: 支持十进制/0x十六进制/负数(从尾部倒数)"""
    try:
        return int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"无法解析数值: {text} (示例: 64 或 0x40)")


def _read_file(path: str) -> bytes:
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        raise SystemExit(f"[错误] 无法读取 {path}: {e}")


def _md5(data: bytes) -> str:
    return hashlib.md5(data).hexdigest()


def _format_hexdump(data: bytes, base: int = 0) -> str:
    lines: list[str] = []
    for i in range(0, len(data), HEX_WIDTH):
        row = data[i:i + HEX_WIDTH]
        hex_part = " ".join(f"{b:02x}" for b in row).ljust(HEX_WIDTH * 3 - 1)
        ascii_part = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
        lines.append(f"  {base + i:08x}  {hex_part}  |{ascii_part}|")
    return "\n".join(lines)


def _diff_chunks(data_a: bytes, data_b: bytes, merge_gap: int) -> list[tuple[int, int]]:
    chunks: list[list[int]] = []
    for i in range(min(len(data_a), len(data_b))):
        if data_a[i] == data_b[i]:
            continue
        if chunks and i - chunks[-1][1] <= merge_gap:
            chunks[-1][1] = i + 1
        else:
            chunks.append([i, i + 1])
    if len(data_a) != len(data_b):
        start, end = min(len(data_a), len(data_b)), max(len(data_a), len(data_b))
        if chunks and start - chunks[-1][1] <= merge_gap:
            chunks[-1][1] = end
        else:
            chunks.append([start, end])
    return [(s, e) for s, e in chunks]


def _cmd_dump(args: argparse.Namespace) -> None:
    data = _read_file(args.file)
    if args.offset < 0:
        start = len(data) + args.offset
        if start < 0:
            raise SystemExit(f"[错误] --offset {args.offset} 超出文件大小 {len(data)} 字节")
    elif args.offset > len(data):
        raise SystemExit(f"[错误] --offset 0x{args.offset:x} 超出文件大小 {len(data)} 字节")
    else:
        start = args.offset
    end = len(data) if args.length is None else min(start + args.length, len(data))
    print(f"文件: {args.file}")
    print(f"大小: {len(data)} 字节  md5: {_md5(data)}")
    print(f"区间: 0x{start:08x} - 0x{end:08x} ({end - start} 字节)")
    dump_text = _format_hexdump(data[start:end], base=start)
    print(dump_text if dump_text else "  (区间为空)")


def _chunk_section(index: int, start: int, end: int, path_a: str, data_a: bytes,
                   path_b: str, data_b: bytes, context: int) -> list[str]:
    """单个差异块的展示段落: 两侧各带上下文的 hexdump"""
    lines = [f"差异块 #{index}: 偏移 0x{start:08x} - 0x{end:08x} ({end - start} 字节)"]
    for label, data in ((f"a[{os.path.basename(path_a)}]", data_a),
                        (f"b[{os.path.basename(path_b)}]", data_b)):
        lo = max(0, start - context)
        hi = min(end + context, len(data))
        if lo >= hi:
            lines.append(f"  {label}: (无此区间数据)")
            continue
        lines.append(f"  {label}:")
        lines.append(_format_hexdump(data[lo:hi], base=lo))
    return lines


def _write_text_atomic(path: str, text: str) -> None:
    """原子写入文本: 先写临时文件再替换, 防止中途失败留下残缺文件"""
    tmp_path = path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        f.write(text)
    os.replace(tmp_path, path)


def _write_bytes_atomic(path: str, data: bytes) -> None:
    """原子写入二进制: 先写临时文件再替换, 防止中途失败留下残缺文件"""
    tmp_path = path + ".tmp"
    with open(tmp_path, "wb") as f:
        f.write(data)
    os.replace(tmp_path, path)


def _extract_chunks(path_a: str, path_b: str, data_a: bytes, data_b: bytes,
                    chunks: list[tuple[int, int]], out_dir: str) -> None:
    """把每个差异块按文件分别导出为独立片段"""
    stem_a = os.path.splitext(os.path.basename(path_a))[0]
    stem_b = os.path.splitext(os.path.basename(path_b))[0]
    for i, (start, end) in enumerate(chunks, 1):
        suffix = f"chunk{i:02d}_0x{start:08x}_{end - start}B.bin"
        for stem, data in ((stem_a, data_a), (stem_b, data_b)):
            piece = data[start:end]
            if not piece:
                continue
            out_path = os.path.join(out_dir, f"{stem}_{suffix}")
            _write_bytes_atomic(out_path, piece)
            print(f"已导出: {out_path} ({len(piece)} 字节)")


def _cmd_diff(args: argparse.Namespace) -> bool:
    """执行对比, 返回是否存在差异"""
    data_a = _read_file(args.file_a)
    data_b = _read_file(args.file_b)
    if args.context < 0 or args.gap < 0:
        raise SystemExit("[错误] --context/--gap 不能为负数")
    lines = [
        f"a: {args.file_a}  大小: {len(data_a)} 字节  md5: {_md5(data_a)}",
        f"b: {args.file_b}  大小: {len(data_b)} 字节  md5: {_md5(data_b)}",
    ]
    return _finish_diff(lines, data_a, data_b, args)


def _finish_diff(lines: list[str], data_a: bytes, data_b: bytes, args: argparse.Namespace) -> bool:
    """对比两侧数据, 打印报告并写 txt/导出片段, 返回是否存在差异"""
    chunks = _diff_chunks(data_a, data_b, args.gap)
    if not chunks:
        lines.append("结果: 两文件完全一致")
    else:
        lines.append(f"结果: 发现 {len(chunks)} 处差异")
        for i, (start, end) in enumerate(chunks, 1):
            lines.append("")
            lines += _chunk_section(i, start, end, args.file_a, data_a, args.file_b, data_b, args.context)

        if len(data_a) != len(data_b) and chunks[-1] == (min(len(data_a), len(data_b)), max(len(data_a), len(data_b))):
            bigger = "a" if len(data_a) > len(data_b) else "b"
            lines.append("")
            lines.append(f"说明: 共有内容部分完全一致, {bigger} 侧在文件末尾多出 {abs(len(data_a) - len(data_b))} 字节")

    report = "\n".join(lines)
    print(report)
    if args.extract_dir:
        os.makedirs(args.extract_dir, exist_ok=True)
        if chunks:
            _extract_chunks(args.file_a, args.file_b, data_a, data_b, chunks, args.extract_dir)
        report_path = os.path.join(args.extract_dir, "diff_report.txt")
    else:
        report_path = "diff_report.txt"
    _write_text_atomic(report_path, report + "\n")
    print(f"对比报告已写入: {report_path}")
    return bool(chunks)


def _parse_image(data: bytes, path: str) -> dict:
    """按 main.py 打包布局拆出 crc/version/tag/aux/payload (模式与变长长度来自末尾 4B meta)"""
    if len(data) < 8:
        raise SystemExit(f"[错误] {path} 大小 {len(data)} 字节, 小于最小镜像 8 字节")
    m = data[-4:]
    if m[0] != _IMAGE_META_MAGIC:
        raise SystemExit(f"[错误] {path} 末尾 meta 魔数 0x{m[0]:02x} != 0xA5, "
                         f"不是 main.py 打包的镜像 (核对打包工具版本)")
    mode_code = m[1] & 0x7F
    if mode_code not in _MODE_BY_CODE:
        raise SystemExit(f"[错误] {path} meta 模式编号 {mode_code} 非法")
    mode = _MODE_BY_CODE[mode_code]
    version_len, tag_len = m[2], m[3]
    aux_len = _IMAGE_AUX_LEN[mode]
    overhead = 4 + version_len + tag_len + aux_len
    body = data[:-4]  # 末尾 meta 之外的镜像主体
    if len(body) < overhead:
        raise SystemExit(
            f"[错误] {path} 大小 {len(data)} 字节, 小于 {mode} 布局最小 {overhead + 4} 字节")
    if m[1] & 0x80:  # is_front: 元数据在头部
        return {
            "mode": mode, "is_front": True,
            "crc": int.from_bytes(body[:4], "little"),
            "version": body[4:4 + version_len],
            "tag": body[4 + version_len:4 + version_len + tag_len],
            "aux": body[4 + version_len + tag_len:overhead],
            "payload": body[overhead:],
        }
    return {
        "mode": mode, "is_front": False,
        "crc": int.from_bytes(body[-4:], "little"),
        "version": body[-4 - tag_len - version_len:-4 - tag_len],
        "tag": body[-4 - tag_len:-4],
        "aux": body[-overhead:-4 - tag_len - version_len],
        "payload": body[:-overhead],
    }


def _decrypt_payload(mode: str, payload: bytes, aux: bytes, key: bytes) -> bytes:
    """按打包模式解密 payload (SHA/CRC 模式 payload 即明文)"""
    import m_dsig.image_aes as aes  # 按需加载: 仅 image_diff 需要 cryptography
    if mode == "GCM":
        try:
            return aes.ase_gcm_decrypt(payload, key, aux[:12], aux[12:])
        except aes.InvalidTag:
            raise SystemExit("[错误] GCM tag 校验失败: 密钥不正确或镜像内容被篡改")
    if mode in ("CBC", "CBC_SHA"):
        return aes.aes_cbc_decrypt(payload, key, aux[:16])
    return payload


def _cmd_image_diff(args: argparse.Namespace) -> bool:
    """解析镜像头并解密 payload 后对比, 返回是否存在差异"""
    import m_crc.image_crc as crc_mod  # 按需加载
    import m_dsig.image_sha as sha

    key = b""      # 首次遇到加密模式时才解析 --key (模式由各侧镜像 meta 自描述)
    mac_key = b""  # CBC_SHA 的 MAC 密钥, 缺省回退用 key

    lines: list[str] = []
    plains: list[bytes] = []
    dec_images: list[tuple[str, bytes]] = []  # 镜像侧的解密 payload, 供 extract_dir 导出
    for label, path in (("a", args.file_a), ("b", args.file_b)):
        data = _read_file(path)
        if getattr(args, f"plain_{label}"):
            plains.append(data)
            lines += [f"{label}: {path}",
                      f"  原始 bin (未解析镜像头): {len(data)} 字节  md5: {_md5(data)}"]
            continue
        info = _parse_image(data, path)
        mode = info["mode"]
        if mode in ("GCM", "CBC", "CBC_SHA"):
            if not key:
                if not args.key:
                    raise SystemExit(f"[错误] {mode} 模式需要 --key (hex)")
                key = bytes.fromhex(args.key)
                if len(key) not in (16, 24, 32):
                    raise SystemExit("[错误] --key 必须是 16/24/32 字节的 hex")
        # main.py 打包时 crc 对 payload(密文/明文)计算, 校验也按同一口径
        crc_calc = crc_mod.crc_generic(info["payload"], args.crc_init, args.crc_refin,
                                       args.crc_refout, args.crc_xor_out, args.crc_poly)
        crc_note = "一致" if crc_calc == info["crc"] else f"不一致! 计算 0x{crc_calc:08x}"
        lines += [
            f"{label}: {path}",
            f"  镜像[{mode} 头部{'在前' if info['is_front'] else '在后'}] "
            f"version={info['version'].decode('utf-8', 'replace')!r} "
            f"tag={info['tag'].decode('utf-8', 'replace')!r} aux={len(info['aux'])}B",
            f"  crc: 存储 0x{info['crc']:08x} {crc_note}",
        ]
        if mode == "SHA":
            ok = sha.sha256(info["payload"]) == info["aux"]
            lines.append(f"  sha256 校验: {'一致' if ok else '不一致!'}")
        elif mode == "CBC_SHA":
            if not mac_key:
                if args.mac_key:
                    mac_key = bytes.fromhex(args.mac_key)
                    if len(mac_key) not in (16, 24, 32):
                        raise SystemExit("[错误] --mac_key 必须是 16/24/32 字节的 hex")
                else:
                    mac_key = key
            # hmac 覆盖 iv||ciphertext, 与打包端/设备端口径一致
            ok = hmac.new(mac_key, info["aux"][:16] + info["payload"],
                          hashlib.sha256).digest() == info["aux"][16:]
            lines.append(f"  hmac 校验: {'一致' if ok else '不一致! (镜像可能被篡改, 仍继续解密)'}")
        plain = _decrypt_payload(mode, info["payload"], info["aux"], key)
        plains.append(plain)
        dec_images.append((path, plain))
        lines.append(f"  解密 payload: {len(plain)} 字节  md5: {_md5(plain)}")

    if args.extract_dir:
        os.makedirs(args.extract_dir, exist_ok=True)
        for path, plain in dec_images:
            stem = os.path.splitext(os.path.basename(path))[0]
            out_path = os.path.join(args.extract_dir, f"{stem}_dec.bin")
            _write_bytes_atomic(out_path, plain)
            print(f"已导出解密 payload: {out_path} ({len(plain)} 字节)")

    lines.append("")
    return _finish_diff(lines, plains[0], plains[1], args)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="bin 文件查看与对比工具")
    sub = parser.add_subparsers(dest="command", required=True)

    p_dump = sub.add_parser("dump", help="十六进制查看 bin 原文")
    p_dump.add_argument("file", help="bin 文件路径")
    p_dump.add_argument("--offset", type=_parse_int, default=0, help="起始偏移, 负数表示从文件尾倒数 (默认 0)")
    p_dump.add_argument("--length", type=_parse_int, default=None, help="查看长度, 缺省到文件尾")

    p_diff = sub.add_parser("diff", help="对比两个 bin 文件")
    p_diff.add_argument("file_a", help="文件 a")
    p_diff.add_argument("file_b", help="文件 b")
    p_diff.add_argument("--context", type=_parse_int, default=DEFAULT_CONTEXT, help="差异块两侧展示的上下文字节数 (默认 8)")
    p_diff.add_argument("--gap", type=_parse_int, default=DEFAULT_MERGE_GAP, help="相距不超过该字节数的差异合并为一块 (默认 8)")
    p_diff.add_argument("extract_dir", nargs="?", default=None, help="可选: 导出差异片段到该目录")

    p_img = sub.add_parser("image_diff", help="解析 main.py 打包的 OTA 镜像并解密 payload 后对比 (模式由镜像 meta 自描述)")
    p_img.add_argument("file_a", help="文件 a (OTA 镜像, 或 --plain_a 指定时的原始 bin)")
    p_img.add_argument("file_b", help="文件 b (OTA 镜像, 或 --plain_b 指定时的原始 bin)")
    p_img.add_argument("--key", default="", help="hex key 16/24/32 字节, GCM/CBC/CBC_SHA 必填")
    p_img.add_argument("--mac_key", default="", help="hex MAC key (CBC_SHA); 缺省复用 --key")
    p_img.add_argument("--plain_a", action="store_true", help="a 侧为未打包的原始 bin")
    p_img.add_argument("--plain_b", action="store_true", help="b 侧为未打包的原始 bin")
    p_img.add_argument("--crc_init", type=_parse_int, default=0xffffffff, help="CRC寄存器初始值 (与打包时一致)")
    p_img.add_argument("--crc_refin", action=argparse.BooleanOptionalAction, default=True, help="输入字节按位反转 (默认开启)")
    p_img.add_argument("--crc_refout", action=argparse.BooleanOptionalAction, default=True, help="输出按位反转 (默认开启)")
    p_img.add_argument("--crc_xor_out", type=_parse_int, default=0xffffffff, help="最终异或值 (与打包时一致)")
    p_img.add_argument("--crc_poly", type=_parse_int, default=0x04c11db7, help="CRC生成多项式 (与打包时一致)")
    p_img.add_argument("--context", type=_parse_int, default=DEFAULT_CONTEXT, help="差异块两侧展示的上下文字节数 (默认 8)")
    p_img.add_argument("--gap", type=_parse_int, default=DEFAULT_MERGE_GAP, help="相距不超过该字节数的差异合并为一块 (默认 8)")
    p_img.add_argument("extract_dir", nargs="?", default=None,
                       help="可选: 导出解密 payload 与解密后差异片段到该目录")
    return parser


def main() -> None:
    args = _build_parser().parse_args()
    if args.command == "dump":
        _cmd_dump(args)
        return
    diff_func = _cmd_image_diff if args.command == "image_diff" else _cmd_diff
    sys.exit(1 if diff_func(args) else 0)


if __name__ == "__main__":
    main()
