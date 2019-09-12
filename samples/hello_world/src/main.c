/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <drivers/gpio.h>
#include "nrf.h"

struct pin_s {
	const char *id;
	s32_t cfg;
};
const struct pin_s p0_pins[] = {
	{"XL1", GPIO_OUTPUT_LOW},
	{"XL2", GPIO_OUTPUT_LOW},
	{"DIG0", GPIO_PULL_DOWN},
	{"DIG1", GPIO_PULL_DOWN},

	{"DIG2", GPIO_PULL_DOWN},
	{"IOX_OSCIO", GPIO_PULL_DOWN},
	{"MPU_INT", GPIO_PULL_DOWN},
	{"SDA", 0}, // H0H1

	{"SCL", 0}, // H0H1
	{"NFC1", 0},
	{"NFC2", 0},
	{"BUTTON", GPIO_INPUT | GPIO_PULL_UP}, // double level

	{"LIS_INT1", 0},
	{"USB_DETECT", GPIO_INPUT}, // S0H1
	{"SDA_EXT", 0}, // H0H1
	{"SCL_EXT", 0}, // H0H1

	{"IOX_RESETn", GPIO_OUTPUT_HIGH},
	{"BAT_CHG_STAT", GPIO_INPUT},
	{"MOS_1", GPIO_OUTPUT_LOW},
	{"MOS_2", GPIO_OUTPUT_LOW},

	{"MOS_3", GPIO_OUTPUT_LOW},
	{"MOS_4", GPIO_OUTPUT_LOW},
	{"CCS_INT", GPIO_PULL_DOWN},
	{"LPS_INT", GPIO_INPUT},

	{"HTS_INT", GPIO_PULL_DOWN},
	{"MIC_DOUT", GPIO_INPUT | GPIO_PULL_DOWN},
	{"MIC_CLK", GPIO_OUTPUT_LOW},
	{"SPEAKER", GPIO_OUTPUT_LOW},

	{"BATTERY", GPIO_INPUT},
	{"SPK_PWR_CTRL", GPIO_OUTPUT_LOW},
	{"VDD_PWR_CTRL", GPIO_OUTPUT_HIGH},
	{"BH_INT", GPIO_PULL_DOWN},
};

const struct pin_s iox_pins[] = {
	{"IOEXT0", GPIO_OUTPUT_LOW},
	{"IOEXT1", GPIO_OUTPUT_LOW},
	{"IOEXT2", GPIO_OUTPUT_LOW},
	{"IOEXT3", GPIO_OUTPUT_LOW},
	{"BAT_MON_EN", GPIO_OUTPUT_LOW},
	{"LIGHTWELL_G", GPIO_OUTPUT_HIGH},
	{"LIGHTWELL_B", GPIO_OUTPUT_HIGH},
	{"LIGHTWELL_R", GPIO_OUTPUT_HIGH},
	{"MPU_PWR_CTRL", GPIO_OUTPUT_LOW},
	{"MIC_PWR_CTRL", GPIO_OUTPUT_LOW},
	{"CCS_PWR_CTRL", GPIO_OUTPUT_LOW},
	{"CCS_RESETn", GPIO_OUTPUT_LOW},
	{"CCS_WAKEn", GPIO_OUTPUT_LOW},
	{"SENSE_R", GPIO_OUTPUT_HIGH},
	{"SENSE_G", GPIO_OUTPUT_HIGH},
	{"SENSE_B", GPIO_OUTPUT_HIGH},
};

void main(void)
{
	if (IS_ENABLED(CONFIG_BOOT_BANNER)) {
		printk("Hello World! %s\n", CONFIG_BOARD);
		printk("ICACHE %x ; DCDCEN %x\n", NRF_NVMC->ICACHECNF, NRF_POWER->DCDCEN);
	}

#if 1
	struct device *gpio = device_get_binding(DT_GPIO_P0_DEV_NAME);
	if (!gpio) {
		printk("FAILED TO FIND GPIO\n");
		return;
	}
	u32_t in = NRF_GPIO->IN;
	u32_t out = NRF_GPIO->OUT;
	const struct pin_s *pp = p0_pins;
	for (u32_t pin = 0; pin < ARRAY_SIZE(p0_pins); ++pin) {
		u32_t bit = (1U << pin);
		u32_t pre = NRF_GPIO->PIN_CNF[pin];
		u32_t post = pre;
		if (0 <= pp->cfg) {
			gpio_pin_configure(gpio, pin, pp->cfg);
			post = NRF_GPIO->PIN_CNF[pin];
		}
		if (IS_ENABLED(CONFIG_BOOT_BANNER)) {
			printk("P0.%02u %x >%d %d> %s", pin, pre,
			       !!(bit & in), !!(bit & out), pp->id);
			if (0 <= pp->cfg) {
				printk(" => %x\n", post);
			} else {
				printk("\n");
			}
		}
		++pp;
	}

	gpio = device_get_binding(DT_INST_0_SEMTECH_SX1509B_LABEL);
	if (!gpio) {
		printk("FAILED TO FIND IOX\n");
		return;
	}

	pp = iox_pins;
	for (u32_t pin = 0; pin < ARRAY_SIZE(iox_pins); ++pin) {
		int rc = gpio_pin_configure(gpio, pin, pp->cfg);
		if (IS_ENABLED(CONFIG_BOOT_BANNER)) {
			printk("IOX.%02u %s %x : %d\n", pin, pp->id, pp->cfg, rc);
		}
		++pp;
	}
#endif
	if (0) {
		while (5000 > k_uptime_get_32()) {
		}
	}
	k_sleep(K_FOREVER);
}
