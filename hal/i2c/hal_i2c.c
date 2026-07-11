/* SPDX-License-Identifier: Apache-2.0 */
#include "hal_i2c.h"

static int hal_i2c_gpio_config(const struct hal_i2c_gpio_cfg* gpio_cfg)
{
    if(!gpio_cfg || !gpio_cfg->port || !gpio_cfg->pin || !gpio_cfg->clk_bus)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

int hal_i2c_dev_init(struct hal_i2c_dev* dev, const struct hal_i2c_host_config* host_cfg)
{
    if(!dev || !host_cfg)
        return VFS_ERR_INVAL;

    return VFS_OK;
}

int hal_i2c_dev_deinit(void)
{
    return VFS_OK;
}

int hal_i2c_init(void)
{
    return VFS_OK;
}

int hal_i2c_start(void)
{
    return VFS_OK;
}
