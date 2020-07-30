/*
 * This driver creates fake I2C buses which can contain emulated devices,
 * implemented by a separate emulation driver. The API between this driver and
 * its emulators is defined by struct i2c_emul_driver_api.
 *
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_i2c_emul_controller

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_emul_ctlr);

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/** Working data for the device */
struct i2c_emul_data {
	uint32_t i2c_cfg;
};

/** Read-only configuration data for the device */
struct i2c_emul_cfg {
	uint32_t base;
};

static sys_slist_t i2c_emul_list;

static struct i2c_emul_registration *i2c_emul_find(int addr)
{
	sys_snode_t *node;

	SYS_SLIST_FOR_EACH_NODE(&i2c_emul_list, node) {
		struct i2c_emul_registration *emul = NULL;

		emul = CONTAINER_OF(node, struct i2c_emul_registration, node);
		if (emul->addr == addr) {
			return emul;
		}
	}

	return NULL;
}

static int i2c_emul_configure(struct device *dev, uint32_t dev_config)
{
	struct i2c_emul_data *data = dev->data;
	sys_snode_t *node;

	/*
	 * Tell all the emulators, since they have no way of looking up this
	 * information otherwise
	 */
	SYS_SLIST_FOR_EACH_NODE(&i2c_emul_list, node) {
		struct i2c_emul_registration *emul;
		const struct i2c_emul_api *api;
		int ret;

		emul = CONTAINER_OF(node, struct i2c_emul_registration, node);
		api = emul->api;
		if (!api || !api->transfer) {
			LOG_ERR("Emulator is missing configure function");
			return -EINVAL;
		}
		ret = api->configure(emul->inst, dev_config);
		if (ret) {
			return ret;
		}
	}

	data->i2c_cfg = dev_config;

	return 0;
}

static int i2c_emul_transfer(struct device *dev, struct i2c_msg *msgs,
			     uint8_t num_msgs, uint16_t addr)
{
	struct i2c_emul_registration *emul;
	const struct i2c_emul_api *api;
	int ret;

	emul = i2c_emul_find(addr);
	if (!emul) {
		return -ENODEV;
	}

	api = emul->api;
	if (!api || !api->transfer) {
		LOG_ERR("Emulator is missing transfer function");
		return -EINVAL;
	}

	ret = api->transfer(emul->inst, msgs, num_msgs, addr);
	if (ret) {
		return ret;
	}

	return 0;
}

static int i2c_emul_init(struct device *dev)
{
	struct i2c_emul_data *data = dev->data;

	data->i2c_cfg = I2C_SPEED_SET(I2C_SPEED_STANDARD) | I2C_MODE_MASTER;

	return 0;
}

int i2c_emul_register(struct i2c_emul_registration *inst)
{
	LOG_INF("Register\n");
	sys_slist_append(&i2c_emul_list, &inst->node);

	return 0;
}

/* Device instantiation */

static struct i2c_driver_api i2c_emul_api = {
	.configure = i2c_emul_configure,
	.transfer = i2c_emul_transfer,
};

#define I2C_EMUL_INIT(n) \
	static struct i2c_emul_cfg i2c_emul_cfg_##n = { \
		.base = DT_INST_REG_ADDR(n), \
	}; \
	static struct i2c_emul_data i2c_emul_data_##n; \
	DEVICE_AND_API_INIT(i2c_##n, \
			    DT_INST_LABEL(n), \
			    i2c_emul_init, \
			    &i2c_emul_data_##n, \
			    &i2c_emul_cfg_##n, \
			    POST_KERNEL, \
			    CONFIG_I2C_INIT_PRIORITY, \
			    &i2c_emul_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_EMUL_INIT)
