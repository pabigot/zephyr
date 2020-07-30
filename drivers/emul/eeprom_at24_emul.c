/*
 * Copyright 2020 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT atmel_at24

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(atmel_at24);

#include <device.h>
#include <drivers/i2c.h>
#include <drivers/i2c_emul.h>

#define REG_COUNT	0x100

/** Defines run-time data used by the emulator */
struct at24_emul_data {
	/** Size of EEPROM */
	uint16_t size;
	/** EEPROM data contents */
	uint8_t *buf;
	/** Current register to read */
	uint8_t cur_reg;
	/** Current I2C config */
	uint32_t dev_config;
	/** Address width for EEPROM in bits (8 or 16) */
	uint8_t addr_width;
	/** true if the bus is too fast */
	bool too_fast;
};

struct at24_emul_cfg {
	const char *dev;
};

static int at24_emul_configure(void *inst_in, uint32_t dev_config)
{
	struct at24_emul_data *inst = inst_in;

	if (inst == NULL) {
		LOG_ERR("Inst is NULL");
		return -EINVAL;
	}
	inst->dev_config = dev_config;

	return 0;
}

static int at24_emul_transfer(void *inst_in,
			      struct i2c_msg *msgs, int num_msgs, int addr)
{
	struct at24_emul_data *inst = inst_in;
	unsigned int len;

	inst->too_fast = I2C_SPEED_GET(inst->dev_config) > I2C_SPEED_STANDARD;

	i2c_dump_msgs("emul", msgs, num_msgs, addr);
	switch (num_msgs) {
	case 1:
		if (msgs->flags & I2C_MSG_READ) {
			/* handle read */
			break;
		}
		inst->cur_reg = msgs->buf[0];
		len = MIN(msgs->len - 1, inst->size - inst->cur_reg);
		memcpy(&inst->buf[inst->cur_reg], &msgs->buf[1], len);
		return 0;
	case 2:
		if (msgs->flags & I2C_MSG_READ) {
			LOG_ERR("Unexpected read");
			return -EREMOTEIO;
		}
		inst->cur_reg = msgs->buf[0];

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

	len = MIN(msgs->len, inst->size - inst->cur_reg);
	memcpy(msgs->buf, &inst->buf[inst->cur_reg], len);
	inst->cur_reg += len;

	return 0;
}

/* Device instantiation */

static struct i2c_emul_api at24_emul_api = {
	.configure = at24_emul_configure,
	.transfer = at24_emul_transfer,
};

static int at24_emul_init(struct device *dev)
{
	struct i2c_emul_registration *emul;
	struct at24_emul_data *inst;
	int i;

	/* TBD: Simon would prefer to use struct device */
	emul = (struct i2c_emul_registration *)dev;
	inst = emul->inst;

	inst->dev_config = 0;
	inst->too_fast = false;

	/* Use some fixed data */
	for (i = 0; i < inst->size; i++)
		inst->buf[i] = (0x10 + i) & 0xff;

	inst->cur_reg = 0;

	return i2c_emul_register(emul);
}

/*
 * TODO: This uses Z_INIT_ENTRY_DEFINE which requires sdtruct device. Either we
 * should move to using a real device or we should have another way of doing
 * the init that doesn't require a cast.
 */
#define EEPROM_AT24_EMUL(n) \
	static uint8_t at24_emul_buf_##n[DT_PROP(DT_INST(n, atmel_at24), size)]; \
	static struct at24_emul_data at24_emul_data_##n = { \
		.size = DT_PROP(DT_INST(n, atmel_at24), size), \
		.buf = at24_emul_buf_##n, \
		.dev_config = I2C_SPEED_SET(I2C_SPEED_STANDARD) | \
			I2C_MODE_MASTER, \
		.addr_width = 8, \
	}; \
	static struct i2c_emul_registration at24_emul_reg_##n = { \
		.inst = &at24_emul_data_##n, \
		.api = &at24_emul_api, \
		.addr = DT_REG_ADDR(DT_INST(n, atmel_at24)), \
	}; \
	Z_INIT_ENTRY_DEFINE(at24_emul_##n, at24_emul_init, \
			    (struct device *)&at24_emul_reg_##n, \
			    POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

DT_INST_FOREACH_STATUS_OKAY(EEPROM_AT24_EMUL)
