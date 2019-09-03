/*
 * Copyright (c) 2019 Piotr Mienkowski
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TEST_GPIO_API_H_
#define TEST_GPIO_API_H_

#include <zephyr.h>
#include <drivers/gpio.h>
#include <ztest.h>

#if defined(DT_ALIAS_LED0_GPIOS_CONTROLLER)
#define TEST_DEV             DT_ALIAS_LED0_GPIOS_CONTROLLER
#define TEST_PIN             DT_ALIAS_LED0_GPIOS_PIN
#define TEST_PIN_DTS_FLAGS   DT_ALIAS_LED0_GPIOS_FLAGS
#else
#error Unsupported board
#endif

#define TEST_GPIO_MAX_RISE_FALL_TIME_US    200

void test_gpio_pin_configure_push_pull(void);
void test_gpio_pin_configure_single_ended(void);

void test_gpio_pin_set_get_raw(void);
void test_gpio_pin_set_get(void);
void test_gpio_pin_set_get_active_high(void);
void test_gpio_pin_set_get_active_low(void);
void test_gpio_pin_toggle(void);

void test_gpio_port_set_masked_get_raw(void);
void test_gpio_port_set_masked_get(void);
void test_gpio_port_set_masked_get_active_high(void);
void test_gpio_port_set_masked_get_active_low(void);
void test_gpio_port_set_bits_clear_bits_raw(void);
void test_gpio_port_set_bits_clear_bits(void);
void test_gpio_port_set_clr_bits_raw(void);
void test_gpio_port_set_clr_bits(void);
void test_gpio_port_toggle(void);

#endif /* TEST_GPIO_API_H_ */
