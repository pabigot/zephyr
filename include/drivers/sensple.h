/**
 * @file sensple.h
 *
 * @brief Public APIs for simplified sensor drivers.
 *
 * The simplified sensor API design is guided by these principles:

 * * Sensors have state machines that are implemented in the driver
 *   and driven by external events.
 * * The application is told that that a sensor's state machine needs
 *   to be processed through a `k_poll_signal`.
 * * The application will be aware of the type of sensor and its
 *   capabilities so generic measurement "channels" and value
 *   representations are inappropriate (mostly).
 */

/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_SENSPLE_H_
#define ZEPHYR_INCLUDE_SENSPLE_H_

/**
 * @brief Simplified Sensor Interface
 * @defgroup senspl_interface Simplified Sensor Interface
 * @ingroup io_interfaces
 * @{
 */

#include <zephyr/types.h>
#include <device.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Bit set in sensple_state::state when the sensor state machine is
 * blocked by a delay. */
#define SENSPLE_STATE_DELAYED BIT(0)

#define SENSPLE_STATE_OBSERVATION_READY BIT(1)
#define SENSPLE_STATE_FAILED BIT(2)

/** State associated with every sensple driver.
 *
 * The Zephyr driver infrastructure provides no standard solution for
 * subsystem common state, so an instance of this has to be maintained
 * in every driver's device.driver_data structure.
 */
struct sensple_state {
	/** Common state. */
	u32_t state;

	/** Driver-specific state. */
	u32_t machine_state;

	/** Pointer to the device for which this instance is somewhere
	 * in the driver_data structure. */
	struct device *dev;

	/** Atomic access to state, and machine_state if needed. */
	struct k_spinlock lock;

	/** Queued access to driver. */
	struct k_sem sem;

	/** Signal used to notify application of ready results. */
	struct k_poll_signal signal;

	/** Work item used to animate sensor activities. */
	struct k_delayed_work delayed_work;
};

/* Table of standard driver API functions.
 *
 * Note: All functions should use sensple_impl_match() to verify that
 * the passed device is correct for the driver. */
struct sensple_driver_api {
	struct k_poll_signal *(*access)(struct device *dev);
	int (*work)(struct device *dev);
	int (*sample)(struct device *dev, unsigned int id);
	int (*fetch)(struct device *dev,
		     void *dp,
		     size_t len,
		     unsigned int id);
};

/** @brief Get the poll signal used to receive device notifications.
 *
 * @param dev Pointer to the sensple device.
 *
 * @return a pointer to the signal that is raised when sensple_work()
 * needs to be invoked for the device.
 *
 * @return Zero on successful initialization. */
__syscall struct k_poll_signal *sensple_access(struct device *dev);

static inline struct k_poll_signal *z_impl_sensple_access(struct device *dev)
{
	const struct sensple_driver_api *api = dev->driver_api;

	return api->access(dev);
}

/** @brief Animate the driver state machine.
 *
 * @param dev Pointer to the sensple device.
 *
 * @return a negative error code, or a driver-specific non-negative
 * value, often a bit set of device events. */
__syscall int sensple_work(struct device *dev);

static inline int z_impl_sensple_work(struct device *dev)
{
	const struct sensple_driver_api *api = dev->driver_api;

	return api->work(dev);
}

/** @brief Request a sample from the sensor.
 *
 * @param dev Pointer to the sensple device.
 *
 * @param id Driver-specific identifier for what is to be sampled.
 *
 * @return a negative error code, or a driver-specific non-negative
 * value, often a bit set of device events. */
__syscall int sensple_sample(struct device *dev,
			     unsigned int id);

static inline int z_impl_sensple_sample(struct device *dev,
					unsigned int id)
{
	const struct sensple_driver_api *api = dev->driver_api;

	if (api->sample) {
		return api->sample(dev, id);
	}
	return -ENOTSUP;
}

/** @brief Fetch data from the sensple device
 *
 * @note In most cases this function should not be invoked directly,
 * but rather through device-specific wrappers that perform data
 * structure casts and size checks.
 *
 * @param dev Pointer to the sensple device.
 *
 * @param dp Pointer to where returned data should be written.
 *
 * @param len Size available at @p dp for returned data, in octets.
 *
 * @param id Driver-specific identifier for the data that is to be
 * returned.

 * @return a negative error code, or a driver-specific non-negative
 * value, often a bit set of device events. */
__syscall int sensple_fetch(struct device *dev,
			    void *dp,
			    size_t len,
			    unsigned int id);

static inline int z_impl_sensple_fetch(struct device *dev,
				       void *dp,
				       size_t len,
				       unsigned int id)
{
	const struct sensple_driver_api *api = dev->driver_api;

	return api->fetch(dev, dp, len, id);
}

#include <syscalls/sensple.h>

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_SENSPLE_H_ */
