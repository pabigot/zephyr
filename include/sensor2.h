/*
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file sensor2.h
 *
 * @brief Public APIs for the sensor driver.
 */

#ifndef ZEPHYR_INCLUDE_SENSOR2_H_
#define ZEPHYR_INCLUDE_SENSOR2_H_

/**
 * @brief Sensor2 Interface
 * @defgroup sensor_interface Sensor2 Interface
 * @ingroup io_interfaces
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/types.h>
#include <device.h>

/** @brief Notification that a sensor2 driver needs processing done.
 *
 * If this bit is set in a @p flags parameter to #sensor2_notify_t
 * callback the sensor2_work() function should be invoked for the
 * device. */
#define SENSOR2_NOTIFY_WORK BIT(0)

/** @brief Notification that a sensor2 driver has a new observation.
 *
 * If this bit is set in a @p flags parameter to a #sensor2_notify_t
 * callback invoking sensor2_fetch() will return a new observation. */
#define SENSOR2_NOTIFY_OBSERVATION BIT(1)

/** @brief Base flag for device-specific notifications.
 *
 * Drivers that support device-specific notifications may use this
 * flag, and others produced by shifting it left up to the included
 * limit of @c SENSOR2_NOTIFY_DEVICE_MAX. */
#define SENSOR2_NOTIFY_DEVICE_BASE BIT(8)

/** @brief Maximum flag reserved for device-specific notifications.
 *
 * See @c SENSOR2_NOTIFY_DEVICE_BASE. */
#define SENSOR2_NOTIFY_DEVICE_MAX BIT(20)

/**
 * @brief Callback type used to notify application of activity.
 *
 * This callback is invoked within sensor2_work() when work produces a
 * result or an error.
 *
 * @param dev Pointer to the sensor device.
 *
 * @param flags Bits consistent with #SENSOR2_NOTIFY_WORK and related
 * flags to indicate processing requirements and results related to @p
 * dev.
 *
 * @param user_data The value passed to sensor2_initialize.
 */
typedef void (*sensor2_notify_t)(struct device *dev,
				 u32_t flags,
				 void *user_data);

typedef int (*sensor2_initialize_t)(struct device *dev,
				    sensor2_notify_t notify,
				    void *user_data);

typedef int (*sensor2_work_t)(struct device *dev);

typedef int (*sensor2_sample_t)(struct device *dev);

typedef int (*sensor2_fetch_t)(struct device *dev,
			       void *obs,
			       size_t size);

struct sensor2_driver_api {
	sensor2_initialize_t initialize;
	sensor2_work_t work;
	sensor2_sample_t sample;
	sensor2_fetch_t fetch;
};

/**
 * @brief Initialize a sensor driver.
 *
 * @param dev Pointer to the sensor device.
 *
 * @param handler A handler to be installed so the driver may invoke
 * `k_work_submit*()` to indicate that `sensor2_work()` must be
 * invoked to allow the driver to make progress.
 *
 * @param notify A callback that may be invoked from sensor2_work() to
 * notify the application that new results or errors have been
 * identified.
 *
 * @param user_data Optional user-defined value passed to `notify`.
 *
 * @return Zero on successful initialization. */
__syscall int sensor2_initialize(struct device *dev,
				 sensor2_notify_t notify,
				 void *user_data);

static inline int _impl_sensor2_initialize(struct device *dev,
					   sensor2_notify_t notify,
					   void *user_data)
{
	const struct sensor2_driver_api *api = dev->driver_api;

	if (!notify) {
		return -EINVAL;
	}
	return api->initialize(dev, notify, user_data);
}

__syscall int sensor2_work(struct device *dev);

static inline int _impl_sensor2_work(struct device *dev)
{
	const struct sensor2_driver_api *api = dev->driver_api;

	return api->work(dev);
}

__syscall int sensor2_sample(struct device *dev);

static inline int _impl_sensor2_sample(struct device *dev)
{
	const struct sensor2_driver_api *api = dev->driver_api;
	sensor2_sample_t impl = api->sample;

	return impl ? impl(dev) : -ENOTSUP;
}

__syscall int sensor2_fetch(struct device *dev,
			    void *obs,
			    size_t size);

static inline int _impl_sensor2_fetch(struct device *dev,
				      void *obs,
				      size_t size)
{
	const struct sensor2_driver_api *api = dev->driver_api;

	return api->fetch(dev, obs, size);
}

#include <syscalls/sensor2.h>

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_SENSOR2_H_ */
