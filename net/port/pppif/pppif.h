/* SPDX-License-Identifier: Apache-2.0 */
#ifndef PPPIF_H
#define PPPIF_H
#ifdef __cplusplus
extern "C"
{
#endif
    /**
     * @file pppif.h
     * @brief PPP 接口头文件 (4G 模组 PPPoS 拨号)
     * @param modem_dev_name 模组设备 label (如 "a7670" / "air780e")
     * @param apn APN 名称 (可 NULL)
     * @param user PPP 用户名 (可 NULL)
     * @param pass PPP 密码 (可 NULL)
     * @return 成功返回 0 (VFS_OK), 失败返回负值错误码
     * @details 提供 PPP 拨号初始化 / 反初始化 / 数据态轮询接口。
     *          裸机 (NO_SYS=1) 下 pppif_poll() 需在主循环周期调用,
     *          驱动 PPP 状态机与串口收发。
     */
    int pppif_init(const char* modem_dev_name, const char* apn, const char* user, const char* pass);

    /**
     * @brief PPP 数据态轮询
     * @return 成功返回 0 (VFS_OK), 失败返回负值错误码
     * @details 裸机 (NO_SYS=1) 下需在主循环周期调用, 驱动 PPP 状态机与串口收发。
     */
    int pppif_poll(void);
    /**
     * @brief PPP 反初始化
     * @return 成功返回 0 (VFS_OK), 失败返回负值错误码
     * @details 关闭 PPP 拨号, 释放资源, 断开链路。
     */
    int pppif_deinit(void);
#ifdef __cplusplus
}
#endif
#endif /* PPPIF_H */
