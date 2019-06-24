/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <i2c.h>
#include <kernel.h>
#include <drivers/rtc/ds3231.h>
#include <logging/log.h>

#define LOG_LEVEL CONFIG_COUNTER_LOG_LEVEL
LOG_MODULE_REGISTER(DS3231);

struct gpios {
	const char *ctrl;
	u32_t pin;
};

struct ds3231_config {
	/* Common structure first because generic API expects this here. */
	struct counter_config_info generic;
	const char *bus_name;
	struct gpios sig32k_gpios;
	struct gpios isw_gpios;
	u16_t addr;
};

struct ds3231_data {
	struct device *i2c;
	struct device *sig32k;
	struct device *isw;
};

static int ds3231_get_configuration(struct device* dev,
				    struct rtc_ds3231_configuration* cp)
{
	return -ENOTSUP;
}

static int ds3231_configure(struct device* dev,
			    const struct rtc_ds3231_configuration* cfg)
{
	return -ENOTSUP;
}

static int ds3231_init(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	struct device *i2c = device_get_binding(cfg->bus_name);

	if (i2c == NULL) {
		LOG_INF("Failed to get I2C %s", cfg->bus_name);
		return -EINVAL;
	}
	data->i2c = i2c;

	if (cfg->sig32k_gpios.ctrl != NULL) {
		struct device *gpio = device_get_binding(cfg->sig32k_gpios.ctrl);

		if (!gpio) {
			LOG_INF("Failed to get 32k GPIO %s", cfg->sig32k_gpios.ctrl);
			return -EINVAL;
		}
		data->sig32k = gpio;
	}

	if (cfg->isw_gpios.ctrl != NULL) {
		struct device *gpio = device_get_binding(cfg->isw_gpios.ctrl);

		if (!gpio) {
			LOG_INF("Failed to get INTn/SQW GPIO %s", cfg->isw_gpios.ctrl);
			return -EINVAL;
		}
		data->isw = gpio;
	}

	return 0;
}

static int ds3231_start(struct device *dev)
{
	return -EALREADY;
}

static int ds3231_stop(struct device *dev)
{
	return -ENOTSUP;
}

static u32_t ds3231_read(struct device *dev)
{
	return -ENOTSUP;
}

int ds3231_set_alarm(struct device *dev,
		     u8_t chan_id,
		     const struct counter_alarm_cfg *alarm_cfg)
{
	return -ENOTSUP;
}

int ds3231_cancel_alarm(struct device *dev,
			u8_t chan_id)
{
	return -ENOTSUP;
}

static u32_t ds3231_get_top_value(struct device *dev)
{
	return UINT32_MAX;
}

static u32_t ds3231_get_pending_int(struct device *dev)
{
	return 0;
}

static int ds3231_set_top_value(struct device *dev,
				u32_t ticks,
				counter_top_callback_t callback,
				void *user_data)
{
	return -ENOTSUP;
}

static u32_t ds3231_get_max_relative_alarm(struct device *dev)
{
	return UINT32_MAX;
}

static const struct rtc_ds3231_driver_api ds3231_api = {
	.counter_api = {
		.start = ds3231_start,
		.stop = ds3231_stop,
		.read = ds3231_read,
		.set_alarm = ds3231_set_alarm,
		.cancel_alarm = ds3231_cancel_alarm,
		.set_top_value = ds3231_set_top_value,
		.get_pending_int = ds3231_get_pending_int,
		.get_top_value = ds3231_get_top_value,
		.get_max_relative_alarm = ds3231_get_max_relative_alarm,
		.get_user_data = 0,
	},
	.get_configuration = ds3231_get_configuration,
	.configure = ds3231_configure,
};

static const struct ds3231_config ds3231_0_config = {
	.generic = {
		.max_top_value = UINT32_MAX,
		.freq = 1,
		.flags = COUNTER_CONFIG_INFO_COUNT_UP,
		.channels = 0,
	},
	.bus_name = DT_INST_0_MAXIM_DS3231_BUS_NAME,
#ifdef DT_INST_0_MAXIM_DS3231_32K_GPIOS_CONTROLLER
	.sig32k_gpios = {
		.ctrl = DT_INST_0_MAXIM_DS3231_32K_GPIOS_CONTROLLER,
		.pin = DT_INST_0_MAXIM_DS3231_32K_GPIOS_PIN,
	},
#endif
#ifdef DT_INST_0_MAXIM_DS3231_ISW_GPIOS_CONTROLLER
	.isw_gpios = {
		.ctrl = DT_INST_0_MAXIM_DS3231_ISW_GPIOS_CONTROLLER,
		.pin = DT_INST_0_MAXIM_DS3231_ISW_GPIOS_PIN,
	},
#endif
	.addr = DT_INST_0_MAXIM_DS3231_BASE_ADDRESS,
};

static struct ds3231_data ds3231_0_data;

#if CONFIG_COUNTER_DS3231_INIT_PRIORITY <= CONFIG_I2C_INIT_PRIORITY
#error COUNTER_DS3231_INIT_PRIORITY must be greater than I2C_INIT_PRIORITY
#endif

DEVICE_AND_API_INIT(ds3231_0, DT_INST_0_MAXIM_DS3231_LABEL,
		    ds3231_init, &ds3231_0_data,
		    &ds3231_0_config,
		    POST_KERNEL, CONFIG_COUNTER_DS3231_INIT_PRIORITY,
		    &ds3231_api);
