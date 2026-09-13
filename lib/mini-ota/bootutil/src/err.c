/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @file: err.c
 * @brief: 统一错误码转字符串实现（见 err.h）
 */
#include "err.h"

const char *err_str(int err)
{
    switch (err)
    {
    case ERR_OK:                return "ok";
    case ERR_ARG:               return "invalid argument";
    case ERR_TOO_SMALL:         return "data smaller than required";
    case ERR_OVERFLOW:          return "length overflow";
    case ERR_NOT_SUPPORTED:     return "feature not enabled";
    case ERR_BUF_TOO_SMALL:     return "output buffer too small";
    case ERR_CRC_MISMATCH:      return "crc mismatch";
    case ERR_HASH_MISMATCH:     return "sha256 mismatch";
    case ERR_AUTH_FAILED:       return "authentication failed";
    case ERR_DECRYPT_FAILED:    return "decrypt failed";
    case ERR_HASH_FAILED:       return "hash failed";
    case ERR_PADDING:           return "invalid pkcs7 padding";
    case ERR_OTA_STATE:         return "ota state missing or corrupt";
    case ERR_OTA_OPEN:          return "ota not enabled";
    case ERR_TRANSMIT:          return "transmit error";
    default:                    return "unknown error";
    }
}
