/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include "drivers/sensor/sht21.h"
#include <stdio.h>

static struct device *dev;

static bool test_and_remove(u32_t flag,
			    u32_t *flags)
{
	bool rv = flag & *flags;
	*flags &= ~flag;
	return rv;
}

static void sht21_notify(struct device *dev,
			 u32_t flags,
			 void *user_data)
{
	int rc;

	if (test_and_remove(SENSOR2_NOTIFY_OBSERVATION, &flags)) {
		struct sht21_observation obs = {0};

		rc = sensor2_fetch(dev, &obs, sizeof(obs));
		printf("fetch %d %p: %d cCel %u pptt\n",
		       rc, &obs, obs.temperature_cCel, obs.humidity_pptt);
	}
	if (test_and_remove(SENSOR2_NOTIFY_WORK, &flags)) {
		rc = sensor2_work(dev);
		if (0 != rc) {
			printf("Work got %d\n", rc);
		}
	}
	if (flags) {
		printf("Unhandled notify: %x\n", flags);
	}
}

void main(void)
{
	static const char *binding_name = DT_SENSIRION_SHT21_SHT21_0_LABEL;
	int rc;

	dev = device_get_binding(binding_name);
	if (!dev) {
		printf("Could not get %s device\n", binding_name);
		return;
	}
	rc = sensor2_initialize(dev, sht21_notify, 0);
	printf("Initialize got %d\n", rc);
	do {
		k_sleep(K_SECONDS(1));
		rc = sensor2_sample(dev);
		printf("Sample got %d\n", rc);
	} while (!rc);
	printf("exit %d\n", rc);
}
