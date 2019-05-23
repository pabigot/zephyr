/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT sensirion_sht21

#include <drivers/i2c.h>
#include <drivers/sensple/sht21.h>
#include <drivers/sensple/impl.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(SHT21, CONFIG_SENSPLE_LOG_LEVEL);

struct driver_data {
	struct sensple_state sensple;
	struct sht21_observation obs;
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
	MST_UNINITIALIZED,
	MST_RESETTING,
	MST_IDLE,
	MST_WAITING_TEMPERATURE,
	MST_WAITING_HUMIDITY,
	MST_FAILED,
};

static void invalidate_obs(struct sht21_observation *op)
{
	op->humidity_pptt = -1;
}

static bool invalid_obs(const struct sht21_observation *op)
{
	return op->humidity_pptt == (u16_t)-1;
}

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

static int trigger(const struct device *dev,
		   bool humidity)
{
	const struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd;

	cmd = CMDBIT_BASE | CMDBIT_NOHOLD | CMDBIT_READ | CMDBIT_NOHOLD;
	cmd |= (humidity ? CMDBIT_RH : CMDBIT_TEMP);
	return i2c_write(data->bus, &cmd, sizeof(cmd), cfg->addr);
}

static int fetch_result(struct device *dev,
			bool humidity)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t buf[3];
	int rc;

	rc = i2c_read(data->bus, buf, sizeof(buf), cfg->addr);
	LOG_DBG("fr got %d for %p", rc, &data->obs);
	if (!rc) {
		u16_t raw = (buf[0] << 8) | buf[1];

		if (crc(buf, sizeof(buf))) {
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
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t cmd = CMDBIT_BASE | CMDBIT_UR | CMDBIT_READ;
	u8_t ur;
	int rc;

	rc = i2c_reg_read_byte(data->bus, cfg->addr, cmd, &ur);
	LOG_DBG("UR got %d: %02x\n", rc, ur);
	if (!rc) {
		rc = ur;
	}
	return rc;
}

static int post_reset(struct device *dev)
{
	struct driver_data *data = dev->driver_data;
	const struct driver_config *cfg = dev->config->config_info;
	u8_t eic[8];
	int rc;

	u8_t buf[16];

	u8_t *bp = buf;
	const u8_t *bpe = bp + 8;
	*bp++ = 0xFA;
	*bp++ = 0x0F;
	rc = i2c_write_read(data->bus, cfg->addr, buf, bp - buf, buf, bpe - buf);
	LOG_DBG("EIC1 got %d", rc);
	if (rc) {
		return rc;
	}
	if (false && (crc(buf, 2)
	    || crc(buf+2, 2)
	    || crc(buf+4, 2)
		      || crc(buf+6, 2))) {
		return -EIO;
	}
	eic[2] = buf[0];
	eic[3] = buf[2];
	eic[4] = buf[4];
	eic[5] = buf[6];

	bp = buf;
	bpe = bp+6;
	*bp++ = 0xFC;
	*bp++ = 0xC9;
	rc = i2c_write_read(data->bus, cfg->addr, buf, bp - buf, buf, bpe - buf);
	LOG_DBG("EIC2 got %d", rc);
	if (rc) {
		return rc;
	}
	if (false && (crc(buf, 3)
		      || crc(buf + 3, 3))) {
		return -EIO;
	}
	eic[6] = buf[0];
	eic[7] = buf[1];
	eic[0] = buf[3];
	eic[1] = buf[4];

	LOG_DBG("EIC: %02x%02x%02x%02x%02x%02x%02x%02x",
		eic[0], eic[1], eic[2], eic[3],
		eic[4], eic[5], eic[6], eic[7]);
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
		data->sample= true;
		k_sem_give(&sp->sem);
		sensple_impl_animate(sp, K_NO_WAIT);

		rv = 0;
	}
	return rv;
}

static struct k_poll_signal *access(struct device *dev)
{
	LOG_INF("SHT21 access %p %p", dev->driver_api, &driver_api);
	if (sensple_impl_match(dev, &driver_api)) {
		struct driver_data *sp = dev->driver_data;
		return &sp->sensple.signal;
	}
	return 0;
}

static void work_handler(struct k_work *work)
{
	struct sensple_state *sp = sensple_state_from_work(work);
	struct device *dev = sp->dev;
	struct driver_data *data = dev->driver_data;
	int rc = 0;

	//printk("START\n");

	k_spinlock_key_t key = k_spin_lock(&sp->lock);

	sp->state &= ~SENSPLE_STATE_DELAYED;
	k_spin_unlock(&sp->lock, key);

	u32_t flags = 0;

	rc = k_sem_take(&sp->sem, K_NO_WAIT);
	if (rc == -EBUSY) {
		sensple_impl_animate(sp, K_MSEC(1));
		return;
	}
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
			if (rc) {
				LOG_ERR("temp trigger failed: %d", rc);
				sp->machine_state = MST_FAILED;
			} else {
				invalidate_obs(&data->obs);
				sp->machine_state = MST_WAITING_TEMPERATURE;
				sensple_impl_animate(sp, K_MSEC(TEMPERATURE_DELAY_ms));
			}
		}
		break;
	case MST_RESETTING:
		// @todo configure resolution? nah, stick with default
		// 14-bit temperature, 12-bit humidity.
 		rc = post_reset(dev);
		if (0 == rc) {
			sp->machine_state = MST_IDLE;
			sensple_impl_animate(sp, K_NO_WAIT);
		}
		break;
	case MST_WAITING_TEMPERATURE:
		rc = fetch_result(dev, false);
		if (rc >= 0) {
			data->obs.temperature_cCel = rc - ZERO_cK;
			rc = trigger(dev, true);
		}
		if (rc < 0) {
			sp->machine_state = MST_FAILED;
			LOG_ERR("humidity failed: %d", rc);
		} else {
			sp->machine_state = MST_WAITING_HUMIDITY;
			sensple_impl_animate(sp, K_MSEC(HUMIDITY_DELAY_ms));
		}
		break;
	case MST_WAITING_HUMIDITY:
		rc = fetch_result(dev, true);
		if (rc < 0) {
			sp->machine_state = MST_FAILED;
		} else {
			data->obs.humidity_pptt = rc;
			LOG_DBG("OBS: %d cCel, %u pptt", data->obs.temperature_cCel, data->obs.humidity_pptt);
			sp->machine_state = MST_IDLE;
			flags |= SENSPLE_STATE_OBSERVATION_READY;
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
		LOG_DBG("signal");
		k_poll_signal_raise(&sp->signal, 0);
	}

	k_sem_give(&sp->sem);
	LOG_DBG("work exit");
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

static int fetch(struct device *dev,
		 void *ptr,
		 size_t len,
		 unsigned int id)
{
	struct driver_data *data = dev->driver_data;

	if (!sensple_impl_match(dev, &driver_api)) {
		return -EINVAL;
	}
	struct sht21_observation *op = ptr;

	if ((id != 0)
	    || (!op)
	    || (len != sizeof(*op))) {
		return -EINVAL;
	}

	struct sensple_state *sp = &data->sensple;

	k_sem_take(&sp->sem, K_FOREVER);
	*op = data->obs;
	k_sem_give(&sp->sem);
	int rv = invalid_obs(op) ? -ENOENT : 0;
	LOG_DBG("return %d %u: %d", op->temperature_cCel, op->humidity_pptt, rv);

	return rv;
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
	int rc = 0;

	LOG_INF("SHT21 device init");

	sensple_impl_setup(sp, dev, work_handler);

	struct device *bus = device_get_binding(cfg->bus_name);

	if (!bus) {
		LOG_DBG("No bus: %s\n", cfg->bus_name);
		rc = -EINVAL;
		goto out;
	}
	data->dev = dev;
	data->bus = bus;
	invalidate_obs(&data->obs);

	u8_t cmd = CMD_SOFT_RESET;
	rc = i2c_write(bus, &cmd, sizeof(cmd), cfg->addr);

	LOG_DBG("reset %02x got %d", cfg->addr, rc);
	if (rc) {
		sp->machine_state = MST_FAILED;
	} else {
		sp->machine_state = MST_RESETTING;
		sensple_impl_animate(sp, K_MSEC(15));
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

DEVICE_AND_API_INIT(sht21, DT_INST_LABEL(0),
		    device_init, &data_0, &config_0,
		    POST_KERNEL, CONFIG_SENSPLE_INIT_PRIORITY,
		    &driver_api);
