/*
 * Logging of I2C messages
 *
 * Copyright Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <drivers/i2c.h>

#define LOG_LEVEL CONFIG_I2C_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(i2c);

void i2c_dump_msgs(const char *name, const struct i2c_msg *msgs,
		   uint8_t num_msgs, uint16_t addr)
{
	char data[80], *ptr, *end = data + sizeof(data) - 3;
	unsigned int i;

	LOG_DBG("I2C msg: %s, addr=%x", name, addr);
	for (i = 0; i < num_msgs; i++) {
		const struct i2c_msg *msg = msgs + i;
		unsigned int j;

		if (msg->flags & I2C_MSG_READ) {
			*data = '\0';
		} else {
			for (j = 0, ptr = data; j < msg->len && ptr < end; j++)
				ptr += sprintf(ptr, "%02x ", msg->buf[j]);
		}
		LOG_DBG("   %c len=%02x: %s",
			msg->flags & I2C_MSG_READ ? 'R' : 'W', msg->len, data);
	}
}
