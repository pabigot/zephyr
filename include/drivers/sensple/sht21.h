/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Application header for SHT21 sensor
 * @file */


#ifndef ZEPHYR_DRIVERS_SENSPLE_SHT21_H_
#define ZEPHYR_DRIVERS_SENSPLE_SHT21_H_

#include <drivers/sensple.h>

struct sht21_observation {
	s16_t temperature_cCel;
	u16_t humidity_pptt;
};

static inline int sht21_sample(struct device *dev)
{
	return sensple_sample(dev, 0);
}

static inline int sht21_fetch(struct device *dev,
			      struct sht21_observation *op)
{
	return sensple_fetch(dev, op, sizeof(*op), 0);
}


#endif /* ZEPHYR_DRIVERS_SENSPLE_SHT21_H_ */
