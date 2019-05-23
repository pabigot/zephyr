/**
 * @file impl.h
 *
 * @brief APIs used in implementing simplified sensor drivers.
 *
 */

/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_SENSPLE_IMPL_H_
#define ZEPHYR_INCLUDE_SENSPLE_IMPL_H_

/**
 * @brief Simplified Sensor Interface
 * @defgroup senspl_interface Simplified Sensor Interface
 * @ingroup io_interfaces
 * @{
 */

#include <drivers/sensple.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Common function to initialize the driver's sensple_state instance.
 *
 * This must be invoked by the driver-specific initialization
 * infrastructure to configure the shared infrastructure, including
 * semaphore, signal, and timer.
 *
 * @param sp pointer to the state instance.
 *
 * @param dev pointer to the device that owns the state instance.
 *
 * @param work_handler the function to be invoked when the sensor
 * state machine must be animated.
 */
void sensple_impl_setup(struct sensple_state *sp,
			struct device *dev,
			k_work_handler_t work_handler);

/** Verify that the passed device is associated with the driver. */
static inline bool sensple_impl_match(const struct device *dev,
				      const struct sensple_driver_api *api)
{
	return (dev->driver_api == api);
}

/** Translate the work structure to the sensple state that contains it. */
static inline struct sensple_state *sensple_state_from_work(struct k_work *work)
{
	struct k_delayed_work* delayed_work =
		CONTAINER_OF(work, struct k_delayed_work, work);
	struct sensple_state *sp =
		CONTAINER_OF(delayed_work, struct sensple_state, delayed_work);

	return sp;
}

/** Animate the sensor state machine after a delay.
 *
 * @param sp state for the sensple driver that should be woken.
 *
 * @param delay offset from current time at which driver should be
 * woken.  Use K_NO_WAIT to implement a yield.
 */
int sensple_impl_animate(struct sensple_state *sp,
			 k_timeout_t delay);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_SENSPLE_IMPL_H_ */
