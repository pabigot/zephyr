/*
 * Copyright (c) 2018 Linaro Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <syscall_handler.h>
#include <drivers/led.h>

static inline int z_vrfy_led_blink(const struct device *dev, uint32_t led,
				   uint32_t delay_on, uint32_t delay_off)
{
	Z_OOPS(Z_SYSCALL_DRIVER_LED(dev, blink));
	return z_impl_led_blink((const struct device *)dev, led, delay_on,
					delay_off);
}
#include <syscalls/led_blink_mrsh.c>

static inline int z_vrfy_led_set_brightness(const struct device *dev,
					    uint32_t led,
					    uint8_t value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_LED(dev, set_brightness));
	return z_impl_led_set_brightness((const struct device *)dev, led,
					 value);
}
#include <syscalls/led_set_brightness_mrsh.c>

static inline int z_vrfy_led_on(const struct device *dev, uint32_t led)
{
	Z_OOPS(Z_SYSCALL_DRIVER_LED(dev, on));
	return z_impl_led_on((const struct device *)dev, led);
}
#include <syscalls/led_on_mrsh.c>

static inline int z_vrfy_led_off(const struct device *dev, uint32_t led)
{
	Z_OOPS(Z_SYSCALL_DRIVER_LED(dev, off));
	return z_impl_led_off((const struct device *)dev, led);
}
#include <syscalls/led_off_mrsh.c>
