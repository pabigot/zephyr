/*
 * Copyright (c) 2019 Piotr Mienkowski
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @addtogroup t_gpio_api
 * @{
 * @defgroup t_gpio_api_config test_gpio_api_config
 * @brief TestPurpose: verify the gpio config functions using single pin
 *        configured as input/output.
 * @}
 */

#include <limits.h>
#include <sys/util.h>
#include "test_gpio_api.h"

static void pin_get_raw_and_verify(struct device *port, unsigned int pin,
				   int val_expected, int idx)
{
	int val_actual;

	val_actual = gpio_pin_get_raw(port, pin);
	zassert_true(val_actual >= 0,
		     "Test point %d: failed to get pin value", idx);
	zassert_equal(val_expected, val_actual,
		      "Test point %d: invalid pin get value", idx);
}

static void pin_set_raw_and_verify(struct device *port, unsigned int pin,
				   int val, int idx)
{
	zassert_equal(gpio_pin_set_raw(port, pin, val), 0,
		      "Test point %d: failed to set pin value", idx);
	k_busy_wait(TEST_GPIO_MAX_RISE_FALL_TIME_US);
}

/** @brief Verify gpio_pin_configure function in push pull mode.
 *
 * - Configure pin in in/out mode, verify that gpio_pin_set_raw /
 *   gpio_pin_get_raw operations change pin state.
 * - Verify that GPIO_OUTPUT_HIGH flag is initializing the pin to high.
 * - Verify that GPIO_OUTPUT_LOW flag is initializing the pin to low.
 * - Verify that configuring the pin as an output without initializing it
 *   to high or low does not change pin state.
 * - Verify that it is not possible to change value of a pin via
 *   gpio_pin_set_raw function if pin is configured as an input.
 */
void test_gpio_pin_configure_push_pull(void)
{
	struct device *port;
	int test_point = 0;
	int ret;

	port = device_get_binding(TEST_DEV);
	zassert_not_null(port, "device " TEST_DEV " not found");

	TC_PRINT("Running test on port=%s, pin=%d\n", TEST_DEV, TEST_PIN);

	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT);
	zassert_equal(ret, 0, "Failed to configure the pin as an output");

	pin_set_raw_and_verify(port, TEST_PIN, 1, test_point++);
	pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);

	/* Configure pin in in/out mode, verify that gpio_pin_set_raw /
	 * gpio_pin_get_raw operations change pin state.
	 */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT | GPIO_INPUT);
	zassert_equal(ret, 0, "Failed to configure the pin in in/out mode");

	pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);
	pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);
	pin_set_raw_and_verify(port, TEST_PIN, 1, test_point++);
	pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);
	pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);
	pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

	/* Verify that GPIO_OUTPUT_HIGH flag is initializing the pin to high. */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_HIGH | GPIO_INPUT);
	zassert_equal(ret, 0,
		      "Failed to configure the pin in in/out mode and "
		      "initialize it to high");

	pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);

	/* Verify that configuring the pin as an output without initializing it
	 * to high or low does not change pin state.
	 */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT | GPIO_INPUT);
	zassert_equal(ret, 0, "Failed to configure the pin in in/out mode");

	pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);

	/* Verify that GPIO_OUTPUT_LOW flag is initializing the pin to low. */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_LOW | GPIO_INPUT);
	zassert_equal(ret, 0,
		      "Failed to configure the pin in in/out mode and "
		      "initialize it to low");

	pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

	/* Verify that configuring the pin as an output without initializing it
	 * to high or low does not change pin state.
	 */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT | GPIO_INPUT);
	zassert_equal(ret, 0, "Failed to configure the pin in in/out mode");

	pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

	/* Verify that it is not possible to change value of a pin via
	 * gpio_pin_set_raw function if pin is configured as an input.
	 */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_INPUT);
	zassert_equal(ret, 0, "Failed to configure the pin as an input");

	int pin_in_val;

	pin_in_val = gpio_pin_get_raw(port, TEST_PIN);
	zassert_true(pin_in_val >= 0,
		     "Test point %d: failed to get pin value", test_point++);

	pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);
	pin_get_raw_and_verify(port, TEST_PIN, pin_in_val, test_point++);
	pin_set_raw_and_verify(port, TEST_PIN, 1, test_point++);
	pin_get_raw_and_verify(port, TEST_PIN, pin_in_val, test_point++);
}

/** @brief Verify gpio_pin_configure function in single ended mode.
 *
 * - Verify that pin configured in Open Drain mode and initialized
 *   to high results in pin high value if the same pin configured
 *   as input is high. Drivers that do not support Open Drain flag
 *   return ENOTSUP.
 * - Setting pin configured in Open Drain mode to low results in
 *   pin low value if the same pin configured as input is high.
 * - Verify that pin configured in Open Source mode and
 *   initialized to low results in pin high value if the same pin
 *   configured as input is high. Drivers that do not support Open
 *   Source flag return ENOTSUP.
 * - Verify that pin configured in Open Source mode and
 *   initialized to low results in pin low value if the same pin
 *   configured as input is low. Drivers that do not support Open
 *   Source flag return ENOTSUP.
 * - Setting pin configured in Open Source mode to high results in
 *   pin high value if the same pin configured as input is low.
 * - Verify that pin configured in Open Drain mode and
 *   initialized to high results in pin low value if the same pin
 *   configured as input is low. Drivers that do not support Open
 *   Drain flag return ENOTSUP.
 */
void test_gpio_pin_configure_single_ended(void)
{
	struct device *port;
	int test_point = 0;
	int pin_in_val;
	int ret;

	port = device_get_binding(TEST_DEV);
	zassert_not_null(port, "device " TEST_DEV " not found");

	TC_PRINT("Running test on port=%s, pin=%d\n", TEST_DEV, TEST_PIN);

	/* Configure pin as an input and check input level */
	ret = gpio_pin_configure(port, TEST_PIN, GPIO_INPUT);
	zassert_equal(ret, 0, "Failed to configure pin as an input");

	pin_in_val = gpio_pin_get_raw(port, TEST_PIN);
	zassert_true(pin_in_val >= 0,
		     "Test point %d: failed to get pin value", test_point++);

	if (pin_in_val == 1) {
		TC_PRINT("Test pin value when configured as input is high\n");
		/* Verify that pin configured in Open Drain mode and initialized
		 * to high results in pin high value if the same pin configured
		 * as input is high. Drivers that do not support Open Drain flag
		 * return ENOTSUP.
		 */
		ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_HIGH |
					 GPIO_OPEN_DRAIN | GPIO_INPUT);
		if (ret == -ENOTSUP) {
			TC_PRINT("Open Drain configuration not supported\n");
			return;
		}
		zassert_equal(ret, 0,
			      "Failed to configure the pin in open drain mode");

		pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);

		/* Setting pin configured in Open Drain mode to low results in
		 * pin low value if the same pin configured as input is high.
		 */
		pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);
		pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

		/* Verify that pin configured in Open Source mode and
		 * initialized to low results in pin high value if the same pin
		 * configured as input is high. Drivers that do not support Open
		 * Source flag return ENOTSUP.
		 */
		ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_LOW |
					 GPIO_OPEN_SOURCE | GPIO_INPUT);
		if (ret == -ENOTSUP) {
			TC_PRINT("Open Source configuration not supported\n");
			return;
		}
		zassert_equal(ret, 0,
			      "Failed to configure the pin in open source mode");

		pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);

		pin_set_raw_and_verify(port, TEST_PIN, 0, test_point++);
		pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);
	} else {
		TC_PRINT("Test pin value when configured as input is low\n");
		/* Verify that pin configured in Open Source mode and
		 * initialized to low results in pin low value if the same pin
		 * configured as input is low. Drivers that do not support Open
		 * Source flag return ENOTSUP.
		 */
		ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_LOW |
					 GPIO_OPEN_SOURCE | GPIO_INPUT);
		if (ret == -ENOTSUP) {
			TC_PRINT("Open Source configuration not supported\n");
			return;
		}
		zassert_equal(ret, 0,
			      "Failed to configure the pin in open source mode");

		pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

		/* Setting pin configured in Open Source mode to high results in
		 * pin high value if the same pin configured as input is low.
		 */
		pin_set_raw_and_verify(port, TEST_PIN, 1, test_point++);
		pin_get_raw_and_verify(port, TEST_PIN, 1, test_point++);

		/* Verify that pin configured in Open Drain mode and
		 * initialized to high results in pin low value if the same pin
		 * configured as input is low. Drivers that do not support Open
		 * Drain flag return ENOTSUP.
		 */
		ret = gpio_pin_configure(port, TEST_PIN, GPIO_OUTPUT_HIGH |
					 GPIO_OPEN_DRAIN | GPIO_INPUT);
		if (ret == -ENOTSUP) {
			TC_PRINT("Open Drain configuration not supported\n");
			return;
		}
		zassert_equal(ret, 0,
			      "Failed to configure the pin in open drain mode");

		pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);

		pin_set_raw_and_verify(port, TEST_PIN, 1, test_point++);
		pin_get_raw_and_verify(port, TEST_PIN, 0, test_point++);
	}
}
