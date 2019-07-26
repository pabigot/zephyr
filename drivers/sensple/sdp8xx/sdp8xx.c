/*
 * Copyright (c) 2018-2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sensirion_sdp8xx

#include <drivers/i2c.h>
#include <drivers/sensple/sdp8xx.h>
#include <drivers/sensple/impl.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(SDP8XX, CONFIG_SENSPLE_LOG_LEVEL);

struct driver_data {
	struct sensple_state sensple;
	struct sdp8xx_observation obs;
	struct device *dev;
	struct device *bus;

	u16_t scale_factor;
	bool sample;
};

struct driver_config {
	const char *bus_name;
	u8_t addr;
};

// Forward declaration
static const struct sensple_driver_api driver_api;

#define RESET_DELAY_ms 2
#define SAMPLE_DELAY_ms 50

/** Bit set in configure() parameter/result to disable
 * configuration reset on each measurement. */
#define CONFIG_OTPRn 0x02

/** Bit set in configure() parameter/result to enable the
 * on-chip heater */
#define CONFIG_HEATER 0x04

/** Bit set in configure() result indicating Vdd is below
 * 2.25V */
#define CONFIG_EOB 0x40

#define CMD_READ_PI1 0x367C
#define CMD_READ_PI2 0xE102
#define CMD_TRIG_DP_POLL 0x362F

/* These constants define the bits for componentized basic 8-bit
 * commands described in the data sheet. */
#define CMDBIT_BASE    0xE0
#define CMDBIT_READ    0x01
#define CMDBIT_TEMP    0x02
#define CMDBIT_RH      0x04
#define CMDBIT_UR      0x06
#define CMDBIT_NOHOLD  0x10
#define CMD_SOFT_RESET 0xFE

#define SAMPLE_TYPE_Temperature 0
#define SAMPLE_TYPE_Humidity 1
#define SAMPLE_TYPE_Pos 1
#define SAMPLE_TYPE_Msk (1U << SAMPLE_TYPE_Pos)

#define STATUS_Msk 0x0003

#define ZERO_cK 27315

enum state_e {
	MST_UNINITIALIZED,
	MST_RESETTING,
	MST_IDLE,
	MST_WAITING_SAMPLE,
	MST_FAILED,
};

static void invalidate_obs(struct sdp8xx_observation *op)
{
	op->diffpres_cPa = SENSOR_SDP8XX_INVALID_OBSERVATION;
}

static bool invalid_obs(const struct sdp8xx_observation *op)
{
	return op->diffpres_cPa == SENSOR_SDP8XX_INVALID_OBSERVATION;
}

typedef u8_t crc_type;
#define CRC_OK 0

static crc_type crc(const u8_t *dp,
		    size_t count)
{
	const u16_t poly = 0x131;
	const crc_type sobit = 0x80;
	crc_type crc = 0xFF;

	while (count--) {
		crc ^= *dp++;
		for (unsigned int bi = 0; bi < 8; ++bi) {
			crc = (crc << 1) ^ ((sobit & crc) ? poly : 0U);
		}
	}
	return crc;
}

/** Utility to pull out 16-bit unsigned chunks with checksum variation.
 *
 * @param[inout] sp pointer into the I2C response buffer at the start
 * of a 16-bit value.  On exit sp is updated to the next half-word to
 * extract, if @p error was false on input and was not set by the
 * call.
 *
 * @param[inout] error set when the extraction identifies a checksum error.
 *
 * @return zero if @p error is set on input or output, otherwise the
 * extracted half-word. */
u16_t extract_hword (const u8_t **spp,
		     bool *error)
{
	const u8_t *sp = *spp;
	u16_t rv = 0;

	if (!*error) {
		if (sp[2] != crc(sp, 2)) {
			*error = true;
		} else {
			rv = (sp[0] << 8) | sp[1];
			*spp += 3;
		}
	}

	return rv;
}

static int reset(const struct device *dev)
{
	const struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd = 0x06;

	return i2c_write(data->bus, &cmd, sizeof(cmd), cfg->addr);
}

int sdp8xx_device_info (struct device *dev,
			u32_t *product,
			u64_t *serial)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t buf[18];
	u8_t *dp = buf;

	*dp++ = CMD_READ_PI1 >> 8;
	*dp++ = CMD_READ_PI1 & 0xFF;

	int rc = i2c_write(data->bus, buf, dp - buf, cfg->addr);

	if (rc < 0) {
		return rc;
	}

	dp = buf;
	*dp++ = CMD_READ_PI2 >> 8;
	*dp++ = CMD_READ_PI2 & 0xFF;
	rc = i2c_write_read(data->bus, cfg->addr, buf, dp - buf, buf, sizeof(buf));

	if (rc >= 0) {
		const u8_t *sp = buf;
		bool error = false;
		*product = (u32_t)extract_hword(&sp, &error) << 16;
		*product |= (u32_t)extract_hword(&sp, &error);
		*serial = (u64_t)extract_hword(&sp, &error) << 48;
		*serial |= (u64_t)extract_hword(&sp, &error) << 32;
		*serial |= (u64_t)extract_hword(&sp, &error) << 16;
		*serial |= (u64_t)extract_hword(&sp, &error);
	}
	return rc;
}

static int trigger(const struct device *dev,
		   bool humidity)
{
	const struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t buf[2];
	u8_t *dp = buf;

	*dp++ = CMD_TRIG_DP_POLL >> 8;
	*dp++ = CMD_TRIG_DP_POLL & 0xFF;
	return i2c_write(data->bus, buf, dp - buf, cfg->addr);
}

static int fetch_result(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t buf[9];
	const size_t count = (0 == data->scale_factor) ? 9 : 6;
	int rc = i2c_read(data->bus, buf, count, cfg->addr);

	/* @todo detect ANACK? */
	if (rc < 0) {
		return rc;
	}

	const u8_t* sp = buf;
	bool error = false;

	u16_t dp_raw = extract_hword(&sp, &error);
	u16_t temp_raw = extract_hword(&sp, &error);

	LOG_DBG("raw %u %u", dp_raw, temp_raw);
	if ((0 == data->scale_factor) && (!error)) {
		data->scale_factor = extract_hword(&sp, &error);
		if (0 == data->scale_factor) {
			return -EIO;
		}
		LOG_INF("scale factor %u", data->scale_factor);
	}

	if (error) {
		rc = -EIO;
	} else if (0 != data->scale_factor) {
		struct sdp8xx_observation *op = &data->obs;

		op->temperature_cCel = 1 + (s16_t)temp_raw / 2;
		op->diffpres_cPa = ((data->scale_factor / 2)
				    + 100 * (s16_t)dp_raw)
			/ data->scale_factor;
		rc = 1;
	} else {
		/* Potential transient failure decoding scale
		 * factor.
		 */
		rc = 0;
	}

	return rc;
}

static int post_reset(struct device *dev)
{
	u32_t product;
	u64_t serial;
	int rc = sdp8xx_device_info(dev, &product, &serial);

	LOG_INF("SDP8xx %x s/n %08x%08x: %d", product, (u32_t)(serial >> 32), (u32_t)serial, rc);

	return rc;
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
		sensple_impl_animate(sp, K_NO_WAIT);
		rv = 0;
	}
	return rv;
}

static struct k_poll_signal *access(struct device *dev)
{
	LOG_INF("SDP8XX access %p %p", dev->driver_api, &driver_api);
	if (sensple_impl_match(dev, &driver_api)) {
		struct driver_data *sp = dev->driver_data;
		return &sp->sensple.signal;
	}

	return NULL;
}

static void work_handler(struct k_work *work)
{
	struct sensple_state *sp = sensple_state_from_work(work);
	struct device *dev = sp->dev;
	struct driver_data *data = dev->driver_data;

	k_spinlock_key_t key = k_spin_lock(&sp->lock);

	sp->state &= ~SENSPLE_STATE_DELAYED;
	k_spin_unlock(&sp->lock, key);

	u32_t flags = 0;
	bool raise = false;
	int rc;

	k_sem_take(&sp->sem, K_FOREVER);
	LOG_DBG("state %u", sp->machine_state);
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
				sensple_impl_animate(sp, K_MSEC(SAMPLE_DELAY_ms));
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
			sensple_impl_animate(sp, K_NO_WAIT);
		}
		break;
	case MST_WAITING_SAMPLE:
		rc = fetch_result(dev);
		LOG_DBG("fetch_result: %d", rc);

		if (rc < 0) {
			sp->machine_state = MST_FAILED;
		} else if (rc > 0) {
			sp->machine_state = MST_IDLE;
			flags |= SENSPLE_STATE_OBSERVATION_READY;
		} else {
			sp->machine_state = MST_IDLE;
		}
		break;
	}
	if (MST_FAILED == sp->machine_state) {
		flags = SENSPLE_STATE_FAILED;
	}
	if (flags) {
		k_spinlock_key_t key = k_spin_lock(&sp->lock);

		sp->state |= flags;
		k_spin_unlock(&sp->lock, key);
		raise = true;
	}

	k_sem_give(&sp->sem);
	if (raise) {
		LOG_DBG("work raise");
		k_poll_signal_raise(&sp->signal, 0);
	}
}

static int work(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	struct sensple_state *sp = &data->sensple;

	if (!sensple_impl_match(dev, &driver_api)) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&sp->lock);

	int rv = sp->state;

	k_spin_unlock(&sp->lock, key);

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
	struct sdp8xx_observation *op = ptr;

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

	LOG_INF("SDP8XX device init");

	sensple_impl_setup(sp, dev, work_handler);

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
		sensple_impl_animate(sp, K_MSEC(RESET_DELAY_ms));
	}

out:
	k_sem_give(&sp->sem);

	return rc;
};

static const struct driver_config config_0 = {
	.bus_name = DT_INST_BUS_LABEL(0),
	.addr = DT_INST_REG_ADDR(0),
};

static struct driver_data data_0;

DEVICE_AND_API_INIT(sdp8xx, DT_INST_LABEL(0),
		    device_init, &data_0, &config_0,
		    POST_KERNEL, CONFIG_SENSPLE_INIT_PRIORITY,
		    &driver_api);
