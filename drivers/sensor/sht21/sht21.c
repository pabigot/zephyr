/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "drivers/sensor/sht21.h"
#include <i2c.h>
#include <logging/log.h>

#define LOG_LEVEL CONFIG_SENSOR_LOG_LEVEL
LOG_MODULE_REGISTER(SHT21);

#define RESET_DELAY_ms 15
#define TEMPERATURE_DELAY_ms 85
#define HUMIDITY_DELAY_ms 22

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
	ST_UNINITIALIZED,
	ST_PRE_INITIALIZE,
	ST_RESETTING,
	ST_IDLE,
	ST_WAITING_TEMPERATURE,
	ST_WAITING_HUMIDITY,
};

struct driver_state {
	struct k_delayed_work work;
	struct device *dev;
	struct k_sem lock;
	struct device *bus;
	sensor2_notify_t notify;
	void *user_data;
	u32_t notify_flags;
	int state;
	u32_t timestamp;
	struct sht21_observation obs;
};

static void invalidate_obs(struct sht21_observation *op)
{
	op->humidity_pptt = -1;
}

static bool invalid_obs(const struct sht21_observation *op)
{
	return op->humidity_pptt == (u16_t)-1;
}

struct driver_config {
	const char *bus_name;
	u8_t addr;
};

typedef u8_t crc_type;
#define CRC_OK 0

static crc_type crc(const u8_t *dp,
		    size_t count)
{
	const u16_t poly = 0x131;
	const crc_type sobit = 0x80;
	crc_type crc = 0;

	while (count--) {
		crc ^= *dp++;
		for (unsigned int bi = 0; bi < 8; ++bi) {
			crc = (crc << 1) ^ ((sobit & crc) ? poly : 0U);
		}
	}
	return crc;
}

static int convert_pptt(u16_t raw)
{
	if ((SAMPLE_TYPE_Humidity << SAMPLE_TYPE_Pos)
	    != (SAMPLE_TYPE_Msk & raw)) {
		return -EINVAL;
	}
	/* RH_pph = -6 + 125 * S / 2^16 */
	raw &= ~STATUS_Msk;
	return ((12500U * raw) >> 16) - 600;
}

static int convert_cK(u16_t raw)
{
	if ((SAMPLE_TYPE_Temperature << SAMPLE_TYPE_Pos)
	    != (SAMPLE_TYPE_Msk & raw)) {
		return -EINVAL;
	}
	/* T_Cel = -46.85 + 175.72 * S / 2^16
	 * T_cK = 27315 - 4685 + 17572 * S / 2^16
	 *      = 22630 + 17572 * S / 2^16
	 */
	raw &= ~STATUS_Msk;
	return ZERO_cK - 4685 + ((17572U * raw) >> 16);
}

static void work_handler(struct k_work *work)
{
	struct driver_state *sp = CONTAINER_OF(work, struct driver_state, work);
	u32_t flags;

	k_sem_take(&sp->lock, K_FOREVER);
	flags = sp->notify_flags;
	sp->notify_flags = 0;
	k_sem_give(&sp->lock);

	if (flags) {
		sp->notify(sp->dev, flags, sp->user_data);
	}
}

static int initialize(struct device *dev,
		      sensor2_notify_t notify,
		      void *user_data)
{
	struct driver_state *sp = dev->driver_data;
	s32_t delay;
	int res = 0;

	k_sem_take(&sp->lock, K_FOREVER);

	if (sp->state != ST_PRE_INITIALIZE) {
		res = -EBUSY;
		goto out;
	}
	sp->notify = notify;
	sp->user_data = user_data;
	k_delayed_work_init(&sp->work, work_handler);

	sp->state = ST_RESETTING;
	delay = RESET_DELAY_ms - (s32_t)(k_uptime_get_32() - sp->timestamp);
	sp->notify_flags = SENSOR2_NOTIFY_WORK;
	k_delayed_work_submit(&sp->work, delay);

out:
	k_sem_give(&sp->lock);
	return res;
}

static int trigger(struct device *dev,
		   bool humidity)
{
	struct driver_state *sp = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd;

	cmd = CMDBIT_BASE | CMDBIT_NOHOLD | CMDBIT_READ | CMDBIT_NOHOLD;
	cmd |= (humidity ? CMDBIT_RH : CMDBIT_TEMP);
	return i2c_write(sp->bus, &cmd, sizeof(cmd), cfg->addr);
}

static int fetch_result(struct device *dev,
			bool humidity)
{
	struct driver_state *sp = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t data[3];
	int rc;

	rc = i2c_read(sp->bus, data, sizeof(data), cfg->addr);
	LOG_DBG("fr got %d for %p", rc, &sp->obs);
	if (!rc) {
		u16_t raw = (data[0] << 8) | data[1];

		if (crc(data, sizeof(data))) {
			rc = -EIO;
		} else if (humidity) {
			rc = convert_pptt(raw);
		} else {
			rc = convert_cK(raw);
		}
	}
	return rc;
}

static int read_ur(struct device *dev)
{
	struct driver_state *sp = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd = CMDBIT_BASE | CMDBIT_UR | CMDBIT_READ;
	u8_t ur;
	int rc;

	rc = i2c_reg_read_byte(sp->bus, cfg->addr, cmd, &ur);
	LOG_DBG("UR got %d: %02x\n", rc, ur);
	if (!rc) {
		rc = ur;
	}
	return rc;
}

static int post_reset(struct device *dev)
{
	struct driver_state *sp = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t eic[8];
	int rc;

	u8_t data[16];
	u8_t *dp = data;
	const u8_t *dpe = dp + 8;
	*dp++ = 0xFA;
	*dp++ = 0x0F;
	rc = i2c_write_read(sp->bus, data, dp - data, data, dpe - data, cfg->addr);
	LOG_DBG("EIC1 got %d", rc);
	if (rc) {
		return rc;
	}
	if (crc(data, 2)
	    || crc(data+2, 2)
	    || crc(data+4, 2)
	    || crc(data+6, 2)) {
		return -EIO;
	}
	eic[2] = data[0];
	eic[3] = data[2];
	eic[4] = data[4];
	eic[5] = data[6];

	dp = data;
	dpe = dp+6;
	*dp++ = 0xFC;
	*dp++ = 0xC9;
	rc = i2c_write_read(sp->bus, data, dp - data, data, dpe - data, cfg->addr);
	LOG_DBG("EIC2 got %d", rc);
	if (rc) {
		return rc;
	}
	if (crc(data, 3)
	    || crc(data + 3, 3)) {
		return -EIO;
	}
	eic[6] = data[0];
	eic[7] = data[1];
	eic[0] = data[3];
	eic[1] = data[4];

	LOG_DBG("EIC: %02x%02x%02x%02x%02x%02x%02x%02x",
		eic[0], eic[1], eic[2], eic[3],
		eic[4], eic[5], eic[6], eic[7]);
	return 0;
}

static int sample(struct device *dev)
{
	struct driver_state *sp = dev->driver_data;
	int res;

	k_sem_take(&sp->lock, K_FOREVER);
	if (sp->state != ST_IDLE) {
		res = -EBUSY;
		goto out;
	}
	res = trigger(dev, false);
	if (res) {
		sp->state = res;
	} else {
		invalidate_obs(&sp->obs);
		sp->state = ST_WAITING_TEMPERATURE;
		sp->timestamp = k_uptime_get_32();
		sp->notify_flags |= SENSOR2_NOTIFY_WORK;
		k_delayed_work_submit(&sp->work, TEMPERATURE_DELAY_ms);
	}

out:
	k_sem_give(&sp->lock);
	return res;
}

static int work(struct device *dev)
{
	struct driver_state *sp = dev->driver_data;
	s32_t delay;
	int res = 0;
	int ins;
	bool entry_error = false;

	k_sem_take(&sp->lock, K_FOREVER);
	ins = sp->state;
	switch (sp->state) {
	default:
		res = (sp->state < 0) ? sp->state : -EIO;
		entry_error = true;
		break;
	case ST_IDLE:
		break;
	case ST_RESETTING:
		delay = RESET_DELAY_ms - (k_uptime_get_32() - sp->timestamp);
		if (delay > 0) {
			sp->notify_flags |= SENSOR2_NOTIFY_WORK;
			k_delayed_work_submit(&sp->work, delay);
			break;
		}

		// @todo configure resolution? nah, stick with default
		// 14-bit temperature, 12-bit humidity.
 		res = post_reset(dev);
		if (0 == res) {
			sp->state = ST_IDLE;
		}
		break;
	case ST_WAITING_TEMPERATURE:
		delay = TEMPERATURE_DELAY_ms - (k_uptime_get_32() - sp->timestamp);
		if (delay > 0) {
			sp->notify_flags |= SENSOR2_NOTIFY_WORK;
			k_delayed_work_submit(&sp->work, delay);
			break;
		}
		res = fetch_result(dev, false);
		if (res >= 0) {
			sp->obs.temperature_cCel = res - ZERO_cK;
			res = trigger(dev, true);
		}
		if (res < 0) {
			sp->state = res;
		} else {
			sp->state = ST_WAITING_HUMIDITY;
			sp->timestamp = k_uptime_get_32();
			sp->notify_flags |= SENSOR2_NOTIFY_WORK;
			k_delayed_work_submit(&sp->work, HUMIDITY_DELAY_ms);
			res = 0;
		}
		break;
	case ST_WAITING_HUMIDITY:
		delay = HUMIDITY_DELAY_ms - (k_uptime_get_32() - sp->timestamp);
		if (delay > 0) {
			sp->notify_flags |= SENSOR2_NOTIFY_WORK;
			k_delayed_work_submit(&sp->work, delay);
			break;
		}
		res = fetch_result(dev, true);
		if (res < 0) {
			sp->state = res;
		} else {
			sp->obs.humidity_pptt = res;
			sp->notify_flags |= SENSOR2_NOTIFY_OBSERVATION;
			sp->state = ST_IDLE;
			k_delayed_work_submit(&sp->work, 0);
			res = 0;
		}
		break;
	}

	if ((res > 0) && !entry_error) {
		sp->state = res;
		sp->notify_flags |= SENSOR2_NOTIFY_WORK;
		k_delayed_work_submit(&sp->work, 0);
	}
	k_sem_give(&sp->lock);
	return res;
}

static int fetch(struct device *dev,
		 void *obs,
		 size_t size)
{
	struct driver_state *sp = dev->driver_data;
	struct sht21_observation *op = obs;

	LOG_DBG("fetch %u at %p, %d from %p", size, op, sp->state, &sp->obs);
	if (!op) {
		return -EINVAL;
	}
	if (sizeof(*op) != size) {
		return -EINVAL;
	}

	k_sem_take(&sp->lock, K_FOREVER);
	*op = sp->obs;
	LOG_DBG("obs %d %u", op->temperature_cCel, op->humidity_pptt);
	k_sem_give(&sp->lock);

	return invalid_obs(op) ? -ENOENT : 0;
}

static const struct sensor2_driver_api driver_api = {
	.initialize = initialize,
	.work = work,
	.sample = sample,
	.fetch = fetch,
};

static int device_init(struct device *dev)
{
	struct driver_state *sp = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	struct device *bus = device_get_binding(cfg->bus_name);
	u8_t cmd = CMD_SOFT_RESET;
	int rc;

	if (!bus) {
		rc = -EINVAL;
		goto out;
	}
	sp->dev = dev;
	sp->bus = bus;
	invalidate_obs(&sp->obs);

	rc = i2c_write(bus, &cmd, sizeof(cmd), cfg->addr);
	if (rc) {
		LOG_DBG("Reset %02x got %d\n", cfg->addr, rc);
		sp->state = rc;
	} else {
		sp->state = ST_PRE_INITIALIZE;
		sp->timestamp = k_uptime_get_32();
		LOG_DBG("Resetting from %u", sp->timestamp);
	}

out:
	k_sem_give(&sp->lock);
	return rc;
};

#ifdef DT_SENSIRION_SHT21_SHT21_0_LABEL

static const struct driver_config config_0 = {
	.bus_name = DT_SENSIRION_SHT21_SHT21_0_BUS_NAME,
	.addr = DT_SENSIRION_SHT21_SHT21_0_BASE_ADDRESS,
};

static struct driver_state state_0 = {
	.lock = _K_SEM_INITIALIZER(state_0.lock, 1, 1),
};

DEVICE_AND_API_INIT(sht21, DT_SENSIRION_SHT21_SHT21_0_LABEL,
		    device_init, &state_0, &config_0,
		    POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
		    &driver_api);

#endif
