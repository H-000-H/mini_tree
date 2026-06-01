#ifndef VFS_H
#define VFS_H

/* file_operation_t 已由 device.h 统一提供 */

/* ── POSIX 风格标准错误码 ── */
#define VFS_OK         0
#define VFS_ERR_INVAL -1  /* 无效参数 */
#define VFS_ERR_NOMEM -2  /* 内存不足 */
#define VFS_ERR_IO    -3  /* 物理 IO 错误 */
#define VFS_ERR_BUSY  -4  /* 设备忙 */
#define VFS_ERR_AGAIN -5  /* 重试 */
#define VFS_ERR_NOSPC -6  /* 无剩余空间/通道 */
#define VFS_ERR_TIMEOUT -7  /* 锁获取/操作超时 */
#define VFS_ERR_HW_FATAL -8  /* 硬件物理故障, 不可恢复 (短路/断路) */
#define VFS_ERR_DEFER   -9  /* 依赖未就绪, 稍后重试 (EPROBE_DEFER) */

#endif /* VFS_H */
