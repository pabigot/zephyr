/**
 * @file
 *
 * @brief Public APIs for the I2C emulation drivers.
 */

/*
 * Copyright 2020 Google LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_I2C_I2C_EMUL_H_
#define ZEPHYR_INCLUDE_DRIVERS_I2C_I2C_EMUL_H_

/**
 * @brief I2C Emulation Interface
 * @defgroup i2c_emul_interface I2C Emulation Interface
 * @ingroup io_emulators
 * @{
 */

#include <zephyr/types.h>
#include <device.h>

#ifdef __cplusplus
extern "C" {
#endif

struct i2c_msg;
struct i2c_emul_api;

/** Node in a linked list of registered emulated I2C devices */
struct i2c_emul_registration {
	sys_snode_t node;

	/* API provided for this device */
	const struct i2c_emul_api *api;

	/* I2C address of the emulated device */
	uint16_t addr;
};

/**
 * Passes I2C messages to the emulator. The emulator updates the data with what
 * was read back.
 *
 * @param inst Emulator instance (@inst from struct i2c_emul_registration)
 * @param msgs Array of messages to transfer.
 * @param num_msgs Number of messages to transfer.
 * @param addr Address of the I2C target device.
 *
 * @retval 0 If successful.
 * @retval -EIO General input / output error.
 */
typedef int (*i2c_emul_transfer_t)(struct i2c_emul_registration *reg,
				   struct i2c_msg *msgs,
				   int num_msgs, int addr);

/**
 * Register an emulated device on the controller
 */
int i2c_emul_register(struct device *dev,
		      struct i2c_emul_registration *inst);

struct i2c_emul_api {
	i2c_emul_transfer_t transfer;
};

/**
 * Back door to allow an emulator to retrieve the host configuration.
 */
uint32_t i2c_emul_configuration(struct device *dev);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_I2C_I2C_EMUL_H_ */
