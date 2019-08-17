/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/regulator.h>
#include <drivers/gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(regulator, CONFIG_GPIO_REGULATOR_LOG_LEVEL);

#define OPTION_BOOT_ON_POS 0
#define OPTION_BOOT_ON BIT(OPTION_BOOT_ON_POS)

struct driver_config {
	const char *gpio_name;
	u32_t delay_us;
	u8_t gpio_pin;
	u8_t gpio_flags;
	u8_t options;
};

struct driver_data {
	struct device *dev;
	struct device *gpio;
	struct onoff_service srv;
};

static void start(struct onoff_service *srv,
		  onoff_service_notify_fn notify)
{
	struct driver_data *data = CONTAINER_OF(srv, struct driver_data, srv);
	const struct driver_config *cfg = data->dev->config->config_info;

	int rc = gpio_pin_write(data->gpio, cfg->gpio_pin, true);

	notify(srv, rc);
}

static void stop(struct onoff_service *srv,
		 onoff_service_notify_fn notify)
{
	struct driver_data *data = CONTAINER_OF(srv, struct driver_data, srv);
	const struct driver_config *cfg = data->dev->config->config_info;

	int rc = gpio_pin_write(data->gpio, cfg->gpio_pin, false);

	notify(srv, rc);
}

static int request(struct device *dev, struct onoff_client *cli)
{
	struct driver_data *data = dev->driver_data;

	return onoff_request(&data->srv, cli);
}

static int release(struct device *dev, struct onoff_client *cli)
{
	struct driver_data *data = dev->driver_data;

	return onoff_release(&data->srv, cli);
}

static const struct regulator_driver_api api = {
	.request = request,
	.release = release,
};

static void init_complete(struct onoff_service *srv,
			  struct onoff_client *cli,
			  void *user_data,
			  int res)
{
	*(volatile bool *)user_data = true;
}

static int init(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	int rc = 0;

	data->gpio = device_get_binding(cfg->gpio_name);
	if (data->gpio == NULL) {
		LOG_ERR("no GPIO device: %s", cfg->gpio_name);
		return -EINVAL;
	}

	data->dev = dev;

	rc = gpio_pin_configure(data->gpio, cfg->gpio_pin,
				GPIO_OUTPUT_INACTIVE | cfg->gpio_flags);

	if ((rc == 0)
	    && ((cfg->options & OPTION_BOOT_ON) != 0U)) {
		volatile bool enabled = false;
		struct onoff_client cli;

		onoff_client_init_callback(&cli, init_complete,
					   (void*)&enabled);
		rc = onoff_request(&data->srv, &cli);
		if (rc >= 0) {
			while (!enabled) {
				/* spin */
			}
			rc = cli.result;
		}
	}

	return rc;
}

/*
*/

#define REGULATOR_DEVICE(id) \
static const struct driver_config regulator_##id##_cfg = { \
	.gpio_name = DT_INST_##id##_REGULATOR_GPIO_ENABLE_GPIOS_CONTROLLER, \
	.gpio_pin = DT_INST_##id##_REGULATOR_GPIO_ENABLE_GPIOS_PIN, \
	.gpio_flags = DT_INST_##id##_REGULATOR_GPIO_ENABLE_GPIOS_FLAGS, \
	.options = (DT_INST_##id##_REGULATOR_GPIO_REGULATOR_BOOT_ON << OPTION_BOOT_ON_POS), \
}; \
static struct driver_data regulator_##id##_data = { \
	.srv = ONOFF_SERVICE_INITIALIZER(start, stop, 0,		\
					 ((DT_INST_##id##_REGULATOR_GPIO_GPIO_WAITS \
					  | (DT_INST_##id##_REGULATOR_GPIO_STARTUP_DELAY_US > 0)) \
					 ? ONOFF_SERVICE_START_WAITS \
					 : 0)				\
					 | (DT_INST_##id##_REGULATOR_GPIO_GPIO_WAITS		\
					    ? ONOFF_SERVICE_STOP_WAITS \
					    : 0)),		       \
}; \
DEVICE_AND_API_INIT(regulator_##id, \
		    DT_INST_##id##_REGULATOR_GPIO_LABEL, \
		    init, &regulator_##id##_data, &regulator_##id##_cfg, \
		    POST_KERNEL, 0, &api);

#ifdef DT_INST_0_REGULATOR_GPIO
REGULATOR_DEVICE(0)
#endif
