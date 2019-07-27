/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "drivers/sensple/as3935.h"
#include "drivers/sensple/impl.h"
#include <i2c.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(AS3935, CONFIG_SENSPLE_LOG_LEVEL);

struct driver_data {
	struct sensple_state sensple;
	struct as3935_observation obs;
	struct device *dev;
	struct device *bus;

	bool sample;
};

struct driver_config {
	const char *bus_name;
	u8_t addr;
};

// Forward declaration
static const struct sensple_driver_api driver_api;

enum state_e {
	MST_UNINITIALIZED,
	MST_RESETTING,
	MST_IDLE,
	MST_WAITING_SAMPLE,
	MST_FAILED,
};

static void invalidate_obs(struct as3935_observation *op)
{
}

static bool invalid_obs(const struct as3935_observation *op)
{
	return false;
}

static int reset(const struct device *dev)
{
	const struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd = 0x06;

	return i2c_write(data->bus, &cmd, sizeof(cmd), cfg->addr);
}

static int trigger(const struct device *dev,
		   bool humidity)
{
	const struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;

	return -ENOTSUP;
}

static int fetch_result(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;

	return -ENOTSUP;
}

static int post_reset(struct device *dev)
{
	return 0;
}

static int sample(struct device *dev,
		  unsigned int id)
{
	int rv = -EINVAL;

	if (sensple_impl_match(dev, &driver_api)
	    && (id == 0)) {
		struct driver_data *data = dev->driver_data;
		struct sensple_state *sp = &data->sensple;

		k_sem_take(&sp->sem, K_FOREVER);
		data->sample = true;
		k_sem_give(&sp->sem);
		LOG_DBG("sample raise");
		k_poll_signal_raise(&sp->signal, 0);
		rv = 0;
	}
	return rv;
}

static struct k_poll_signal *access(struct device *dev)
{
	LOG_INF("AS3935 access %p %p", dev->driver_api, &driver_api);
	if (sensple_impl_match(dev, &driver_api)) {
		struct driver_data *sp = dev->driver_data;
		return &sp->sensple.signal;
	}

	return NULL;
}

static int work(struct device *dev)
{
	int rv = 0;
	bool raise = false;
	int rc;

	if (!sensple_impl_match(dev, &driver_api)) {
		rv = -EINVAL;
		goto out;
	}

	struct driver_data *data = dev->driver_data;
	struct sensple_state *sp = &data->sensple;

	if (SENSPLE_STATE_DELAYED & sp->state) {
		LOG_DBG("premature %d", sp->state);
		goto out;
	}

	LOG_DBG("work %u", sp->machine_state);
	k_sem_take(&sp->sem, K_FOREVER);
	switch (sp->machine_state) {
	default:
		sp->machine_state = MST_FAILED;
		/* fallthrough */
	case MST_FAILED:
		break;
	case MST_IDLE:
		if (data->sample) {
			data->sample = false;
			rc = trigger(dev, false);
			LOG_DBG("trigger: %d\n", rc);
			if (rc < 0) {
				LOG_ERR("trigger failed: %d", rc);
				sp->machine_state = MST_FAILED;
			} else {
				invalidate_obs(&data->obs);
				sp->machine_state = MST_WAITING_SAMPLE;
				sensple_impl_delay(sp, K_MSEC(SAMPLE_DELAY_ms));
			}
		}
		break;
	case MST_RESETTING:
 		rc = post_reset(dev);
		if (rc < 0) {
			sp->machine_state = MST_FAILED;
			LOG_ERR("post_reset failed: %d", rc);
		} else {
			sp->machine_state = MST_IDLE;
			raise = true;
		}
		break;
	case MST_WAITING_SAMPLE:
		rc = fetch_result(dev);
		LOG_DBG("fetch_result: %d", rc);

		if (rc < 0) {
			sp->machine_state = MST_FAILED;
		} else if (rc > 0) {
			sp->machine_state = MST_IDLE;
			rv = SENSPLE_STATE_OBSERVATION_READY;
			raise = true;
		} else {
			sp->machine_state = MST_IDLE;
		}
		break;
	}
	if (MST_FAILED == sp->machine_state) {
		rv = -EIO;
	}

	k_sem_give(&sp->sem);
	if (raise) {
		LOG_DBG("work raise");
		k_poll_signal_raise(&sp->signal, 0);
	}

out:
	return rv;
}

int fetch(struct device *dev,
	  void *ptr,
	  size_t len,
	  unsigned int id)
{
	struct driver_data *data = dev->driver_data;

	if (!sensple_impl_match(dev, &driver_api)) {
		return -EINVAL;
	}
	struct as3935_observation *op = ptr;

	if ((id != 0)
	    || (!op)
	    || (len != sizeof(*op))) {
		return -EINVAL;
	}

	struct sensple_state *sp = &data->sensple;

	k_sem_take(&sp->sem, K_FOREVER);
	*op = data->obs;
	k_sem_give(&sp->sem);
	LOG_DBG("return %d cPa %d cCel", op->diffpres_cPa, op->temperature_cCel);

	return invalid_obs(op) ? -ENOENT : 0;
}

static const struct sensple_driver_api driver_api = {
	.access = access,
	.work = work,
	.sample = sample,
	.fetch = fetch,
};

static int device_init(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	struct sensple_state *sp = &data->sensple;
	const struct driver_config *cfg = dev->config->config_info;
	int rc = -EINVAL;

	LOG_INF("AS3935 device init");

	sensple_impl_setup(sp, dev);

	struct device *bus = device_get_binding(cfg->bus_name);

	if (!bus) {
		LOG_DBG("No bus: %s\n", cfg->bus_name);
		goto out;
	}
	data->dev = dev;
	data->bus = bus;
	invalidate_obs(&data->obs);

	rc = reset(dev);
	LOG_DBG("reset got %d", rc);
	if (rc < 0) {
		sp->machine_state = MST_FAILED;
	} else {
		sp->machine_state = MST_RESETTING;
		sensple_impl_delay(sp, K_MSEC(RESET_DELAY_ms));
	}

out:
	k_sem_give(&sp->sem);

	return rc;
};

#ifdef DT_INST_0_SENSIRION_AS3935_LABEL

static const struct driver_config config_0 = {
	.bus_name = DT_INST_0_SENSIRION_AS3935_BUS_NAME,
	.addr = DT_INST_0_SENSIRION_AS3935_BASE_ADDRESS,
};

static struct driver_data data_0;

DEVICE_AND_API_INIT(as3935, DT_INST_0_SENSIRION_AS3935_LABEL,
		    device_init, &data_0, &config_0,
		    POST_KERNEL, CONFIG_SENSPLE_INIT_PRIORITY,
		    &driver_api);

#endif
