/*
 * Copyright 2020 Google LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT atmel_at24

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(atmel_at24);

#include <device.h>
#include <emul.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#define REG_COUNT	0x100

struct at24_emul_cfg;

/** Defines run-time data used by the emulator */
struct at24_emul_data {
	struct i2c_emul_registration reg;

	struct device *i2c;

	const struct at24_emul_cfg *cfg;

	/** Current register to read (address) */
	uint32_t cur_reg;
	/** Current I2C config */
	uint32_t dev_config;
	/** true if the bus is too fast */
	bool too_fast;
};

struct at24_emul_cfg {
	const char *i2c_label;

	struct at24_emul_data *data;

	/** EEPROM data contents */
	uint8_t *buf;

	/** Size of EEPROM */
	uint32_t size;

	uint16_t addr;

	/** Address width for EEPROM in bits (8 or 16) */
	uint8_t addr_width;
};

static int at24_emul_transfer(struct i2c_emul_registration *reg,
			      struct i2c_msg *msgs, int num_msgs, int addr)
{
	struct at24_emul_data *data = CONTAINER_OF(reg, struct at24_emul_data, reg);
	const struct at24_emul_cfg *cfg = data->cfg;
	unsigned int len;

	data->too_fast = I2C_SPEED_GET(i2c_emul_configuration(data->i2c)) > I2C_SPEED_STANDARD;

	i2c_dump_msgs("emul", msgs, num_msgs, addr);
	switch (num_msgs) {
	case 1:
		if (msgs->flags & I2C_MSG_READ) {
			/* handle read */
			break;
		}
		data->cur_reg = msgs->buf[0];
		len = MIN(msgs->len - 1, cfg->size - data->cur_reg);
		memcpy(&cfg->buf[data->cur_reg], &msgs->buf[1], len);
		return 0;
	case 2:
		if (msgs->flags & I2C_MSG_READ) {
			LOG_ERR("Unexpected read");
			return -EREMOTEIO;
		}
		data->cur_reg = msgs->buf[0];

		/* Now process the 'read' part of the message */
		msgs++;
		if (!(msgs->flags & I2C_MSG_READ)) {
			LOG_ERR("Unexpected write");
			return -EREMOTEIO;
		}
		break;
	default:
		LOG_ERR("Invalid number of messages");
		return -EREMOTEIO;
	}

	len = MIN(msgs->len, cfg->size - data->cur_reg);
	memcpy(msgs->buf, &cfg->buf[data->cur_reg], len);
	data->cur_reg += len;

	return 0;
}

/* Device instantiation */

static struct i2c_emul_api at24_emul_api = {
	.transfer = at24_emul_transfer,
};

static int emul_atmel_at24_init(const struct emulator_registration *reg,
				struct device *parent)
{
	const struct at24_emul_cfg *cfg = reg->cfg;
	struct at24_emul_data *data = cfg->data;

	/* TBD: Simon would prefer to use struct device */

	data->reg.api = &at24_emul_api;
	data->reg.addr = cfg->addr;

	data->i2c = parent;
	data->cfg = cfg;

	memset(cfg->buf, 0xFF, cfg->size);

	int rc = i2c_emul_register(parent, &data->reg);

	return rc;
}

#define EEPROM_AT24_EMUL(n) \
	static uint8_t at24_emul_buf_##n[DT_INST_PROP(n, size)]; \
	static struct at24_emul_data at24_emul_data_##n; \
	static struct at24_emul_cfg at24_emul_cfg_##n = { \
		.i2c_label = DT_INST_BUS_LABEL(n), \
		.data = &at24_emul_data_##n, \
		.buf = at24_emul_buf_##n, \
		.size = DT_INST_PROP(n, size), \
		.addr = DT_INST_REG_ADDR(n), \
		.addr_width = 8, \
	}; \
	EMULATOR_DEFINE(emul_atmel_at24_init, DT_DRV_INST(n), &at24_emul_cfg_##n)

DT_INST_FOREACH_STATUS_OKAY(EEPROM_AT24_EMUL)
