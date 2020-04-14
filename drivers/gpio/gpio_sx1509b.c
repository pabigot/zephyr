/*
 * Copyright (c) 2018 Aapo Vienamo
 * Copyright (c) 2018 Peter Bigot Consulting, LLC
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2020 ZedBlox Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT semtech_sx1509b

#include <errno.h>

#include <kernel.h>
#include <device.h>
#include <init.h>
#include <drivers/gpio.h>
#include <drivers/i2c.h>
#include <sys/byteorder.h>
#include <sys/util.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(sx1509b, CONFIG_GPIO_LOG_LEVEL);

#include "gpio_utils.h"

/* Number of pins supported by the device */
#define NUM_PINS 16

/* Max to select all pins supported on the device. */
#define ALL_PINS ((u16_t)BIT_MASK(NUM_PINS))

/* Reset delay is 2.5 ms, round up for Zephyr resolution */
#define RESET_DELAY_MS 3

/** Cache of the output configuration and data of the pins. */
struct sx1509b_pin_state {
	u16_t input_disable;    /* 0x00 */
	u16_t long_slew;        /* 0x02 */
	u16_t low_drive;        /* 0x04 */
	u16_t pull_up;          /* 0x06 */
	u16_t pull_down;        /* 0x08 */
	u16_t open_drain;       /* 0x0A */
	u16_t polarity;         /* 0x0C */
	u16_t dir;              /* 0x0E */
	u16_t data;             /* 0x10 */
	u16_t interrupt;        /* 0x12 */
	u32_t interrupt_sense;  /* 0x14 */
	u16_t debounce_en;
};

/** Runtime driver data */
struct sx1509b_drv_data {
	/* gpio_driver_data needs to be first */
	struct gpio_driver_data common;
	struct device *i2c_master;
	struct device *gpio_int;
	struct sx1509b_pin_state pin_state;
	struct sx1509b_config *cfg;
	struct gpio_callback gpio_cb;
	struct k_sem lock;

	struct k_work work;
	struct device *dev;

	/* Enabled INT pins generating a cb */
	u32_t cb_pins;
	/* user ISR cb */
	sys_slist_t cb;
};

/** Configuration data */
struct sx1509b_config {
	/* gpio_driver_config needs to be first */
	struct gpio_driver_config common;
	const char *i2c_master_dev_name;
	const char *gpio_int_dev_name;
	u16_t i2c_slave_addr;
	u16_t gpio_pin;
	u16_t gpio_flags;
};

/* General configuration register addresses */
enum {
	/* TODO: Add rest of the regs */
	SX1509B_REG_CLOCK       = 0x1e,
	SX1509B_REG_RESET       = 0x7d,
};

/* Magic values for softreset */
enum {
	SX1509B_REG_RESET_MAGIC0        = 0x12,
	SX1509B_REG_RESET_MAGIC1        = 0x34,
};

/* Register bits for SX1509B_REG_CLOCK */
enum {
	SX1509B_REG_CLOCK_FOSC_OFF      = 0 << 5,
	SX1509B_REG_CLOCK_FOSC_EXT      = 1 << 5,
	SX1509B_REG_CLOCK_FOSC_INT_2MHZ = 2 << 5,
};

/* Pin configuration register addresses */
enum {
	SX1509B_REG_INPUT_DISABLE       = 0x00,
	SX1509B_REG_PULL_UP             = 0x06,
	SX1509B_REG_PULL_DOWN           = 0x08,
	SX1509B_REG_OPEN_DRAIN          = 0x0a,
	SX1509B_REG_DIR                 = 0x0e,
	SX1509B_REG_DATA                = 0x10,
	SX1509B_REG_LED_DRIVER_ENABLE   = 0x20,
	SX1509B_REG_DEBOUNCE_CONFIG     = 0x22,
	SX1509B_REG_DEBOUNCE_ENABLE     = 0x23,
};

/* Interrupt config register addresses */
enum {
	SX1509B_REG_INTERRUPT_MASK   = 0x12,
	SX1509B_REG_SENSE_B          = 0x14,
	SX1509B_REG_SENSE_A          = 0x16,
	SX1509B_REG_INTERRUPT_SOURCE = 0x18,
};

/* Edge sensitivity types */
enum {
	SX1509B_EDGE_NONE     = 0x00,
	SX1509B_EDGE_RISING   = 0x01,
	SX1509B_EDGE_FALLING  = 0x02,
	SX1509B_EDGE_BOTH     = 0x03,
};

/**
 * @brief Write a big-endian word to an internal address of an I2C slave.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param dev_addr Address of the I2C device for writing.
 * @param reg_addr Address of the internal register being written.
 * @param value Value to be written to internal register.
 *
 * @retval 0 If successful.
 * @retval -EIO General input / output error.
 */
static inline int i2c_reg_write_word_be(struct device *dev, u16_t dev_addr,
					u8_t reg_addr, u16_t value)
{
	u8_t tx_buf[3] = { reg_addr, value >> 8, value & 0xff };

	return i2c_write(dev, tx_buf, 3, dev_addr);
}

/**
 * @brief Write a big-endian byte to an internal address of an I2C slave.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param dev_addr Address of the I2C device for writing.
 * @param reg_addr Address of the internal register being written.
 * @param value Value to be written to internal register.
 *
 * @retval 0 If successful.
 * @retval -EIO General input / output error.
 */
static inline int i2c_reg_write_byte_be(struct device *dev, u16_t dev_addr,
					u8_t reg_addr, u8_t value)
{
	u8_t tx_buf[3] = { reg_addr, value };

	return i2c_write(dev, tx_buf, 2, dev_addr);
}

#ifdef CONFIG_GPIO_SX1509B_INTERRUPT
static int sx1509b_handle_interrupt(void *arg)
{
	struct device *dev = (struct device *) arg;
	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	int ret = 0;
	u16_t int_source;

	k_sem_take(&drv_data->lock, K_FOREVER);

	ret = i2c_burst_read(drv_data->i2c_master, cfg->i2c_slave_addr,
				 SX1509B_REG_INTERRUPT_SOURCE,
				 (u8_t *)&int_source,
				 sizeof(int_source));
	if (ret) {
		goto out;
	}

	int_source = sys_be16_to_cpu(int_source);

	if ((int_source & drv_data->cb_pins) != 0) {
		gpio_fire_callbacks(&drv_data->cb, dev, int_source);
	}

	/* reset interrupts */
	ret = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_INTERRUPT_SOURCE, 0xffff);
	if (ret) {
		goto out;
	}

out:
	k_sem_give(&drv_data->lock);

	return ret;
}

static void sx1509b_work_handler(struct k_work *work)
{
	struct sx1509b_drv_data *drv_data =
		CONTAINER_OF(work, struct sx1509b_drv_data, work);

	sx1509b_handle_interrupt(drv_data->dev);
}

static void sx1509_int_cb(struct device *dev, struct gpio_callback *gpio_cb,
			   u32_t pins)
{
	struct sx1509b_drv_data *drv_data = CONTAINER_OF(gpio_cb,
		struct sx1509b_drv_data, gpio_cb);

	ARG_UNUSED(pins);

	k_work_submit(&drv_data->work);
}
#endif

static int sx1509b_config(struct device *dev,
			  gpio_pin_t pin,
			  gpio_flags_t flags)
{
	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	struct sx1509b_pin_state *pins = &drv_data->pin_state;

	struct {
		u8_t reg;
		struct sx1509b_pin_state pins;
	} __packed outbuf;
	int rc = 0;
	bool data_first = false;

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	/* Zephyr currently defines drive strength support based on
	 * the behavior and capabilities of the Nordic GPIO
	 * peripheral: strength defaults to low but can be set high,
	 * and is controlled independently for output levels.
	 *
	 * SX150x defaults to high strength, and does not support
	 * different strengths for different levels.
	 *
	 * Until something more general is available reject any
	 * attempt to set a non-default drive strength.
	 */
	if ((flags & (GPIO_DS_ALT_LOW | GPIO_DS_ALT_HIGH)) != 0) {
		return -ENOTSUP;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	pins->open_drain &= ~BIT(pin);
	if ((flags & GPIO_SINGLE_ENDED) != 0) {
		if ((flags & GPIO_LINE_OPEN_DRAIN) != 0) {
			pins->open_drain |= BIT(pin);
		} else {
			/* Open source not supported */
			rc = -ENOTSUP;
			goto out;
		}
	}

	if ((flags & GPIO_PULL_UP) != 0) {
		pins->pull_up |= BIT(pin);
	} else {
		pins->pull_up &= ~BIT(pin);
	}
	if ((flags & GPIO_PULL_DOWN) != 0) {
		pins->pull_down |= BIT(pin);
	} else {
		pins->pull_down &= ~BIT(pin);
	}

	if ((flags & GPIO_INPUT) != 0) {
		pins->input_disable &= ~BIT(pin);
	} else {
		pins->input_disable |= BIT(pin);
	}

	if ((flags & GPIO_OUTPUT) != 0) {
		pins->dir &= ~BIT(pin);
		if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			pins->data &= ~BIT(pin);
			data_first = true;
		} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			pins->data |= BIT(pin);
			data_first = true;
		}
	} else {
		pins->dir |= BIT(pin);
	}

	if (flags & GPIO_INT_DEBOUNCE) {
		pins->debounce_en |= BIT(pin);
	} else {
		pins->debounce_en &= ~BIT(pin);
	}

	outbuf.reg = SX1509B_REG_INPUT_DISABLE;
	outbuf.pins.input_disable = sys_cpu_to_be16(pins->input_disable);
	outbuf.pins.long_slew = sys_cpu_to_be16(pins->long_slew);
	outbuf.pins.low_drive = sys_cpu_to_be16(pins->low_drive);
	outbuf.pins.pull_up = sys_cpu_to_be16(pins->pull_up);
	outbuf.pins.pull_down = sys_cpu_to_be16(pins->pull_down);
	outbuf.pins.open_drain = sys_cpu_to_be16(pins->open_drain);
	outbuf.pins.polarity = sys_cpu_to_be16(pins->polarity);
	outbuf.pins.dir = sys_cpu_to_be16(pins->dir);
	outbuf.pins.data = sys_cpu_to_be16(pins->data);

	LOG_DBG("CFG %u %x : ID %04x ; PU %04x ; PD %04x ; DIR %04x ; DAT %04x",
		pin, flags,
		pins->input_disable, pins->pull_up, pins->pull_down,
		pins->dir, pins->data);
	if (data_first) {
		rc = i2c_reg_write_word_be(drv_data->i2c_master,
					   cfg->i2c_slave_addr,
					   SX1509B_REG_DATA, pins->data);
		if (rc == 0) {
			rc = i2c_write(drv_data->i2c_master,
				       &outbuf.reg,
				       sizeof(outbuf) - sizeof(pins->data) -
				       sizeof(pins->interrupt) -
				       sizeof(pins->interrupt_sense) -
				       sizeof(pins->debounce_en),
				       cfg->i2c_slave_addr);
		}
	} else {
		rc = i2c_write(drv_data->i2c_master,
			       &outbuf.reg,
			       sizeof(outbuf) - sizeof(pins->interrupt) -
			       sizeof(pins->interrupt_sense) -
			       sizeof(pins->debounce_en),
			       cfg->i2c_slave_addr);
	}

	/* write only one byte for debounce config */
	rc = i2c_reg_write_byte_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_DEBOUNCE_CONFIG,
					CONFIG_GPIO_SX1509B_DEBOUNCE_TIME);
	if (rc) {
		goto out;
	}

	rc = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_DEBOUNCE_ENABLE,
					pins->debounce_en);
	if (rc) {
		goto out;
	}

out:
	k_sem_give(&drv_data->lock);
	return rc;
}

static int port_get(struct device *dev,
		    gpio_port_value_t *value)
{
	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	u16_t pin_data;
	int rc = 0;

	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	u8_t cmd = SX1509B_REG_DATA;

	rc = i2c_write_read(drv_data->i2c_master, cfg->i2c_slave_addr,
			    &cmd, sizeof(cmd),
			    &pin_data, sizeof(pin_data));
	LOG_DBG("read %04x got %d", sys_be16_to_cpu(pin_data), rc);
	if (rc != 0) {
		goto out;
	}

	*value = sys_be16_to_cpu(pin_data);

out:
	k_sem_give(&drv_data->lock);
	return rc;
}

static int port_write(struct device *dev,
		      gpio_port_pins_t mask,
		      gpio_port_value_t value,
		      gpio_port_value_t toggle)
{
	/* Can't do I2C bus operations from an ISR */
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	u16_t *outp = &drv_data->pin_state.data;

	k_sem_take(&drv_data->lock, K_FOREVER);

	u16_t orig_out = *outp;
	u16_t out = ((orig_out & ~mask) | (value & mask)) ^ toggle;
	int rc = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
				       SX1509B_REG_DATA, out);
	if (rc == 0) {
		*outp = out;
	}

	k_sem_give(&drv_data->lock);

	LOG_DBG("write %04x msk %04x val %04x => %04x: %d", orig_out, mask, value, out, rc);

	return rc;
}

static int port_set_masked(struct device *dev,
			   gpio_port_pins_t mask,
			   gpio_port_value_t value)
{
	return port_write(dev, mask, value, 0);
}

static int port_set_bits(struct device *dev,
			 gpio_port_pins_t pins)
{
	return port_write(dev, pins, pins, 0);
}

static int port_clear_bits(struct device *dev,
			   gpio_port_pins_t pins)
{
	return port_write(dev, pins, 0, 0);
}

static int port_toggle_bits(struct device *dev,
			    gpio_port_pins_t pins)
{
	return port_write(dev, 0, 0, pins);
}

static int pin_interrupt_configure(struct device *dev,
				   gpio_pin_t pin,
				   enum gpio_int_mode mode,
				   enum gpio_int_trig trig)
{
	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	struct sx1509b_pin_state *pins = &drv_data->pin_state;
	int rc = 0;

	if (!IS_ENABLED(CONFIG_GPIO_SX1509B_INTERRUPT) &&
			(mode != GPIO_INT_MODE_DISABLED)) {
		return -ENOTSUP;
	}

	k_sem_take(&drv_data->lock, K_FOREVER);

	if (mode == GPIO_INT_MODE_EDGE) {
		pins->interrupt &= ~BIT(pin);
		if (trig == GPIO_INT_TRIG_BOTH) {
			pins->interrupt_sense |= (SX1509B_EDGE_BOTH <<
								(pin * 2));
		} else if (trig == GPIO_INT_TRIG_LOW) {
			pins->interrupt_sense |= (SX1509B_EDGE_FALLING <<
								(pin * 2));
		} else if (trig == GPIO_INT_TRIG_HIGH) {
			pins->interrupt_sense |= (SX1509B_EDGE_RISING <<
								(pin * 2));
		}
	} else {
		pins->interrupt |= BIT(pin);
	}

	rc = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_INTERRUPT_MASK,
					pins->interrupt);
	if (rc) {
		goto out;
	}

	rc = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_SENSE_B,
					(pins->interrupt_sense >> 16) & 0xffff);
	if (rc) {
		goto out;
	}

	rc = i2c_reg_write_word_be(drv_data->i2c_master, cfg->i2c_slave_addr,
					SX1509B_REG_SENSE_A,
					pins->interrupt_sense & 0xffff);

out:
	k_sem_give(&drv_data->lock);
	return rc;
}

/**
 * @brief Initialization function of SX1509B
 *
 * @param dev Device struct
 * @return 0 if successful, failed otherwise.
 */
static int sx1509b_init(struct device *dev)
{
	const struct sx1509b_config *cfg = dev->config->config_info;
	struct sx1509b_drv_data *drv_data = dev->driver_data;
	int rc;

	/*
	 * storing dev ptr with drv data to be used in gpio_cb
	 * in interrupt ctx
	 */
	drv_data->dev = dev;

	drv_data->i2c_master = device_get_binding(cfg->i2c_master_dev_name);
	if (!drv_data->i2c_master) {
		LOG_ERR("%s: no bus %s", dev->config->name,
			cfg->i2c_master_dev_name);
		rc = -EINVAL;
		goto out;
	}

#ifdef CONFIG_GPIO_SX1509B_INTERRUPT
	drv_data->gpio_int = device_get_binding(cfg->gpio_int_dev_name);
	if (!drv_data->gpio_int) {
		rc = -ENOTSUP;
		goto out;
	} else {
		k_work_init(&drv_data->work, sx1509b_work_handler);

		gpio_pin_configure(drv_data->gpio_int, cfg->gpio_pin,
				GPIO_INPUT | cfg->gpio_flags);
		gpio_pin_interrupt_configure(drv_data->gpio_int, cfg->gpio_pin,
				GPIO_INT_EDGE_TO_ACTIVE);

		gpio_init_callback(&drv_data->gpio_cb, sx1509_int_cb,
				BIT(cfg->gpio_pin));
		gpio_add_callback(drv_data->gpio_int, &drv_data->gpio_cb);
		gpio_enable_callback(drv_data->gpio_int, cfg->gpio_pin);
	}
#endif

	/* Reset state */
	drv_data->pin_state = (struct sx1509b_pin_state) {
		.input_disable	 = 0x0000,
		.pull_up		 = 0x0000,
		.pull_down		 = 0x0000,
		.open_drain		 = 0x0000,
		.dir			 = 0xffff,
		.data			 = 0xffff,
		.interrupt		 = 0xffff,
		.interrupt_sense = 0x00000000,
	};

	rc = i2c_reg_write_byte(drv_data->i2c_master, cfg->i2c_slave_addr,
				SX1509B_REG_RESET, SX1509B_REG_RESET_MAGIC0);
	if (rc != 0) {
		LOG_ERR("%s: reset m0 failed: %d\n", dev->config->name, rc);
		goto out;
	}
	rc = i2c_reg_write_byte(drv_data->i2c_master, cfg->i2c_slave_addr,
				SX1509B_REG_RESET, SX1509B_REG_RESET_MAGIC1);
	if (rc != 0) {
		goto out;
	}

	k_sleep(K_MSEC(RESET_DELAY_MS));

	/* Reset state mediated by initial configuration */
	drv_data->pin_state = (struct sx1509b_pin_state) {
		.dir = (ALL_PINS
			& ~(DT_INST_PROP(0, init_out_low)
			    | DT_INST_PROP(0, init_out_high))),
		.data = (ALL_PINS
			 & ~DT_INST_PROP(0, init_out_low)),
	};

	rc = i2c_reg_write_byte(drv_data->i2c_master, cfg->i2c_slave_addr,
				SX1509B_REG_CLOCK,
				SX1509B_REG_CLOCK_FOSC_INT_2MHZ);
	if (rc == 0) {
		rc = i2c_reg_write_word_be(drv_data->i2c_master,
					   cfg->i2c_slave_addr,
					   SX1509B_REG_DATA,
					   drv_data->pin_state.data);
	}
	if (rc == 0) {
		rc = i2c_reg_write_word_be(drv_data->i2c_master,
					   cfg->i2c_slave_addr,
					   SX1509B_REG_DIR,
					   drv_data->pin_state.dir);
	}
	if (rc != 0) {
		goto out;
	}

out:
	if (rc != 0) {
		LOG_ERR("%s init failed: %d", dev->config->name, rc);
	} else {
		LOG_INF("%s init ok", dev->config->name);
	}
	k_sem_give(&drv_data->lock);
	return rc;
}

#ifdef CONFIG_GPIO_SX1509B_INTERRUPT
static int gpio_sx1509b_manage_callback(struct device *dev,
					  struct gpio_callback *callback,
					  bool set)
{
	struct sx1509b_drv_data *data = dev->driver_data;

	return gpio_manage_callback(&data->cb, callback, set);
}

static int gpio_sx1509b_enable_callback(struct device *dev,
					  gpio_pin_t pin)
{
	struct sx1509b_drv_data *data = dev->driver_data;

	data->cb_pins |= BIT(pin);

	return 0;
}

static int gpio_sx1509b_disable_callback(struct device *dev,
					   gpio_pin_t pin)
{
	struct sx1509b_drv_data *data = dev->driver_data;

	data->cb_pins &= ~BIT(pin);

	return 0;
}
#endif

static const struct gpio_driver_api api_table = {
	.pin_configure = sx1509b_config,
	.port_get_raw = port_get,
	.port_set_masked_raw = port_set_masked,
	.port_set_bits_raw = port_set_bits,
	.port_clear_bits_raw = port_clear_bits,
	.port_toggle_bits = port_toggle_bits,
	.pin_interrupt_configure = pin_interrupt_configure,
#ifdef CONFIG_GPIO_SX1509B_INTERRUPT
	.manage_callback = gpio_sx1509b_manage_callback,
	.enable_callback = gpio_sx1509b_enable_callback,
	.disable_callback = gpio_sx1509b_disable_callback,
#endif
};

static const struct sx1509b_config sx1509b_cfg = {
	.common = {
		.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0),
	},
	.i2c_master_dev_name = DT_INST_BUS_LABEL(0),
	.i2c_slave_addr = DT_INST_REG_ADDR(0),
#ifdef CONFIG_GPIO_SX1509B_INTERRUPT
	.gpio_int_dev_name = DT_INST_GPIO_LABEL(0, nint_gpios),
	.gpio_pin = DT_INST_GPIO_PIN(0, nint_gpios),
	.gpio_flags = DT_INST_GPIO_FLAGS(0, nint_gpios),
#endif
};

static struct sx1509b_drv_data sx1509b_drvdata = {
	.lock = Z_SEM_INITIALIZER(sx1509b_drvdata.lock, 1, 1),
};

DEVICE_AND_API_INIT(sx1509b, DT_INST_LABEL(0),
		    sx1509b_init, &sx1509b_drvdata, &sx1509b_cfg,
		    POST_KERNEL, CONFIG_GPIO_SX1509B_INIT_PRIORITY,
		    &api_table);
