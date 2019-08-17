/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief API for regulators controlled by a GPIO.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_GPIO_REGULATOR_H_
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_REGULATOR_H_

#include <zephyr/types.h>
#include <drivers/gpio.h>
#include <sys/onoff.h>

#ifdef __cplusplus
extern "C" {
#endif

struct regulator_driver_api {
	int (*request)(struct device *dev, struct onoff_client *cli);
	int (*release)(struct device *dev, struct onoff_client *cli);
};

/**
 * @brief Request a regulator.
 */
static inline int regulator_request(struct device *reg,
				    struct onoff_client *cli)
{
	const struct regulator_driver_api *api =
		(const struct regulator_driver_api *)reg->driver_api;

	return api->request(reg, cli);
}

/**
 * @brief Release a regulator.
 */
static inline int regulator_release(struct device *reg,
				    struct onoff_client *cli)
{
	const struct regulator_driver_api *api =
		(const struct regulator_driver_api *)reg->driver_api;

	return api->release(reg, cli);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_GPIO_REGULATOR_H_ */
