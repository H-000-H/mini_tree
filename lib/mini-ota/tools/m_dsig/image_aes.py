import os

from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def aes_cbc_encrypt(data: bytes, key: bytes):
    """
    bin版 : 返回原始字节, 用于写入固件镜像bin
    :param data: encrypted context
    :param key: encrypted key
    :key 16 bytes-> AES128
    :key 24 bytes-> AES192
    :key 32 bytes-> AES256
    :returns ciphertext , iv (原始字节)
    """
    # cbc 要求contex必须16字节的倍数 不够会填充
    iv = os.urandom(16)
    padder = padding.PKCS7(128).padder()
    padder_context = padder.update(data) + padder.finalize()

    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    encryptor = cipher.encryptor()
    ciphertext = encryptor.update(padder_context) + encryptor.finalize()
    return ciphertext, iv


def aes_cbc_encrypt_hex(data: bytes, key: bytes):
    """
    hex版 : 返回十六进制字符串, 用于打印/json/c头文件
    :returns ciphertext , iv (hex字符串)
    """
    ciphertext, iv = aes_cbc_encrypt(data, key)
    return ciphertext.hex(), iv.hex()


def aes_cbc_decrypt(ciphertext: bytes, key: bytes, iv: bytes) -> bytes:
    """
    bin版 : 传入原始字节
    :param ciphertext: encrypted context
    :param key: decrypted key
    :param iv: 原始字节
    :return: orange context
    """
    cipher = Cipher(algorithms.AES(key), modes.CBC(iv))
    decryptor = cipher.decryptor()
    # 上文可能填充了多余字节现在去填充
    padded_data = decryptor.update(ciphertext) + decryptor.finalize()
    unpadder = padding.PKCS7(128).unpadder()
    return unpadder.update(padded_data) + unpadder.finalize()


def aes_cbc_decrypt_hex(ciphertext: str, key: bytes, iv: str) -> bytes:
    """
    hex版 : 传入十六进制字符串
    :return: orange context
    """
    return aes_cbc_decrypt(bytes.fromhex(ciphertext), key, bytes.fromhex(iv))


def ase_gcm_encrypt(plain_data: bytes, key: bytes):
    """
    bin版 : 返回原始字节, 用于写入固件镜像bin
    :param plain_data: encrypted context
    :param key: encrypted key
    :key 16 bytes-> AES128
    :key 24 bytes-> AES192
    :key 32 bytes-> AES256
    :returns ciphertext , nonce , tag (原始字节)
    :note : gcm couldn't use key and nonce together this will crack
    """
    nonce = os.urandom(12)
    cipher = Cipher(algorithms.AES(key), modes.GCM(nonce))
    encryptor = cipher.encryptor()
    ciphertext = encryptor.update(plain_data) + encryptor.finalize()
    return ciphertext, nonce, encryptor.tag


def ase_gcm_encrypt_hex(plain_data: bytes, key: bytes):
    """
    hex版 : 返回十六进制字符串, 用于打印/json/c头文件
    :returns ciphertext , nonce , tag (hex字符串)
    """
    ciphertext, nonce, tag = ase_gcm_encrypt(plain_data, key)
    return ciphertext.hex(), nonce.hex(), tag.hex()


def ase_gcm_decrypt(ciphertext: bytes, key: bytes, nonce: bytes, tag: bytes):
    """
    bin版 : 传入原始字节
    :param ciphertext: encrypted context
    :param key: encrypted key
    :param nonce: 原始字节
    :param tag: 原始字节
    :return: orange context
    """
    cipher = Cipher(algorithms.AES(key), modes.GCM(nonce, tag))
    decryptor = cipher.decryptor()
    return decryptor.update(ciphertext) + decryptor.finalize()


def ase_gcm_decrypt_hex(ciphertext: str, key: bytes, nonce: str, tag: str):
    """
    hex版 : 传入十六进制字符串
    :return: orange context
    """
    return ase_gcm_decrypt(bytes.fromhex(ciphertext), key, bytes.fromhex(nonce), bytes.fromhex(tag))


def test():
    key = b'0123456789abcdef'  # 16 bytes -> AES128
    print("----------------------------------------------------------------------------------")
    aes_cbc_contest, aes_cbc_iv = aes_cbc_encrypt(b'test', key)
    aes_cbc_decrypt_context = aes_cbc_decrypt(aes_cbc_contest, key, aes_cbc_iv)
    print("AES CBC加密测试(bin版):", " | ", "加密内容=", aes_cbc_contest.hex(), " | ", "初始化向量=", aes_cbc_iv.hex(),
          " | ")
    print("ASE cbc解密测试(bin版)", aes_cbc_decrypt_context)
    print("----------------------------------------------------------------------------------")
    aes_cbc_contest, aes_cbc_iv = aes_cbc_encrypt_hex(b'test', key)
    aes_cbc_decrypt_context = aes_cbc_decrypt_hex(aes_cbc_contest, key, aes_cbc_iv)
    print("AES CBC加密测试(hex版):", " | ", "加密内容=", aes_cbc_contest, " | ", "初始化向量=", aes_cbc_iv, " | ")
    print("ASE cbc解密测试(hex版)", aes_cbc_decrypt_context)
    print("----------------------------------------------------------------------------------")
    ase_gcm_contest, ase_gcm_nonce, ase_gcm_tag = ase_gcm_encrypt(b'test', key)
    ase_gcm_decrypt_context = ase_gcm_decrypt(ase_gcm_contest, key, ase_gcm_nonce, ase_gcm_tag)
    print("AES GCM加密测试(bin版):", " | ", "加密内容=", ase_gcm_contest.hex(), " | ", "初始化向量=",
          ase_gcm_nonce.hex(), " | ", "标签=", ase_gcm_tag.hex(), " | ")
    print("ASE gcm解密测试(bin版)", ase_gcm_decrypt_context)
    print("----------------------------------------------------------------------------------")
    ase_gcm_contest, ase_gcm_nonce, ase_gcm_tag = ase_gcm_encrypt_hex(b'test', key)
    ase_gcm_decrypt_context = ase_gcm_decrypt_hex(ase_gcm_contest, key, ase_gcm_nonce, ase_gcm_tag)
    print("AES GCM加密测试(hex版):", " | ", "加密内容=", ase_gcm_contest, " | ", "初始化向量=", ase_gcm_nonce, " | ",
          "标签=", ase_gcm_tag, " | ")
    print("ASE gcm解密测试(hex版)", ase_gcm_decrypt_context)
    print("----------------------------------------------------------------------------------")
    # 失败逻辑测试: 以下异常都应被拦截, 未拦截说明校验失效
    gcm_ct, gcm_nonce, gcm_tag = ase_gcm_encrypt(b'test', key)
    try:
        ase_gcm_decrypt(gcm_ct, b'wrong-key-16byte', gcm_nonce, gcm_tag)
        print("[异常] GCM 错误密钥未被拦截")
    except InvalidTag:
        print("失败逻辑 GCM 错误密钥 -> InvalidTag 拦截 OK")
    tampered = bytearray(gcm_ct)
    tampered[0] ^= 0xff
    try:
        ase_gcm_decrypt(bytes(tampered), key, gcm_nonce, gcm_tag)
        print("[异常] GCM 篡改密文未被拦截")
    except InvalidTag:
        print("失败逻辑 GCM 篡改密文 -> InvalidTag 拦截 OK")
    try:
        ase_gcm_encrypt(b'test', b'key')
        print("[异常] 非法key长度未被拦截")
    except ValueError:
        print("失败逻辑 非法key长度 -> ValueError 拦截 OK")
    cbc_ct, cbc_iv = aes_cbc_encrypt(b'test', key)
    try:
        aes_cbc_decrypt(cbc_ct, key, b'short-iv')
        print("[异常] CBC 非法iv长度未被拦截")
    except ValueError:
        print("失败逻辑 CBC 非法iv长度 -> ValueError 拦截 OK")
    print("----------------------------------------------------------------------------------")


if __name__ == '__main__':
    test()
