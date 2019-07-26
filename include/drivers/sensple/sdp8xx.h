/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Application header for SDP800/SDP810 sensor
 * @file */


#ifndef ZEPHYR_DRIVERS_SENSPLE_SDP8XX_H_
#define ZEPHYR_DRIVERS_SENSPLE_SDP8XX_H_

#include <zephyr/types.h>
#include <errno.h>
#include <drivers/sensple.h>

/** Product number matcher for 500 Pa range manifold-connection
 * device.
 */
#define SENSOR_SDP8XX_PN_SDP800_500Pa 0x03020100

/** Product number matcher for 500 Pa range tube-connection
 * device.
 */
#define SENSOR_SDP8XX_PN_SDP810_500Pa_PN = 0x03020A00;

/** Product number matcher for 125 Pa range manifold-connection
 * device.
 */
#define SENSOR_SDP8XX_PN_SDP800_125Pa_PN = 0x03020200;

/** Product number matcher for 125 Pa range tube-connection
 * device.
 */
#define SENSOR_SDP8XX_PN_SDP810_125Pa_PN = 0x03020B00;

#define SENSOR_SDP8XX_INVALID_OBSERVATION -10000000

struct sdp8xx_observation {
	s32_t diffpres_cPa;
	s16_t temperature_cCel;
};

static inline int sdp8xx_sample(struct device *dev)
{
	return sensple_sample(dev, 0);
}

static inline int sdp8xx_fetch(struct device *dev,
			       struct sdp8xx_observation *op)
{
	return sensple_fetch(dev, op, sizeof(*op), 0);
}

/** Read the device information.
 *
 * @param[out] product a 32-bit product identifier.  The upper 24 bits
 * match one of the product numbers (e.g. @ref SDP810_125Pa_PN)
 * while the low 8 bits are a revision number.
 *
 * @param[out] serial a 64-bit unique serial number for the
 * device. */
int sdp8xx_device_info (struct device *dev,
			u32_t *product,
			u64_t *serial);

#endif /* ZEPHYR_DRIVERS_SENSPLE_SDP8XX_H_ */
