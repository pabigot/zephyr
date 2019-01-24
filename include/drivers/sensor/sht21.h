/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Application header for SHT21 sensor
 * @file */


#ifndef ZEPHYR_DRIVERS_SENSOR_SHT21_H_
#define ZEPHYR_DRIVERS_SENSOR_SHT21_H_

#include <zephyr/types.h>
#include <sensor2.h>

struct sht21_observation {
	s16_t temperature_cCel;
	u16_t humidity_pptt;
};

#endif /* ZEPHYR_DRIVERS_SENSOR_SHT21_H_ */
