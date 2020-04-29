/*
 * Copyright (c) 2019-2020 Peter Bigot Consulting, LLC
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#define DT_DRV_COMPAT "test_nrf_timer"

#include <zephyr.h>
#include <device.h>
#include <drivers/counter.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <drivers/rtc/ds3231.h>
#include <drivers/hwinfo.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/nrf_clock_control.h>

#define NODE DT_PATH(test_config)

#define CLOCK ((NRF_CLOCK_Type*)(DT_REG_ADDR(DT_NODELABEL(clock))))

#define TICK_RTC ((NRF_RTC_Type*)(DT_NODE_HAS_PROP(NODE, tick_rtc)	\
				  ? (void*)DT_REG_ADDR(DT_PHANDLE(NODE, tick_rtc)) \
				  : NRF_RTC1))
#define TICK_CAPTURE_CC 1

#define HFCLK_TIMER ((NRF_TIMER_Type*)DT_REG_ADDR(DT_PHANDLE(NODE, hf_timer)))
#define HFCLK_Hz (16U * 1000U * 1000U)
#define HFCLK_READ_CC 0
#define HFCLK_CAPTURE_CC 1
#define HFCLK_GPIOTE_CH 1
#define HFCLK_PPI_CH 0

#define GPIOTE ((NRF_GPIOTE_Type*)DT_REG_ADDR(DT_NODELABEL(gpiote)))
#define PPI NRF_PPI

static u32_t hfclk_Hz;
static inline u32_t hfclk_read32(void)
{
	HFCLK_TIMER->TASKS_CAPTURE[HFCLK_READ_CC] = 1;
	return HFCLK_TIMER->CC[HFCLK_READ_CC];
}

static u32_t const tick_Hz = 32768;
static inline u32_t tick_read24(void)
{
	return TICK_RTC->COUNTER;
}

static u32_t systick_Hz;
static u32_t volatile systick_overflows;
static inline u32_t systick_read32(void)
{
	return ((systick_overflows + 1U) << 24) - SysTick->VAL;
}

static inline u32_t delta32(u32_t a, u32_t b)
{
	return b - a;
}

static inline u32_t delta24(u32_t a, u32_t b)
{
	return BIT_MASK(24) & (b - a);
}

struct gpios {
	NRF_GPIO_Type* periph;
	union {
		const char *label;
		struct device *dev;
	};
	gpio_pin_t pin;
	gpio_dt_flags_t flags;
	bool port;
};

#define GPIO_INITIALIZER(prop) {			\
	.periph = (NRF_GPIO_Type*)DT_REG_ADDR(DT_PHANDLE(NODE, prop)), \
	.label = DT_GPIO_LABEL(NODE, prop),		\
	.pin = DT_GPIO_PIN(NODE,prop),			\
	.flags = DT_GPIO_FLAGS(NODE, prop),		\
	.port = (DT_REG_ADDR(DT_PHANDLE(NODE, prop)) != NRF_P0_BASE),  \
}

static struct config {
	struct gpios int_pps;
	struct gpios hf_pps;
	struct gpios lf_pps;
} config = {
	.int_pps = GPIO_INITIALIZER(int_gpios),
	.hf_pps = GPIO_INITIALIZER(hf_gpios),
	.lf_pps = GPIO_INITIALIZER(lf_gpios),
};

struct sample {
	u32_t trig_capture_hfclk;
	u32_t int_capture_tick;
	u32_t int_capture_systick;
	u32_t work_capture_tick;
	u32_t work_capture_systick;
};

static struct sample volatile sample;
static struct sample last_sample;

static void show_id(void)
{
	union {
		u8_t hwid[10];
		char hwtext[31];
	} u = { 0 };
	int len = hwinfo_get_device_id(u.hwid, sizeof(u.hwid));
	const u8_t *sp = u.hwid + len;
	char *dp = u.hwtext + 3 * len - 1;

	*dp-- = 0;
	while (sp > u.hwid) {
		static char hexid[] = "0123456789abcdef";

		--sp;
		*dp-- = hexid[*sp & 0x0F];
		*dp-- = hexid[*sp >> 4];
		if (dp > u.hwtext) {
			*dp-- = ':';
		}
	}

	printk("%s %s\n", CONFIG_BOARD, u.hwtext);
	printk("RTC at %p, HFTIMER at %p\n",
	       TICK_RTC, HFCLK_TIMER);
}

static void show_clkstat(void)
{
	static const char *lfclk_src[] = {
		[CLOCK_LFCLKSTAT_SRC_RC] = "RC",
		[CLOCK_LFCLKSTAT_SRC_Xtal] = "Xtal",
		[CLOCK_LFCLKSTAT_SRC_Synth] = "Synth",
	};
	static const char *hfclk_src[] = {
		[CLOCK_HFCLKSTAT_SRC_RC] = "RC",
		[CLOCK_HFCLKSTAT_SRC_Xtal] = "Xtal",
	};
	u32_t reg = CLOCK->LFCLKSTAT;

	printk("LFCLK: %s %s\n",
	       (reg & CLOCK_LFCLKSTAT_STATE_Msk) ? "Running" : "Off",
	       lfclk_src[(reg & CLOCK_LFCLKSTAT_SRC_Msk)
			 >> CLOCK_LFCLKSTAT_SRC_Pos]);
	reg = CLOCK->HFCLKSTAT;
	printk("HFCLK: %s %s, raw freq %u Hz ; timer %u Hz\n",
	       (reg & CLOCK_HFCLKSTAT_STATE_Msk) ? "Running" : "Off",
	       hfclk_src[(reg & CLOCK_HFCLKSTAT_SRC_Msk)
			 >> CLOCK_HFCLKSTAT_SRC_Pos],
	       HFCLK_Hz,
	       HFCLK_Hz >> HFCLK_TIMER->PRESCALER);
}

static inline s32_t err_ppm(u32_t delta,
			    u32_t expected)
{
	s32_t err = delta - expected;
	return ((s64_t)1000000 * err) / (s32_t)expected;
}

static void pps_work_handler(struct k_work *work)
{
	static u32_t count;
	struct sample cur = sample;

	cur.work_capture_systick = systick_read32();
	cur.work_capture_tick = tick_read24();
	++count;

	u32_t tk_delay = delta24(cur.int_capture_tick, cur.work_capture_tick);
	u32_t st_delay = delta32(cur.int_capture_systick, cur.work_capture_systick);
	u32_t tk_delta = delta24(last_sample.int_capture_tick,
				  cur.int_capture_tick);
	u32_t st_delta = delta32(last_sample.int_capture_systick,
				 cur.int_capture_systick);
	u32_t hf_delta = delta32(last_sample.trig_capture_hfclk,
				 cur.trig_capture_hfclk);

	printk("%u: delay %u / %u\n"
	       "\tlf %u delta %u ; err %d ppm\n"
	       "\tsystick %u delta %u; err %d ppm\n"
	       "\thf %u delta %u ; err %d ppm\n",
	       count, tk_delay, st_delay,
	       cur.int_capture_tick, tk_delta, err_ppm(tk_delta, tick_Hz),
	       cur.int_capture_systick, st_delta, err_ppm(st_delta, systick_Hz),
	       cur.trig_capture_hfclk, hf_delta, err_ppm(hf_delta, hfclk_Hz));

	if ((count % 10) == 0) {
		show_clkstat();
	}

	last_sample = cur;
}

static K_WORK_DEFINE(pps_work, pps_work_handler);

static void pps_irq_handler(struct device *dev,
			    struct gpio_callback *cb,
			    u32_t pins)
{
	sample.int_capture_systick = systick_read32();
	sample.int_capture_tick = tick_read24();
	sample.trig_capture_hfclk = HFCLK_TIMER->CC[HFCLK_CAPTURE_CC];
	k_work_submit(&pps_work);
}

static void setup_captures(void)
{
	config.int_pps.dev = device_get_binding(config.int_pps.label);
	config.hf_pps.dev = device_get_binding(config.hf_pps.label);
	config.lf_pps.dev = device_get_binding(config.lf_pps.label);

	SysTick->LOAD = 0xffffff;
	SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk |
			  SysTick_CTRL_CLKSOURCE_Msk);

	const u32_t delay_us = 1000;
	u32_t t0 = SysTick->VAL;
	k_busy_wait(delay_us);
	u32_t t1 = SysTick->VAL;
	u32_t dt = delta24(t1, t0);
	systick_Hz = 1000U * 1000U * ((dt + 500U) / 1000U);
	printk("SysTick advanced %u in %u us, %u Hz\n",
	       dt, delay_us, systick_Hz);

	HFCLK_TIMER->MODE = (TIMER_MODE_MODE_Timer << TIMER_MODE_MODE_Pos);
	HFCLK_TIMER->BITMODE = (TIMER_BITMODE_BITMODE_32Bit << TIMER_BITMODE_BITMODE_Pos);
	HFCLK_TIMER->PRESCALER = 0;
	HFCLK_TIMER->TASKS_START = 1;

	hfclk_Hz = (16UL * 1000U * 1000U) >> HFCLK_TIMER->PRESCALER;

	t0 = hfclk_read32();
	k_busy_wait(delay_us);
	t1 = hfclk_read32();
	dt = delta32(t0, t1);
	printk("HFCLK advanced %u in %u us, %u Hz\n",
	       dt, delay_us, hfclk_Hz);

	GPIOTE->CONFIG[HFCLK_GPIOTE_CH] = 0
		| (GPIOTE_CONFIG_MODE_Event << GPIOTE_CONFIG_MODE_Pos)
		| (config.hf_pps.pin << GPIOTE_CONFIG_PSEL_Pos)
		| (config.hf_pps.port << GPIOTE_CONFIG_PORT_Pos)
		| (GPIOTE_CONFIG_POLARITY_HiToLo << GPIOTE_CONFIG_POLARITY_Pos)
		;

	PPI->CH[HFCLK_PPI_CH] = (PPI_CH_Type){
		.EEP = (uintptr_t)(GPIOTE->EVENTS_IN + HFCLK_GPIOTE_CH),
		.TEP = (uintptr_t)(HFCLK_TIMER->TASKS_CAPTURE + HFCLK_CAPTURE_CC),
	};
	PPI->CHENSET = BIT(HFCLK_PPI_CH);

	static struct gpio_callback int_pps_cb;
	gpio_init_callback(&int_pps_cb, pps_irq_handler,
			   BIT(config.int_pps.pin));

	gpio_add_callback(config.int_pps.dev, &int_pps_cb);

	int rc = gpio_pin_configure(config.int_pps.dev,
				    config.int_pps.pin,
				    GPIO_INPUT | config.int_pps.flags);
	if (rc >= 0) {
		rc = gpio_pin_interrupt_configure(config.int_pps.dev,
						  config.int_pps.pin,
						  GPIO_INT_EDGE_TO_ACTIVE);
	}
	printk("pin config %d\n", rc);
}

void main(void)
{
	int rc;
	struct device *ds3231;
	struct device *clock = device_get_binding(DT_LABEL(DT_NODELABEL(clock)));

	show_id();
	setup_captures();
	show_clkstat();

	const char *const dev_id = DT_LABEL(DT_INST(0, maxim_ds3231));

	printk("clock %p\n", clock);

	if (false) {
		rc = clock_control_on(clock, CLOCK_CONTROL_NRF_SUBSYS_HF);
		printk("Clock on %d\n", rc);
	}

	ds3231 = device_get_binding(dev_id);
	if (!ds3231) {
		printk("No device %s available\n", dev_id);
		return;
	}

	rc = rtc_ds3231_ctrl_update(ds3231,
				    0,
				    RTC_DS3231_REG_CTRL_RS_Msk | RTC_DS3231_REG_CTRL_INTCN);
	printk("Enable PPS got %d\n", rc);

	u32_t t0 = k_uptime_get_32();
	u32_t last_systick = SysTick->VAL;
	u32_t t1;
	do {
		u32_t systick = SysTick->VAL;
		systick_overflows += (systick > last_systick);
		last_systick = systick;
		t1 = k_uptime_get_32();
	} while ((t1 - t0) < 10000U);

	rc = rtc_ds3231_ctrl_update(ds3231,
					RTC_DS3231_REG_CTRL_INTCN,
					0);
	printk("Disable PPS got %d\n", rc);
}
