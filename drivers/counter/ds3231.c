/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>
#include <drivers/rtc/ds3231.h>
#include <gpio.h>
#include <i2c.h>
#include <kernel.h>
#include <logging/log.h>
#include <sys/timeutil.h>
#include <sys/util.h>

LOG_MODULE_REGISTER(DS3231, CONFIG_COUNTER_LOG_LEVEL);

#define REG_MONCEN_CENTURY 0x80
#define REG_HOURS_12H 0x40
#define REG_HOURS_PM 0x20
#define REG_HOURS_20 0x20
#define REG_HOURS_10 0x20
#define REG_DAYDATE_DOW 0x40
#define REG_ALARM_IGN 0x80

enum {
	SYNCSM_IDLE,
	SYNCSM_PREP_READ,
	SYNCSM_FINISH_READ,
	SYNCSM_PREP_WRITE,
	SYNCSM_FINISH_WRITE,
};

struct register_map {
	u8_t sec;
	u8_t min;
	u8_t hour;
	u8_t dow;
	u8_t dom;
	u8_t moncen;
	u8_t year;

	struct {
		u8_t sec;
		u8_t min;
		u8_t hour;
		u8_t date;
	} __packed alarm1;

	struct {
		u8_t min;
		u8_t hour;
		u8_t date;
	} __packed alarm2;

	u8_t ctrl;
	u8_t ctrl_stat;
	u8_t aging;
	s8_t temp_units;
	u8_t temp_frac256;
};

struct gpios {
	const char *ctrl;
	u32_t pin;
};

struct ds3231_config {
	/* Common structure first because generic API expects this here. */
	struct counter_config_info generic;
	const char *bus_name;
	struct gpios isw_gpios;
	u16_t addr;
};

struct ds3231_data {
	struct device *ds3231;
	struct device *i2c;
	struct device *isw;
	struct register_map registers;

	/* Timer structure used for synchronization */
	struct k_timer sync_timer;

	/* Work structures for the various cases of ISW interrupt. */
	struct k_work alarm_work;
	struct k_work sqw_work;
	struct k_work sync_work;

	/* Forward ISW interrupt to proper worker. */
	struct gpio_callback isw_callback;

	/* syncclock captured in the last ISW interrupt handler */
	u32_t isw_syncclock;

	struct rtc_ds3231_syncpoint syncpoint;
	struct rtc_ds3231_syncpoint new_sp;

	time_t rtc_registers;
	time_t rtc_base;
	u32_t syncclock_base;

	/* Signal to be raised when a synchronize or set operation
	 * completes.  Null when nobody's waiting for such an
	 * operation.
	 */
	struct k_poll_signal *sync_signal;

	/* Handlers and state when using the counter alarm API. */
	counter_alarm_callback_t counter_handler[2];
	u32_t counter_ticks[2];

	/* Handlers and state for DS3231 alarm API. */
	rtc_ds3231_alarm_callback_handler_t alarm_handler[2];
	void *alarm_user_data[2];
	u8_t alarm_flags[2];

	/* Flags recording requests for ISW monitoring. */
	u8_t isw_mon_req;
#define ISW_MON_REQ_Alarm 0x01
#define ISW_MON_REQ_Sync 0x02

	/* Status of synchronization operations. */
	u8_t sync_state;
};

/*
 * Set and clear specific bits in the control register.
 *
 * This function assumes the device register cache is valid and will
 * update the device only if the value changes as a result of applying
 * the set and clear changes.
 *
 * Caches and returns the value with the changes applied.
 */
static int sc_ctrl(struct device *dev,
		   u8_t set,
		   u8_t clear)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	struct register_map *rp = &data->registers;
	u8_t ctrl = (rp->ctrl & ~clear) | set;
	int rc = ctrl;

	if (rp->ctrl != ctrl) {
		u8_t buf[2] = {
			offsetof(struct register_map, ctrl),
			ctrl,
		};
		rc = i2c_write(data->i2c, buf, sizeof(buf), cfg->addr);
		if (rc >= 0) {
			rp->ctrl = ctrl;
			rc = ctrl;
		}
	}
	return rc;
}

/*
 * Read the ctrl_stat register then set and clear bits in it.
 *
 * OSF, A1F, and A2F will be written with 1s if the corresponding bits
 * do not appear in either set or clear.  This ensures that if any
 * flag becomes set between the read and the write that indicator will
 * not be cleared.
 *
 * Returns the value as originally read (disregarding the effect of
 * clears and sets).
 */
static int rsc_stat(struct device *dev,
		    u8_t set,
		    u8_t clear)
{
	u8_t const ign = RTC_DS3231_REG_STAT_OSF | RTC_DS3231_ALARM1 | RTC_DS3231_ALARM2;
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	struct register_map *rp = &data->registers;
	u8_t addr = offsetof(struct register_map, ctrl_stat);
	int rc;

	rc = i2c_write_read(data->i2c, cfg->addr,
			    &addr, sizeof(addr),
			    &rp->ctrl_stat, sizeof(rp->ctrl_stat));
	if (rc >= 0) {
		u8_t stat = rp->ctrl_stat & ~clear;

		if (rp->ctrl_stat != stat) {
			u8_t buf[2] = {
				addr,
				stat | (ign & ~(set | clear)),
			};
			rc = i2c_write(data->i2c, buf, sizeof(buf), cfg->addr);
		}
		if (rc >= 0) {
			rc = rp->ctrl_stat;
		}
	}
	return rc;
}

/*
 * Look for current users of the interrupt/square-wave signal and
 * enable monitoring iff at least one consumer is active.
 */
static void validate_isw_monitoring(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	const struct register_map *rp = &data->registers;
	u8_t isw_mon_req = 0;

	if (rp->ctrl & (RTC_DS3231_ALARM1 | RTC_DS3231_ALARM2)) {
		isw_mon_req |= ISW_MON_REQ_Alarm;
	}
	if (data->sync_state != SYNCSM_IDLE) {
		isw_mon_req |= ISW_MON_REQ_Sync;
	}
	LOG_DBG("ISW %p : %d ?= %d", data->isw, isw_mon_req, data->isw_mon_req);
	if ((data->isw != NULL)
	    && (isw_mon_req != data->isw_mon_req)) {
		int rc = 0;

		/* Disable before reconfigure */
		rc = gpio_pin_disable_callback(data->isw, cfg->isw_gpios.pin);

		if ((rc >= 0)
		    && ((isw_mon_req & ISW_MON_REQ_Sync)
			!= (data->isw_mon_req & ISW_MON_REQ_Sync))) {
			if (isw_mon_req & ISW_MON_REQ_Sync) {
				rc = sc_ctrl(dev, 0,
					     RTC_DS3231_REG_CTRL_INTCN
					     | RTC_DS3231_REG_CTRL_RS_Msk);
			} else {
				rc = sc_ctrl(dev, RTC_DS3231_REG_CTRL_INTCN, 0);
			}
		}

		/* Enable if any requests active */
		if ((rc >= 0) && (isw_mon_req != 0)) {
			rc = gpio_pin_enable_callback(data->isw, cfg->isw_gpios.pin);
		}

		LOG_INF("ISW reconfigure to %x: %d", isw_mon_req, rc);
		data->isw_mon_req = isw_mon_req;
	}
}

static const u8_t *decode_time(struct tm *tp,
			       const u8_t *rp,
			       bool with_sec)
{
	u8_t reg;

	if (with_sec) {
		u8_t reg = *rp++;

		tp->tm_sec = 10 * ((reg >> 4) & 0x07) + (reg & 0x0F);
	}

	reg = *rp++;
	tp->tm_min = 10 * ((reg >> 4) & 0x07) + (reg & 0x0F);

	reg = *rp++;
	tp->tm_hour = (reg & 0x0F);
	if (REG_HOURS_12H & reg) {
		/* 12-hour */
		if (REG_HOURS_10 & reg) {
			tp->tm_hour += 10;
		}
		if (REG_HOURS_PM & reg) {
			tp->tm_hour += 12;
		}
	} else {
		/* 24 hour */
		tp->tm_hour += 10 * ((reg >> 4) & 0x03);
	}

	return rp;
}

static u8_t decode_alarm(const u8_t *ap,
			 bool with_sec,
			 time_t *tp)
{
	struct tm tm = {
		/* tm_year zero is 1900 with underflows a 32-bit counter
		 * representation.  Use 1978-01, the first January after the
		 * POSIX epoch where the first day of the month is the first
		 * day of the week.
		 */
		.tm_year = 78,
	};
	const u8_t *dp = decode_time(&tm, ap, with_sec);
	u8_t flags = 0;
	u8_t amf = RTC_DS3231_ALARM_FLAGS_IGNDA;

	/* Done decoding time, now decode day/date. */
	if (REG_DAYDATE_DOW & *dp) {
		flags |= RTC_DS3231_ALARM_FLAGS_DOW;

		/* Because tm.tm_wday does not contribute to the UNIX
		 * time that the civil time translates into, we need
		 * to also record the tm_mday for our selected base
		 * 1978-01 that will produce the correct tm_wday.
		 */
		tm.tm_mday = (*dp & 0x07);
		tm.tm_wday = tm.tm_mday - 1;
	} else {
		tm.tm_mday = 10 * ((*dp >> 4) & 0x3) + (*dp & 0x0F);
	}

	/* Walk backwards to extract the alarm mask flags. */
	while (true) {
		if (REG_ALARM_IGN & *dp) {
			flags |= amf;
		}
		amf >>= 1;
		if (dp-- == ap) {
			break;
		}
	}

	/* Convert to the reduced representation. */
	*tp = timeutil_timegm(&tm);
	return flags;
}

static int encode_alarm(u8_t *ap,
			bool with_sec,
			time_t time,
			u8_t flags)
{
	struct tm tm;
	u8_t val;

	(void)gmtime_r(&time, &tm);

	/* For predictable behavior the low 4 bits of flags
	 * (corresponding to AxMy) must be 0b1111, 0b1110, 0b1100,
	 * 0b1000, or 0b0000.  This corresponds to the bitwise inverse
	 * being one less than a power of two.
	 */
	if (!is_power_of_two(1U + (0x0F & ~flags))) {
		LOG_DBG("invalid alarm mask in flags: %02x", flags);
		return -EINVAL;
	}

	if (with_sec) {
		if (flags & RTC_DS3231_ALARM_FLAGS_IGNSE) {
			val = REG_ALARM_IGN;
		} else {
			val = ((tm.tm_sec / 10) << 4) | (tm.tm_sec % 10);
		}
		*ap++ = val;
	}

	if (flags & RTC_DS3231_ALARM_FLAGS_IGNMN) {
		val = REG_ALARM_IGN;
	} else {
		val = ((tm.tm_min / 10) << 4) | (tm.tm_min % 10);
	}
	*ap++ = val;

	if (flags & RTC_DS3231_ALARM_FLAGS_IGNHR) {
		val = REG_ALARM_IGN;
	} else {
		val = ((tm.tm_hour / 10) << 4) | (tm.tm_hour % 10);
	}
	*ap++ = val;

	if (flags & RTC_DS3231_ALARM_FLAGS_IGNDA) {
		val = REG_ALARM_IGN;
	} else if (flags & RTC_DS3231_ALARM_FLAGS_DOW) {
		val = REG_DAYDATE_DOW | (tm.tm_wday + 1);
	} else {
		val = ((tm.tm_mday / 10) << 4) | (tm.tm_mday % 10);
	}
	*ap++ = val;

	return 0;
}

static u32_t decode_rtc(struct ds3231_data *data)
{
	struct tm tm = { 0 };
	const struct register_map *rp = &data->registers;

	decode_time(&tm, &rp->sec, true);
	tm.tm_wday = (rp->dow & 0x07) - 1;
	tm.tm_mday = 10 * ((rp->dom >> 4) & 0x03) + (rp->dom & 0x0F);
	tm.tm_mon = 10 * (((0xF0 & ~REG_MONCEN_CENTURY) & rp->moncen) >> 4)
		    + (rp->moncen & 0x0F) - 1;
	tm.tm_year = (10 * (rp->year >> 4)) + (rp->year & 0x0F);
	if (REG_MONCEN_CENTURY & rp->moncen) {
		tm.tm_year += 100;
	}

	data->rtc_registers = timeutil_timegm(&tm);
	return data->rtc_registers;
}

static int update_registers(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	u32_t syncclock;
	int rc;
	u8_t addr = 0;

	data->syncclock_base = rtc_ds3231_read_syncclock(dev);
	rc = i2c_write_read(data->i2c, cfg->addr,
			    &addr, sizeof(addr),
			    &data->registers, sizeof(data->registers));
	syncclock = rtc_ds3231_read_syncclock(dev);
	if (rc < 0) {
		return rc;
	}
	data->rtc_base = decode_rtc(data);

	return 0;
}

static int ds3231_get_alarm(struct device *dev,
			    u8_t id,
			    struct rtc_ds3231_alarm *cp)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	u8_t addr;
	u8_t len;

	if (id == 0) {
		addr = offsetof(struct register_map, alarm1);
		len = sizeof(data->registers.alarm1);
	} else if (id < cfg->generic.channels) {
		addr = offsetof(struct register_map, alarm2);
		len = sizeof(data->registers.alarm2);
	} else {
		return -EINVAL;
	}

	/* Update alarm structure */
	u8_t *rbp = &data->registers.sec + addr;
	int rc = i2c_write_read(data->i2c, cfg->addr,
				&addr, sizeof(addr),
				rbp, len);
	if (rc < 0) {
		LOG_DBG("get_config at %02x failed: %d\n", addr, rc);
		return rc;
	}

	*cp = (struct rtc_ds3231_alarm){ 0 };
	cp->flags = decode_alarm(rbp, (id == 0), &cp->time);
	cp->handler = data->alarm_handler[id];
	cp->user_data = data->alarm_user_data[id];

	return 0;
}

static int ds3231_counter_cancel_alarm(struct device *dev,
				       u8_t id)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;

	if (id >= cfg->generic.channels) {
		return -EINVAL;
	}

	data->alarm_handler[id] = NULL;
	data->alarm_user_data[id] = NULL;

	return sc_ctrl(dev, 0, RTC_DS3231_ALARM1 << id);
}


static int ds3231_set_alarm(struct device *dev,
			    u8_t id,
			    const struct rtc_ds3231_alarm *cp)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	u8_t addr;
	u8_t len;

	if (id == 0) {
		addr = offsetof(struct register_map, alarm1);
		len = sizeof(data->registers.alarm1);
	} else if (id < cfg->generic.channels) {
		addr = offsetof(struct register_map, alarm2);
		len = sizeof(data->registers.alarm2);
	} else {
		return -EINVAL;
	}

	u8_t buf[5] = { addr };
	int rc = encode_alarm(buf + 1, (id == 0), cp->time, cp->flags);

	if (rc < 0) {
		return rc;
	}

	/* @todo resolve race condition: a previously stored alarm may
	 * trigger between clear of AxF and the write of the new alarm
	 * control.
	 */
	rc = rsc_stat(dev, 0U, (RTC_DS3231_ALARM1 << id));
	if (rc >= 0) {
		rc = i2c_write(data->i2c, buf, len + 1, cfg->addr);
	}
	if ((rc >= 0)
	    && (cp->handler != NULL)) {
		rc = sc_ctrl(dev, RTC_DS3231_ALARM1 << id, 0);
	}
	if (rc >= 0) {
		memmove(&data->registers.sec + addr, buf + 1, len);
		data->alarm_handler[id] = cp->handler;
		data->alarm_user_data[id] = cp->user_data;
		data->alarm_flags[id] = cp->flags;
		validate_isw_monitoring(dev);
	}

	return rc;
}

static int ds3231_check_alarms(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct register_map *rp = &data->registers;
	u8_t mask = (RTC_DS3231_ALARM1 | RTC_DS3231_ALARM2);

	/* Fetch and clear only the alarm flags that are not
	 * interrupt-enabled.
	 */
	return mask & rsc_stat(dev, 0U, (rp->ctrl & mask) ^ mask);
}

static int check_handled_alarms(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct register_map *rp = &data->registers;
	u8_t mask = (RTC_DS3231_ALARM1 | RTC_DS3231_ALARM2);

	/* Fetch and clear only the alarm flags that are
	 * interrupt-enabled.
	 */
	return mask & rsc_stat(dev, 0U, rp->ctrl & mask);
}

static void counter_alarm_forwarder(struct device *dev,
				    u8_t id,
				    u32_t syncclock,
				    void *ud)
{
	struct ds3231_data *data = dev->driver_data;
	counter_alarm_callback_t handler = data->counter_handler[id];
	u32_t ticks = data->counter_ticks[id];

	data->counter_handler[id] = NULL;
	data->counter_ticks[id] = 0;
	if (handler) {
		handler(dev, id, ticks, ud);
	}
}

static void alarm_worker(struct k_work *work)
{
	struct ds3231_data *data = CONTAINER_OF(work, struct ds3231_data, alarm_work);
	struct device *ds3231 = data->ds3231;
	const struct ds3231_config *cfg = ds3231->config->config_info;

	int af = check_handled_alarms(ds3231);

	if (af < 0) {
		LOG_ERR("failed to read alarm flags");
		return;
	}

	u8_t id;
	bool validate_isw = false;

	for (id = 0; id < cfg->generic.channels; ++id) {
		if ((af & (RTC_DS3231_ALARM1 << id)) == 0) {
			continue;
		}

		rtc_ds3231_alarm_callback_handler_t handler = data->alarm_handler[id];
		void *ud = data->alarm_user_data[id];

		if (data->alarm_flags[id] & RTC_DS3231_ALARM_FLAGS_AUTODISABLE) {
			int rc = ds3231_counter_cancel_alarm(ds3231, id);

			LOG_DBG("autodisable %d: %d", id, rc);
			validate_isw = true;
		}

		if (handler) {
			handler(ds3231, id, data->isw_syncclock, ud);
		}
	}
	if (validate_isw) {
		validate_isw_monitoring(ds3231);
	}

	LOG_DBG("ALARM %02x at %u latency %u", af, data->isw_syncclock, rtc_ds3231_read_syncclock(ds3231) - data->isw_syncclock);
}

static void sqw_worker(struct k_work *work)
{
	struct ds3231_data *data = CONTAINER_OF(work, struct ds3231_data, sqw_work);
	u32_t syncclock = rtc_ds3231_read_syncclock(data->ds3231);

	/* This is a placeholder for potential application-controlled
	 * use of the square wave functionality.
	 */
	LOG_DBG("SQW %u latency %u", data->isw_syncclock, syncclock - data->isw_syncclock);
}

static int ds3231_read(struct device *dev,
		       time_t *time)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	u8_t addr = 0;
	int rc = i2c_write_read(data->i2c, cfg->addr,
				&addr, sizeof(addr),
				&data->registers, 7);

	if (rc >= 0) {
		*time = decode_rtc(data);
	}

	return rc;
}

static u32_t ds3231_counter_read(struct device *dev)
{
	time_t time = 0;

	/* Counter API doesn't allow for failure to read. */
	(void)ds3231_read(dev, &time);
	return time;
}

static void sync_finish(struct device *dev,
			int rc)
{
	struct ds3231_data *data = dev->driver_data;

	data->sync_state = SYNCSM_IDLE;
	(void)validate_isw_monitoring(dev);

	LOG_DBG("sync complete, signal %d to %p\n", rc, data->sync_signal);
	if (data->sync_signal) {
		k_poll_signal_raise(data->sync_signal, rc);
		data->sync_signal = NULL;
	}
}

static void sync_prep_read(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	int rc = sc_ctrl(dev, 0U, RTC_DS3231_REG_CTRL_INTCN | RTC_DS3231_REG_CTRL_RS_Msk);

	if (rc < 0) {
		sync_finish(dev, rc);
		return;
	}
	data->sync_state = SYNCSM_FINISH_READ;
	validate_isw_monitoring(dev);
}

static void sync_finish_read(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;

	data->syncpoint.rtc.tv_sec = ds3231_counter_read(dev);
	data->syncpoint.rtc.tv_nsec = 0;
	data->syncpoint.syncclock = data->isw_syncclock;
	sync_finish(dev, 0);
}

static void sync_timer_handler(struct k_timer *tmr)
{
	struct ds3231_data *data = CONTAINER_OF(tmr, struct ds3231_data, sync_timer);

	LOG_INF("sync_timer fired");
	k_work_submit(&data->sync_work);
}

static void sync_prep_write(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	u32_t syncclock = rtc_ds3231_read_syncclock(dev);
	u32_t offset = syncclock - data->new_sp.syncclock;
	u32_t syncclock_Hz = rtc_ds3231_syncclock_frequency(dev);
	u32_t offset_s = offset / syncclock_Hz;
	u32_t offset_ms = (offset % syncclock_Hz) * 1000U / syncclock_Hz;
	time_t when = data->new_sp.rtc.tv_sec;

	when += offset_s;
	offset_ms += data->new_sp.rtc.tv_nsec / NSEC_PER_USEC / USEC_PER_MSEC;
	if (offset_ms >= MSEC_PER_SEC) {
		offset_ms -= MSEC_PER_SEC;
	} else {
		when += 1;
	}

	u32_t rem_ms = MSEC_PER_SEC - offset_ms;

	if (rem_ms < 5) {
		when += 1;
		rem_ms += MSEC_PER_SEC;
	}
	data->new_sp.rtc.tv_sec = when;
	data->new_sp.rtc.tv_nsec = 0;

	data->sync_state = SYNCSM_FINISH_WRITE;
	k_timer_start(&data->sync_timer, K_MSEC(rem_ms), 0);
	LOG_INF("sync %u in %u ms after %u", (u32_t)when, rem_ms, syncclock);
}

static void sync_finish_write(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	time_t when = data->new_sp.rtc.tv_sec;
	struct tm tm;
	u8_t buf[8];
	u8_t *bp = buf;
	u8_t val;

	*bp++ = offsetof(struct register_map, sec);

	(void)gmtime_r(&when, &tm);
	val = ((tm.tm_sec / 10) << 4) | (tm.tm_sec % 10);
	*bp++ = val;

	val = ((tm.tm_min / 10) << 4) | (tm.tm_min % 10);
	*bp++ = val;

	val = ((tm.tm_hour / 10) << 4) | (tm.tm_hour % 10);
	*bp++ = val;

	*bp++ = 1 + tm.tm_wday;

	val = ((tm.tm_mday / 10) << 4) | (tm.tm_mday % 10);
	*bp++ = val;

	tm.tm_mon += 1;
	val = ((tm.tm_mon / 10) << 4) | (tm.tm_mon % 10);
	if (tm.tm_year >= 100) {
		tm.tm_year -= 100;
		val |= REG_MONCEN_CENTURY;
	}
	*bp++ = val;

	val = ((tm.tm_year / 10) << 4) | (tm.tm_year % 10);
	*bp++ = val;

	u32_t syncclock = rtc_ds3231_read_syncclock(dev);
	int rc = i2c_write(data->i2c, buf, bp - buf, cfg->addr);

	if (rc >= 0) {
		data->syncpoint.rtc.tv_sec = when;
		data->syncpoint.rtc.tv_nsec = 0;
		data->syncpoint.syncclock = syncclock;
		LOG_INF("sync %u at %u", (u32_t)when, syncclock);
	}
	sync_finish(dev, rc);
}

static void sync_worker(struct k_work *work)
{
	struct ds3231_data *data = CONTAINER_OF(work, struct ds3231_data, sync_work);
	u32_t syncclock = rtc_ds3231_read_syncclock(data->ds3231);

	LOG_DBG("SYNC.%u %u latency %u", data->sync_state, data->isw_syncclock, syncclock - data->isw_syncclock);
	switch (data->sync_state) {
	default:
	case SYNCSM_IDLE:
		break;
	case SYNCSM_PREP_READ:
		sync_prep_read(data->ds3231);
		break;
	case SYNCSM_FINISH_READ:
		sync_finish_read(data->ds3231);
		break;
	case SYNCSM_PREP_WRITE:
		sync_prep_write(data->ds3231);
		break;
	case SYNCSM_FINISH_WRITE:
		sync_finish_write(data->ds3231);
		break;
	}
}

static void isw_gpio_callback(struct device *port,
			      struct gpio_callback *cb,
			      u32_t pins)
{
	struct ds3231_data *data = CONTAINER_OF(cb, struct ds3231_data, isw_callback);

	data->isw_syncclock = rtc_ds3231_read_syncclock(data->ds3231);
	if (data->registers.ctrl & RTC_DS3231_REG_CTRL_INTCN) {
		k_work_submit(&data->alarm_work);
	} else if (data->sync_state != SYNCSM_IDLE) {
		k_work_submit(&data->sync_work);
	} else {
		k_work_submit(&data->sqw_work);
	}
}

static int ds3231_get_sync(struct device *dev,
			   struct rtc_ds3231_syncpoint *syncpoint)
{
	struct ds3231_data *data = dev->driver_data;

	if (data->syncpoint.rtc.tv_sec == 0) {
		return -ENOENT;
	}
	*syncpoint = data->syncpoint;
	return 0;
}

static int ds3231_synchronize(struct device *dev,
			      struct k_poll_signal *signal)
{
	struct ds3231_data *data = dev->driver_data;

	if (signal == NULL) {
		return -EINVAL;
	}
	if (data->isw == NULL) {
		return -ENOTSUP;
	}
	if (data->sync_state != SYNCSM_IDLE) {
		return -EBUSY;
	}

	data->sync_signal = signal;
	data->sync_state = SYNCSM_PREP_READ;
	k_work_submit(&data->sync_work);

	return 0;
}

static int ds3231_set(struct device *dev,
		      const struct rtc_ds3231_syncpoint *syncpoint,
		      struct k_poll_signal *signal)
{
	struct ds3231_data *data = dev->driver_data;

	if ((syncpoint == NULL)
	    || (signal == NULL)) {
		return -EINVAL;
	}
	if (data->isw == NULL) {
		return -ENOTSUP;
	}
	if (data->sync_state != SYNCSM_IDLE) {
		return -EBUSY;
	}

	data->new_sp = *syncpoint;
	data->sync_signal = signal;
	data->sync_state = SYNCSM_PREP_WRITE;
	k_work_submit(&data->sync_work);

	return 0;
}


static int ds3231_init(struct device *dev)
{
	struct ds3231_data *data = dev->driver_data;
	const struct ds3231_config *cfg = dev->config->config_info;
	struct device *i2c = device_get_binding(cfg->bus_name);
	int rc;

	data->ds3231 = dev;
	if (i2c == NULL) {
		LOG_WRN("Failed to get I2C %s", cfg->bus_name);
		return -EINVAL;
	}

	data->i2c = i2c;
	rc = update_registers(dev);
	if (rc < 0) {
		LOG_WRN("Failed to fetch registers: %d", rc);
		return rc;
	}

	/* INTCN and AxIE to power-up default, RS to 1 Hz */
	rc = sc_ctrl(dev,
		     RTC_DS3231_REG_CTRL_INTCN,
		     RTC_DS3231_REG_CTRL_RS_Msk | RTC_DS3231_ALARM1 | RTC_DS3231_ALARM2);
	if (rc < 0) {
		LOG_WRN("Failed to reset config: %d", rc);
		return rc;
	}

	if (cfg->isw_gpios.ctrl != NULL) {
		struct device *gpio = device_get_binding(cfg->isw_gpios.ctrl);

		if (gpio == NULL) {
			LOG_WRN("Failed to get INTn/SQW GPIO %s", cfg->isw_gpios.ctrl);
			return -EINVAL;
		}

		k_timer_init(&data->sync_timer, sync_timer_handler, NULL);
		k_work_init(&data->alarm_work, alarm_worker);
		k_work_init(&data->sqw_work, sqw_worker);
		k_work_init(&data->sync_work, sync_worker);
		gpio_init_callback(&data->isw_callback,
				   isw_gpio_callback,
				   BIT(cfg->isw_gpios.pin));

		rc = gpio_pin_configure(gpio, cfg->isw_gpios.pin,
					GPIO_DIR_IN
					| GPIO_PUD_PULL_UP
					| GPIO_INT
					| GPIO_INT_EDGE
					| GPIO_INT_ACTIVE_LOW
					| GPIO_INT_DEBOUNCE);
		if (rc >= 0) {
			rc = gpio_add_callback(gpio, &data->isw_callback);
		}
		if (rc >= 0) {
			data->isw = gpio;
		} else {
			LOG_WRN("Failed to configure ISW callback: %d", rc);
		}
	}

	LOG_DBG("Initialized");
	return 0;
}

static int ds3231_counter_start(struct device *dev)
{
	return -EALREADY;
}

static int ds3231_counter_stop(struct device *dev)
{
	return -ENOTSUP;
}

int ds3231_counter_set_alarm(struct device *dev,
			     u8_t id,
			     const struct counter_alarm_cfg *alarm_cfg)
{
	struct ds3231_data *data = dev->driver_data;
	const struct register_map *rp = &data->registers;
	time_t when;
	int rc = 0;

	if (rp->ctrl & (RTC_DS3231_ALARM1 << id)) {
		return -EBUSY;
	}

	if ((alarm_cfg->flags & COUNTER_ALARM_CFG_ABSOLUTE) == 0) {
		rc = ds3231_read(dev, &when);
		if (rc >= 0) {
			when += alarm_cfg->ticks;
		}
	} else {
		when = alarm_cfg->ticks;
	}

	struct rtc_ds3231_alarm alarm = {
		.time = (u32_t)when,
		.handler = counter_alarm_forwarder,
		.user_data = alarm_cfg->user_data,
		.flags = RTC_DS3231_ALARM_FLAGS_AUTODISABLE,
	};

	if (rc >= 0) {
		data->counter_handler[id] = alarm_cfg->callback;
		data->counter_ticks[id] = alarm.time;
		rc = ds3231_set_alarm(dev, id, &alarm);
	}
	return rc;
}

static u32_t ds3231_counter_get_top_value(struct device *dev)
{
	return UINT32_MAX;
}

static u32_t ds3231_counter_get_pending_int(struct device *dev)
{
	return 0;
}

static int ds3231_counter_set_top_value(struct device *dev,
					const struct counter_top_cfg *cfg)
{
	return -ENOTSUP;
}

static u32_t ds3231_counter_get_max_relative_alarm(struct device *dev)
{
	return UINT32_MAX;
}

static const struct rtc_ds3231_driver_api ds3231_api = {
	.counter_api = {
		.start = ds3231_counter_start,
		.stop = ds3231_counter_stop,
		.read = ds3231_counter_read,
		.set_alarm = ds3231_counter_set_alarm,
		.cancel_alarm = ds3231_counter_cancel_alarm,
		.set_top_value = ds3231_counter_set_top_value,
		.get_pending_int = ds3231_counter_get_pending_int,
		.get_top_value = ds3231_counter_get_top_value,
		.get_max_relative_alarm = ds3231_counter_get_max_relative_alarm,
	},
	.ctrl_update = sc_ctrl,
	.stat_update = rsc_stat,
	.get_alarm = ds3231_get_alarm,
	.set_alarm = ds3231_set_alarm,
	.synchronize = ds3231_synchronize,
	.get_sync = ds3231_get_sync,
	.set = ds3231_set,
	.check_alarms = ds3231_check_alarms,
};

static const struct ds3231_config ds3231_0_config = {
	.generic = {
		.max_top_value = UINT32_MAX,
		.freq = 1,
		.flags = COUNTER_CONFIG_INFO_COUNT_UP,
		.channels = 2,
	},
	.bus_name = DT_INST_0_MAXIM_DS3231_BUS_NAME,
	/* Driver does not currently use 32k GPIO. */
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
