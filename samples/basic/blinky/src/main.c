/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>

#define SLEEP_TIME_MS   1000

void main(void)
{
	struct device *dev;
	bool led_is_on = false;
	int ret;

	dev = device_get_binding(DT_ALIAS_LED0_GPIOS_CONTROLLER);
	if (dev == NULL) {
		return;
	}

	ret = gpio_pin_configure(dev, DT_ALIAS_LED0_GPIOS_PIN, GPIO_OUTPUT |
				 DT_ALIAS_LED0_GPIOS_FLAGS);
	if (ret < 0) {
		return;
	}

	while (1) {
		gpio_pin_set(dev, DT_ALIAS_LED0_GPIOS_PIN, led_is_on);
		led_is_on = !led_is_on;
		k_sleep(SLEEP_TIME_MS);
	}
}
