/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @copyright SPDX-License-Identifier: Apache-2.0
 * @file status.c
 * @brief MINI_ERR_TO_STR 错误码字符串表
 * @author H-000-H
 * @details
 *   独立编译单元: 只提供诊断用字符串表, 无任何副作用与动态分配。
 *   位于 core 静态库中, 未被引用时由链接器整体丢弃 —— 不用字符串表的构建零开销。
 */
#include "status.h"
#include "log_err.h" /* 仅用于片对齐自检 */

/* mini-log 是随仓 vendor, 其错误码数值写死 (不含 status.h)。在此断言它与本头
 * log 片基准一致 —— 任一侧改数值而忘了同步另一侧, 这里会编译失败。 */
_Static_assert(MINI_LOG_ERR_PARAM == MINI_ERR_SUBSYS(MINI_ERR_SUBSYS_LOG_BASE, 0),
               "MINI_LOG_ERR_PARAM drifted from the log slot base");
_Static_assert(MINI_LOG_ERR_FLASH_READ == MINI_ERR_SUBSYS(MINI_ERR_SUBSYS_LOG_BASE, 6),
               "MINI_LOG_ERR_FLASH_READ drifted from the log slot base");

const char* MINI_ERR_TO_STR(int err)
{
    switch (err)
    {
        case MINI_OK: return "MINI_OK";

        case MINI_ERR_INVAL: return "MINI_ERR_INVAL";
        case MINI_ERR_ISR: return "MINI_ERR_ISR";
        case MINI_ERR_NOMEM: return "MINI_ERR_NOMEM";
        case MINI_ERR_IO: return "MINI_ERR_IO";
        case MINI_ERR_BUSY: return "MINI_ERR_BUSY";
        case MINI_ERR_AGAIN: return "MINI_ERR_AGAIN";
        case MINI_ERR_NOSPC: return "MINI_ERR_NOSPC";
        case MINI_ERR_TIMEOUT: return "MINI_ERR_TIMEOUT";
        case MINI_ERR_HW_FATAL: return "MINI_ERR_HW_FATAL";
        case MINI_ERR_DEFER: return "MINI_ERR_DEFER";
        case MINI_ERR_NODEV: return "MINI_ERR_NODEV";
        case MINI_ERR_NOTSUPP: return "MINI_ERR_NOTSUPP";
        case MINI_ERR_STATE: return "MINI_ERR_STATE";
        case MINI_ERR_RANGE: return "MINI_ERR_RANGE";
        case MINI_ERR_NOENT: return "MINI_ERR_NOENT";
        case MINI_ERR_EXIST: return "MINI_ERR_EXIST";
        case MINI_ERR_NOTINIT: return "MINI_ERR_NOTINIT";
        case MINI_ERR_NODATA: return "MINI_ERR_NODATA";
        case MINI_ERR_CORRUPT: return "MINI_ERR_CORRUPT";
        case MINI_ERR_CRC: return "MINI_ERR_CRC";
        case MINI_ERR_PARITY: return "MINI_ERR_PARITY";
        case MINI_ERR_OVERFLOW: return "MINI_ERR_OVERFLOW";
        case MINI_ERR_NOTREADY: return "MINI_ERR_NOTREADY";
        case MINI_ERR_CANCELED: return "MINI_ERR_CANCELED";
        case MINI_ERR_PERM: return "MINI_ERR_PERM";
        case MINI_ERR_PROTO: return "MINI_ERR_PROTO";
        case MINI_ERR_AUTH: return "MINI_ERR_AUTH";

        default: return "MINI_ERR_UNKNOWN";
    }
}
