/*
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_sim_i2c

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_sim);

#include <device.h>
#include "i2c-priv.h"

/* Structure declarations */

struct i2c_sim_data {
	bool slave_attached;
	struct i2c_slave_config *slave_cfg;
};

struct i2c_sim_cfg {
	uint32_t base;
};

static int i2c_sim_configure(struct device *dev, uint32_t dev_config)
{
	const struct i2c_sim_cfg *config = NULL;

	if (dev == NULL) {
		LOG_ERR("Device handle is NULL");
		return -EINVAL;
	}

	config = dev->config_info;
	if (!config) {
		LOG_ERR("Device config is NULL");
		return -EINVAL;
	}

	return 0;
}

static int i2c_sim_transfer(struct device *dev, struct i2c_msg *msgs,
			    uint8_t num_msgs, uint16_t addr)
{
	struct i2c_sim_data *data = dev->driver_data;
	const struct i2c_slave_callbacks *cbs = data->slave_cfg->callbacks;
	int rc;

	if (!data->slave_attached) {
		return -EINVAL;
	}
	rc = cbs->slave_transfer(data->slave_cfg, msgs, num_msgs, addr);

	return rc;
}

#ifdef CONFIG_I2C_SLAVE
static int i2c_sim_slave_register(struct device *dev,
				  struct i2c_slave_config *cfg)
{
	struct i2c_sim_data *data = dev->driver_data;

	if (!cfg || !cfg->callbacks) {
		return -EINVAL;
	}
	if (!cfg->callbacks->slave_transfer) {
		return -ENOTSUP;
	}
	if (data->slave_attached) {
		return -EBUSY;
	}
	data->slave_cfg = cfg;
	data->slave_attached = true;

	return 0;
}

static int i2c_sim_slave_unregister(struct device *dev,
				    struct i2c_slave_config *cfg)
{
	struct i2c_sim_data *data = dev->driver_data;

	if (!data->slave_attached) {
		return -EINVAL;
	}
	data->slave_cfg = NULL;
	data->slave_attached = false;

	return 0;
}
#endif

static int i2c_sim_init(struct device *dev)
{
	/* Nothing to do here so far */

	return 0;
}

/* Device instantiation */

static struct i2c_driver_api i2c_sim_api = {
	.configure = i2c_sim_configure,
	.transfer = i2c_sim_transfer,
#ifdef CONFIG_I2C_SLAVE
	.slave_register = i2c_sim_slave_register,
	.slave_unregister = i2c_sim_slave_unregister,
#endif
};

#define I2C_SIM_INIT(n) \
	static struct i2c_sim_cfg i2c_sim_cfg_##n = { \
		.base = DT_INST_REG_ADDR(n), \
	}; \
	static struct i2c_sim_data i2c_sim_data_##n; \
	DEVICE_AND_API_INIT(i2c_##n, \
			    DT_INST_LABEL(n), \
			    i2c_sim_init, \
			    &i2c_sim_data_##n, \
			    &i2c_sim_cfg_##n, \
			    POST_KERNEL, \
			    CONFIG_I2C_INIT_PRIORITY, \
			    &i2c_sim_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_SIM_INIT)
