/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/gpio.h>
#include <syscall_handler.h>

Z_SYSCALL_HANDLER(gpio_config, port, access_op, pin, flags)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, config));
	return z_impl_gpio_config((struct device *)port, access_op, pin, flags);
}

Z_SYSCALL_HANDLER(gpio_write, port, access_op, pin, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, write));
	return z_impl_gpio_write((struct device *)port, access_op, pin, value);
}

Z_SYSCALL_HANDLER(gpio_read, port, access_op, pin, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, read));
	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(value, sizeof(u32_t)));
	return z_impl_gpio_read((struct device *)port, access_op, pin,
			       (u32_t *)value);
}

Z_SYSCALL_HANDLER(gpio_port_get_raw, port, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, port_get_raw));
	return z_impl_gpio_port_get_raw((struct device *)port,
					(gpio_port_value_t *)value);
}

Z_SYSCALL_HANDLER(gpio_port_set_masked_raw, port, mask, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, port_set_masked_raw));
	return z_impl_gpio_port_set_masked_raw((struct device *)port, mask,
						value);
}

Z_SYSCALL_HANDLER(gpio_port_set_bits_raw, port, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, port_set_bits_raw));
	return z_impl_gpio_port_set_bits_raw((struct device *)port, value);
}

Z_SYSCALL_HANDLER(gpio_port_clear_bits_raw, port, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, port_clear_bits_raw));
	return z_impl_gpio_port_clear_bits_raw((struct device *)port, value);
}

Z_SYSCALL_HANDLER(gpio_port_toggle_bits, port, value)
{
	Z_OOPS(Z_SYSCALL_DRIVER_GPIO(port, port_toggle_bits));
	return z_impl_gpio_port_toggle_bits((struct device *)port, value);
}

Z_SYSCALL_HANDLER(gpio_enable_callback, port, access_op, pin)
{
	return z_impl_gpio_enable_callback((struct device *)port, access_op,
					  pin);
}

Z_SYSCALL_HANDLER(gpio_disable_callback, port, access_op, pin)
{
	return z_impl_gpio_disable_callback((struct device *)port, access_op,
					   pin);
}

Z_SYSCALL_HANDLER(gpio_get_pending_int, port)
{
	return z_impl_gpio_get_pending_int((struct device *)port);
}
