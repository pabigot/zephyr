/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>

#include <device.h>
#include <counter.h>
#include <misc/printk.h>
#include <drivers/rtc/ds3231.h>

void main(void)
{
	struct device *ds3231;

	printk("Test DS3231 driver\n");
	ds3231 = device_get_binding(DT_INST_0_MAXIM_DS3231_LABEL);
	if (!ds3231) {
		printk("No device available\n");
		return;
	}

	printk("Counter at %p\n", ds3231);
	printk("\tMax top value: %u (%08x)\n",
	       counter_get_max_top_value(ds3231),
	       counter_get_max_top_value(ds3231));
	printk("\t%u channels\n", counter_get_num_of_channels(ds3231));
	printk("\t%u Hz\n", counter_get_frequency(ds3231));

	printk("Top counter value: %u (%08x)\n",
	       counter_get_top_value(ds3231),
	       counter_get_top_value(ds3231));

	struct rtc_ds3231_configuration cfg;
	int rc = rtc_ds3231_get_configuration(ds3231, &cfg);
	printk("Get config: %d\n", rc);
}
