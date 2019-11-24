/*
 * Copyright (c) 2016-2019 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>
#include "nrf_clock_calibration.h"
#include <logging/log.h>
#include <hal/nrf_power.h>

LOG_MODULE_REGISTER(clock_control, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

/* Helper logging macros which prepends device name to the log. */
#define CLOCK_LOG(lvl, dev, ...) \
	LOG_##lvl("%s: " GET_ARG1(__VA_ARGS__), dev->config->name \
			COND_CODE_0(NUM_VA_ARGS_LESS_1(__VA_ARGS__),\
					(), (, GET_ARGS_LESS_1(__VA_ARGS__))))
#define ERR(dev, ...) CLOCK_LOG(ERR, dev, __VA_ARGS__)
#define WRN(dev, ...) CLOCK_LOG(WRN, dev, __VA_ARGS__)
#define INF(dev, ...) CLOCK_LOG(INF, dev, __VA_ARGS__)
#define DBG(dev, ...) CLOCK_LOG(DBG, dev, __VA_ARGS__)

/* returns true if clock stopping or starting can be performed. If false then
 * start/stop will be deferred and performed later on by handler owner.
 */
typedef bool (*nrf_clock_handler_t)(struct device *dev);

/* Clock instance structure */
struct nrf_clock_control {
	/* Lock protecting ref and list contents */
	struct k_spinlock lock;

	sys_slist_t list;	/* List of users requesting callback */
	u8_t ref;		/* Users counter */
	bool started;		/* Indicated that clock is started */
};

/* Clock instance static configuration */
struct nrf_clock_control_config {
	nrf_clock_handler_t start_handler; /* Called before start */
	nrf_clock_handler_t stop_handler; /* Called before stop */
	nrf_clock_event_t started_evt;	/* Clock started event */
	nrf_clock_task_t start_tsk;	/* Clock start task */
	nrf_clock_task_t stop_tsk;	/* Clock stop task */
};

static void clkstarted_handle(struct device *dev);

/* Return true if given event has enabled interrupt and is triggered. Event
 * is cleared.
 */
static bool clock_event_check_and_clean(nrf_clock_event_t evt, u32_t intmask)
{
	bool ret = nrf_clock_event_check(NRF_CLOCK, evt) &&
			nrf_clock_int_enable_check(NRF_CLOCK, intmask);

	if (ret) {
		nrf_clock_event_clear(NRF_CLOCK, evt);
	}

	return ret;
}

static enum clock_control_status get_status(struct device *dev,
					    clock_control_subsys_t sys)
{
	struct nrf_clock_control *data = dev->driver_data;
	enum clock_control_status rv = CLOCK_CONTROL_STATUS_OFF;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (data->started) {
		rv = CLOCK_CONTROL_STATUS_ON;
	} else if (data->ref > 0) {
		rv = CLOCK_CONTROL_STATUS_STARTING;
	}

	k_spin_unlock(&data->lock, key);

	return rv;
}

static int clock_stop(struct device *dev, clock_control_subsys_t sub_system)
{
	const struct nrf_clock_control_config *config =
						dev->config->config_info;
	struct nrf_clock_control *data = dev->driver_data;
	int err = 0;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (data->ref == 0) {
		err = -EALREADY;
		goto out;
	}
	data->ref--;

	if (data->ref == 0) {
		bool do_stop = true;

		DBG(dev, "Stopping");
		if (config->stop_handler) {
			do_stop = config->stop_handler(dev);
		}

		if (do_stop) {
			nrf_clock_task_trigger(NRF_CLOCK, config->stop_tsk);
			/* It may happen that clock is being stopped when it
			 * has just been started and start is not yet handled
			 * (due to irq_lock). In that case after stopping the
			 * clock, started event is cleared to prevent false
			 * interrupt being triggered.
			 */
			nrf_clock_event_clear(NRF_CLOCK, config->started_evt);
		}

		/* TBD: Is this the right reaction if the stop_handler
		 * returned false?
		 */
		data->started = false;
	}

out:
	k_spin_unlock(&data->lock, key);

	return err;
}

static int clock_async_start(struct device *dev,
			     clock_control_subsys_t sub_system,
			     struct clock_control_async_data *data)
{
	const struct nrf_clock_control_config *config =
						dev->config->config_info;
	struct nrf_clock_control *clk_data = dev->driver_data;
	k_spinlock_key_t key;
	int ret = 0;

	__ASSERT_NO_MSG((data == NULL) ||
			((data != NULL) && (data->cb != NULL)));

	key = k_spin_lock(&clk_data->lock);

	bool first_request = (clk_data->ref == 0);
	bool do_restart = false;
	bool already_started = clk_data->started;

	++clk_data->ref;
	__ASSERT_NO_MSG(clk_data->ref > 0);

	if (first_request) {
		bool do_start = true;

		if (config->start_handler) {
			do_start = config->start_handler(dev);
		}

		if (do_start) {
			DBG(dev, "Triggering start task");
			nrf_clock_task_trigger(NRF_CLOCK,
					       config->start_tsk);
		} else {
			/* If external start_handler indicated that clcok is
			 * still running (it may happen in case of LF RC clock
			 * which was requested to be stopped during ongoing
			 * calibration (clock will not be stopped in that case)
			 * and requested to be started before calibration is
			 * completed. In that case clock is still running and
			 * we can notify enlisted requests.
			 */
			do_restart = true;
		}
	}

	/* Handle callback registration if necessary */
	if (data) {
		struct clock_control_async_data *user;

		/* Reject registration from already-registered user */
		SYS_SLIST_FOR_EACH_CONTAINER(&clk_data->list, user, node) {
			if (user == data) {
				ret = -EBUSY;
				goto out;
			}
		}

		/* Register callback if not already started */
		if (!already_started) {
			sys_slist_append(&clk_data->list, &data->node);
		}
	}


out:
	/* Failed requests do not count as references */
	if (ret != 0) {
		--clk_data->ref;
	}

	k_spin_unlock(&clk_data->lock, key);

	if (ret == 0) {
		if (do_restart) {
			clkstarted_handle(dev);
		}
		if (already_started && (data != NULL)) {
			data->cb(dev, data->user_data);
		}
	}

	return ret;
}

static int clock_start(struct device *dev, clock_control_subsys_t sub_system)
{
	return clock_async_start(dev, sub_system, NULL);
}

/* Note: this function has public linkage, and MUST have this
 * particular name.  The platform architecture itself doesn't care,
 * but there is a test (tests/kernel/arm_irq_vector_table) that needs
 * to find it to it can set it in a custom vector table.  Should
 * probably better abstract that at some point (e.g. query and reset
 * it by pointer at runtime, maybe?) so we don't have this leaky
 * symbol.
 */
void nrf_power_clock_isr(void *arg);

static int hfclk_init(struct device *dev)
{
	IRQ_CONNECT(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0,
		    DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0_PRIORITY,
		    nrf_power_clock_isr, 0, 0);

	irq_enable(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0);

	nrf_clock_lf_src_set(NRF_CLOCK, CLOCK_CONTROL_NRF_K32SRC);

	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
		z_nrf_clock_calibration_init(dev);
	}

	nrf_clock_int_enable(NRF_CLOCK,
			(NRF_CLOCK_INT_HF_STARTED_MASK |
			 NRF_CLOCK_INT_LF_STARTED_MASK |
			 COND_CODE_1(CONFIG_USB_NRFX,
				(NRF_POWER_INT_USBDETECTED_MASK |
				 NRF_POWER_INT_USBREMOVED_MASK |
				 NRF_POWER_INT_USBPWRRDY_MASK),
				(0))));

	return 0;
}

static int lfclk_init(struct device *dev)
{
	return 0;
}

static const struct clock_control_driver_api clock_control_api = {
	.on = clock_start,
	.off = clock_stop,
	.async_on = clock_async_start,
	.get_status = get_status,
};

static struct nrf_clock_control hfclk;
static const struct nrf_clock_control_config hfclk_config = {
	.start_tsk = NRF_CLOCK_TASK_HFCLKSTART,
	.started_evt = NRF_CLOCK_EVENT_HFCLKSTARTED,
	.stop_tsk = NRF_CLOCK_TASK_HFCLKSTOP
};

DEVICE_AND_API_INIT(clock_nrf5_m16src,
		    DT_INST_0_NORDIC_NRF_CLOCK_LABEL  "_16M",
		    hfclk_init, &hfclk, &hfclk_config, PRE_KERNEL_1,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &clock_control_api);


static struct nrf_clock_control lfclk;
static const struct nrf_clock_control_config lfclk_config = {
	.start_tsk = NRF_CLOCK_TASK_LFCLKSTART,
	.started_evt = NRF_CLOCK_EVENT_LFCLKSTARTED,
	.stop_tsk = NRF_CLOCK_TASK_LFCLKSTOP,
	.start_handler =
		IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION) ?
			z_nrf_clock_calibration_start : NULL,
	.stop_handler =
		IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION) ?
			z_nrf_clock_calibration_stop : NULL
};

DEVICE_AND_API_INIT(clock_nrf5_k32src,
		    DT_INST_0_NORDIC_NRF_CLOCK_LABEL  "_32K",
		    lfclk_init, &lfclk, &lfclk_config, PRE_KERNEL_1,
		    CONFIG_KERNEL_INIT_PRIORITY_DEVICE,
		    &clock_control_api);

static void clkstarted_handle(struct device *dev)
{
	struct nrf_clock_control *data = dev->driver_data;

	DBG(dev, "Clock started");

	/* Under lock set the started flag and swap out the list of
	 * callbacks */
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	sys_slist_t notify = data->list;

	data->started = true;
	sys_slist_init(&data->list);

	k_spin_unlock(&data->lock, key);

	/* Notify all registered users the start event */
	sys_snode_t *node = sys_slist_get(&notify);
	while (node != NULL) {
		struct clock_control_async_data *async_data =
			CONTAINER_OF(node, struct clock_control_async_data, node);

		async_data->cb(dev, async_data->user_data);
	}
}

#if defined(CONFIG_USB_NRFX)
static bool power_event_check_and_clean(nrf_power_event_t evt, u32_t intmask)
{
	bool ret = nrf_power_event_check(NRF_POWER, evt) &&
			nrf_power_int_enable_check(NRF_POWER, intmask);

	if (ret) {
		nrf_power_event_clear(NRF_POWER, evt);
	}

	return ret;
}
#endif

static void usb_power_isr(void)
{
#if defined(CONFIG_USB_NRFX)
	extern void usb_dc_nrfx_power_event_callback(nrf_power_event_t event);

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBDETECTED,
					NRF_POWER_INT_USBDETECTED_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBDETECTED);
	}

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBPWRRDY,
					NRF_POWER_INT_USBPWRRDY_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBPWRRDY);
	}

	if (power_event_check_and_clean(NRF_POWER_EVENT_USBREMOVED,
					NRF_POWER_INT_USBREMOVED_MASK)) {
		usb_dc_nrfx_power_event_callback(NRF_POWER_EVENT_USBREMOVED);
	}
#endif
}

void nrf_power_clock_isr(void *arg)
{
	ARG_UNUSED(arg);

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_HFCLKSTARTED,
					NRF_CLOCK_INT_HF_STARTED_MASK)) {
		struct device *hfclk_dev = DEVICE_GET(clock_nrf5_m16src);
		struct nrf_clock_control *data = hfclk_dev->driver_data;

		/* Check needed due to anomaly 201:
		 * HFCLKSTARTED may be generated twice.
		 */
		if (!data->started) {
			clkstarted_handle(hfclk_dev);
		}
	}

	if (clock_event_check_and_clean(NRF_CLOCK_EVENT_LFCLKSTARTED,
					NRF_CLOCK_INT_LF_STARTED_MASK)) {
		struct device *lfclk_dev = DEVICE_GET(clock_nrf5_k32src);

		if (IS_ENABLED(
			CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
			z_nrf_clock_calibration_lfclk_started(lfclk_dev);
		}
		clkstarted_handle(lfclk_dev);
	}

	usb_power_isr();

	if (IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC_CALIBRATION)) {
		z_nrf_clock_calibration_isr();
	}
}

#ifdef CONFIG_USB_NRFX
void nrf5_power_usb_power_int_enable(bool enable)
{
	u32_t mask;

	mask = NRF_POWER_INT_USBDETECTED_MASK |
	       NRF_POWER_INT_USBREMOVED_MASK |
	       NRF_POWER_INT_USBPWRRDY_MASK;

	if (enable) {
		nrf_power_int_enable(NRF_POWER, mask);
		irq_enable(DT_INST_0_NORDIC_NRF_CLOCK_IRQ_0);
	} else {
		nrf_power_int_disable(NRF_POWER, mask);
	}
}
#endif
