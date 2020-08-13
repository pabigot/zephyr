/*
 * This driver creates fake I2C buses which can contain emulated devices,
 * implemented by a separate emulation driver. The API between this driver and
 * its emulators is defined by struct i2c_emul_driver_api.
 *
 * Copyright 2020 Google LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_i2c_emul_controller

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c_emul_ctlr);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

/** Working data for the device */
struct i2c_emul_data {
	/* List of i2c_emul_registrations associated with the device. */
	sys_slist_t emuls;
	/* I2C host configuration. */
	uint32_t config;
};

/** Structure uniquely identifying a device that is a child of an i2c_emul node.
 *
 * Currently this uses the device node label, but that will go away by 2.5. */
struct emul_link {
	const char *label;
};

/** Read-only configuration data for the device */
struct i2c_emul_cfg {
	/* Identifiers for children of the node. */
	const struct emul_link *children;
	/* Number of children (devices on the bus) */
	uint32_t num_children;
	uint32_t base;
};

uint32_t i2c_emul_configuration(struct device *dev)
{
	struct i2c_emul_data *data = dev->data;

	return data->config;
}

static struct i2c_emul_registration *i2c_emul_find(struct device *dev,
						   int addr)
{
	struct i2c_emul_data *data = dev->data;
	sys_snode_t *node;

	SYS_SLIST_FOR_EACH_NODE(&data->emuls, node) {
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

	data->config = dev_config;
	return 0;
}

static int i2c_emul_transfer(struct device *dev, struct i2c_msg *msgs,
			     uint8_t num_msgs, uint16_t addr)
{
	struct i2c_emul_registration *emul;
	const struct i2c_emul_api *api;
	int ret;

	emul = i2c_emul_find(dev, addr);
	if (!emul) {
		/* TBD: value to match unacknowledged I2C transaction */
		return -EIO;
	}

	api = emul->api;
	__ASSERT_NO_MSG(emul->api);
	__ASSERT_NO_MSG(emul->api->transfer);

	ret = api->transfer(emul, msgs, num_msgs, addr);
	if (ret) {
		return ret;
	}

	return 0;
}

/* Helper to search for the (unique) registration of an emulator for a device.
 * Should be extracted to the emulator module. */
static const struct emulator_registration *
find_reg(const struct emul_link *elp)
{
	const struct emulator_registration *erp = __emul_registration_start;
	const struct emulator_registration *erpe = __emul_registration_end;

	while (erp < erpe) {
		if (strcmp(erp->dev_label, elp->label) == 0) {
			return erp;
		}
		++erp;
	}

	return NULL;
}

static int i2c_emul_init(struct device *dev)
{
	struct i2c_emul_data *data = dev->data;
	const struct i2c_emul_cfg *cfg = dev->config;

	sys_slist_init(&data->emuls);

	/* Walk the list of children, find the corresponding emulator
	 * registration, and initialize the emulator.
	 */
	const struct emul_link *elp = cfg->children;
	const struct emul_link *const elpe = elp + cfg->num_children;
	while (elp < elpe) {
		const struct emulator_registration *erp = find_reg(elp);

		__ASSERT_NO_MSG(erp);

		int rc = erp->init(erp, dev);

		if (rc != 0) {
			LOG_WRN("Init %s emulator failed: %d\n",
				 elp->label, rc);
		}

		++elp;
	}

	return 0;
}

int i2c_emul_register(struct device *dev,
		      struct i2c_emul_registration *reg)
{
	struct i2c_emul_data *data = dev->data;

	sys_slist_append(&data->emuls, &reg->node);

	LOG_INF("Register emulator at %02x\n", reg->addr);
	return 0;
}

/* Device instantiation */

static struct i2c_driver_api i2c_emul_api = {
	.configure = i2c_emul_configure,
	.transfer = i2c_emul_transfer,
};

#define EMUL_LINK_AND_COMMA(node_id) {		\
	.label = DT_LABEL(node_id),		\
},

#define I2C_EMUL_INIT(n) \
	static const struct emul_link emuls_##n[] = { \
		DT_FOREACH_CHILD(DT_DRV_INST(0), EMUL_LINK_AND_COMMA) \
	}; \
	static struct i2c_emul_cfg i2c_emul_cfg_##n = { \
		.children = emuls_##n, \
                .num_children = ARRAY_SIZE(emuls_##n), \
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
