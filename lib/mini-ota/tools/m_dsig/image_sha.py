from cryptography.hazmat.primitives import hashes


def sha256(data: bytes) -> bytes:
    """
    :param data: origin data
    :return: sha256 hash of data
    """
    digest = hashes.Hash(hashes.SHA256())
    digest.update(data)
    return digest.finalize()


def sha512(data: bytes) -> bytes:
    """
    :param data: origin data
    :return: sha512 hash of data
    """
    digest = hashes.Hash(hashes.SHA512())
    digest.update(data)
    return digest.finalize()


if __name__ == '__main__':
    assert sha256(b'test').hex() == '9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08'
    print("sha256 succeeded")
