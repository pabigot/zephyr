/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* @brief Real-time clock control based on the DS3231 counter API. */
#ifndef ZEPHYR_INCLUDE_DRIVERS_RTC_DS3231_H_
#define ZEPHYR_INCLUDE_DRIVERS_RTC_DS3231_H_

#include <zephyr/types.h>
#include <time.h>
#include <counter.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Summary */
struct rtc_ds3231_configuration {
};

typedef int (*rtc_ds3231_api_get_configuration)(struct device* dev,
						struct rtc_ds3231_configuration* cfg);

typedef int (*rtc_ds3231_api_configure)(struct device* dev,
					const struct rtc_ds3231_configuration* cfg);

struct rtc_ds3231_driver_api {
	struct counter_driver_api counter_api;
	rtc_ds3231_api_get_configuration get_configuration;
	rtc_ds3231_api_configure configure;
};

/** @brief Read the DS3231 configuration and state. */
__syscall int rtc_ds3231_get_configuration(struct device* dev,
					   struct rtc_ds3231_configuration* cfg);

static inline int z_impl_rtc_ds3231_get_configuration(struct device* dev,
						      struct rtc_ds3231_configuration* cfg)
{
	const struct rtc_ds3231_driver_api *api =
		(const struct rtc_ds3231_driver_api*)dev->driver_api;
	return api->get_configuration(dev, cfg);
}

/** @brief Write the DS3231 configuration. */
__syscall int rtc_ds3231_configure(struct device* dev,
				   const struct rtc_ds3231_configuration* cfg);

static inline int z_impl_rtc_ds3231_configure(struct device* dev,
					      const struct rtc_ds3231_configuration* cfg)
{
	const struct rtc_ds3231_driver_api *api =
		(const struct rtc_ds3231_driver_api*)dev->driver_api;
	return api->configure(dev, cfg);
}

#ifdef __cplusplus
}
#endif

// @todo this should be syscalls/drivers/rtc/ds3231.h
#include <syscalls/ds3231.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_RTC_DS3231_H_ */
