#! /usr/bin/env python3
"""
@brief : 固件 bin 打包为 OTA 镜像 (可选 CRC/SHA/AES-GCM/CBC 校验加密)
用法示例 :
  python tools/main.py bootloader.bin image_test.bin --version 1.2.0 --tag h-000-h
  python tools/main.py app.bin out.bin --check SHA                       # sha256 校验, aux=32B
  python tools/main.py app.bin out.bin --check GCM --key 00112233445566778899aabbccddeeff
  python tools/main.py app.bin out.bin --check CBC_SHA --key 00112233445566778899aabbccddeeff --mac_key ffeeddccbbaa99887766554433221100
镜像布局(is_front=true):  crc(4B) | version | tag | aux | payload | meta(4B)
镜像布局(is_front=false): payload | aux | version | tag | crc(4B) | meta(4B)
meta(4B, 恒在末尾): magic(0xA5) | mode|0x80(is_front) | version_len | tag_len, 镜像自描述
payload: 原始bin 或 加密后的密文(加密时crc对密文计算)
aux: GCM=nonce(12B)+tag(16B)  CBC=iv(16B)  CBC_SHA=iv(16B)+hmac(32B)  SHA=sha256(32B)  CRC=空
CBC_SHA 的 hmac 覆盖 iv||ciphertext(设备端 read.c 的 CBC_SHA 分支用同样覆盖范围验 MAC);
  可用 --mac_key 指定独立 MAC 密钥(不给则复用 --key)
version/tag 为变长字符串, 长度记录在 meta 中(各不超过 255 字节)
CRC模型参数可用命令行调整(--crc_init/--crc_refin/--crc_refout/--crc_xor_out/--crc_poly),
默认值: init=0xffffffff refin=refout=true xor_out=0xffffffff poly=0x04c11db7 (即标准CRC-32)
"""
from enum import Enum
import argparse
import hashlib
import hmac
import os
import sys
import m_crc.image_crc as crc
import m_dsig.image_aes as aes
import m_dsig.image_sha as sha

def parse_int_arg(text: str) -> int:
    """命令行数值参数: 支持十进制与0x十六进制"""
    try:
        return int(text, 0)
    except ValueError:
        raise argparse.ArgumentTypeError(f"无法解析数值: {text} (示例: 255 或 0x04C11DB7)")

def make_hmac_tag(key, ciphertext, digestmod):
    return hmac.new(key, ciphertext, digestmod).digest()

class check_cripted(Enum):
    GCM = 1
    CBC = 2
    CBC_SHA = 3
    SHA = 4
    CRC = 5

# 设备端 read.h 的 image_check_t 枚举值, meta 里存的是这套编号(与上面打包枚举数值无关)
_MODE_CODE = {"CRC": 0, "SHA": 1, "GCM": 2, "CBC": 3, "CBC_SHA": 4}
_META_MAGIC = 0xA5

def make_image(
    bin_file:str,
    image_path:str,
    version : str ,
    tag : str ,
    is_front : bool,
    cripted : check_cripted,
    key : bytes = b"",
    mac_key : bytes = b"",
    crc_init: int = 0xffffffff,
    crc_refin: bool = True,
    crc_refout: bool = True,
    crc_xor_out: int = 0xffffffff,
    crc_poly: int = 0x04c11db7):
    """
    :param bin_file: bin 源码路径
    :param image_path: image加tag或者加密后的路径
    :param version: 版本
    :param tag: 标签
    :param is_front: 总体在前还是在后
    :param cripted: 加密和各种校验等级
    :param key: 加密密钥(可选)
    :param mac_key: HMAC 密钥(仅 CBC_SHA 使用), 空则复用 key
    :param crc_init: CRC寄存器初始值
    :param crc_refin: 输入字节是否按位反转
    :param crc_refout: 输出是否按位反转
    :param crc_xor_out: 最终异或值
    :param crc_poly: CRC生成多项式
    """
    with open(bin_file, "rb") as f:
        fw = f.read()

    # 未单独指定 MAC 密钥时回退用加密密钥(生产环境应传 --mac_key 保证两者独立)
    mac_key = mac_key or key

    version_bytes = version.encode()
    tag_bytes = tag.encode()
    if len(version_bytes) > 0xFF or len(tag_bytes) > 0xFF:
        raise ValueError(f"version/tag 长度须各不超过 255 字节 "
                         f"(现 version={len(version_bytes)}, tag={len(tag_bytes)})")

    if cripted == check_cripted.GCM:
        payload , nonce , gcm_tag = aes.ase_gcm_encrypt(fw, key)
        aux = nonce + gcm_tag
    elif cripted == check_cripted.CBC:
        payload , iv = aes.aes_cbc_encrypt(fw, key)
        aux = iv
    elif cripted == check_cripted.CBC_SHA:
        payload , iv = aes.aes_cbc_encrypt(fw, key)
        # 覆盖范围: iv||ciphertext, 必须与设备端 read.c 的 CBC_SHA 分支一致
        aux = iv + make_hmac_tag(mac_key, iv + payload, hashlib.sha256)
    elif cripted == check_cripted.SHA:
        payload = fw
        aux = sha.sha256(fw)
    else:
        payload = fw
        aux = b""

    crc_value = crc.crc_generic(payload, crc_init, crc_refin, crc_refout, crc_xor_out, crc_poly)

    image = bytearray()
    if is_front:
        image += crc_value.to_bytes(4, "little")
        image += version_bytes
        image += tag_bytes
        image += aux
        image += payload
    else:
        image += payload
        image += aux
        image += version_bytes
        image += tag_bytes
        image += crc_value.to_bytes(4, "little")

    # 末尾自描述 meta: 设备端 read.c 从最后 4B 读出模式与变长字段长度
    image += bytes((_META_MAGIC,
                    _MODE_CODE[cripted.name] | (0x80 if is_front else 0),
                    len(version_bytes), len(tag_bytes)))

    #原子写入: 先写临时文件再替换, 防止中途失败留下残缺镜像
    tmp_path = image_path + ".tmp"
    with open(tmp_path, "wb") as image_file:
        image_file.write(image)
    os.replace(tmp_path, image_path)

def main():
    parser = argparse.ArgumentParser(description="MCU firmware image tool")
    parser.add_argument("bin_file", help="input firmware .bin file")
    parser.add_argument("image_path", help="output image file")
    parser.add_argument("--version", default="1.0.0", help="image version")
    parser.add_argument("--tag", default="", help="image tag")
    parser.add_argument("--is_front", action="store_true", help="check logical in front of bin context (default: end of bin context)")
    parser.add_argument("--check", default="CRC", choices=[c.name for c in check_cripted], help="image check/encrypt mode")
    parser.add_argument("--key", default="", help="hex key 16/24/32 bytes , required for GCM/CBC/CBC_SHA")
    parser.add_argument("--mac_key", default="", help="hex MAC key for CBC_SHA ; 缺省复用 --key (生产环境建议独立)")
    parser.add_argument("--crc_init", type=parse_int_arg, default=0xffffffff, help="CRC寄存器初始值 (十进制或0x十六进制)")
    parser.add_argument("--crc_refin", action=argparse.BooleanOptionalAction, default=True, help="输入字节按位反转 (默认开启, --no-crc_refin 关闭)")
    parser.add_argument("--crc_refout", action=argparse.BooleanOptionalAction, default=True, help="输出按位反转 (默认开启, --no-crc_refout 关闭)")
    parser.add_argument("--crc_xor_out", type=parse_int_arg, default=0xffffffff, help="最终异或值 (十进制或0x十六进制)")
    parser.add_argument("--crc_poly", type=parse_int_arg, default=0x04c11db7, help="CRC生成多项式 (十进制或0x十六进制)")
    args = parser.parse_args()

    tmp_path = args.image_path + ".tmp"
    try:
        key = b""
        mac_key = b""
        if args.check in ("GCM", "CBC", "CBC_SHA"):
            if not args.key:
                parser.error(f"--key is required for {args.check}")
            key = bytes.fromhex(args.key)
            if len(key) not in (16, 24, 32):
                parser.error("key must be hex of 16/24/32 bytes")
            if args.mac_key:
                mac_key = bytes.fromhex(args.mac_key)
                if len(mac_key) not in (16, 24, 32):
                    parser.error("mac_key must be hex of 16/24/32 bytes")
        make_image(args.bin_file, args.image_path, args.version, args.tag, args.is_front, check_cripted[args.check], key, mac_key,
                   crc_init=args.crc_init, crc_refin=args.crc_refin, crc_refout=args.crc_refout,
                   crc_xor_out=args.crc_xor_out, crc_poly=args.crc_poly)
    except FileNotFoundError:
        print(f"[错误] 输入文件不存在: {args.bin_file}")
        sys.exit(1)
    except ValueError as e:
        print(f"[错误] 密钥或参数不合法: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"[错误] 生成镜像失败: {type(e).__name__}: {e}")
        sys.exit(1)
    finally:
        #任一步失败时清理临时文件, 成功时已被 replace 消费
        if os.path.exists(tmp_path):
            os.remove(tmp_path)

if __name__ == "__main__":
    main()
