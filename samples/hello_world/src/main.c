/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

#define NUM_TIMERS 50
#define TIMEOUT_INTERVAL K_MSEC(1)

struct state_s {
	struct k_timer timer;
	volatile unsigned int state;
	volatile unsigned int count;
};

struct state_s state[NUM_TIMERS];

void expiry(struct k_timer *timer)
{
	struct state_s *sp = CONTAINER_OF(timer, struct state_s, timer);

	sp->count += 1;
	if (sp->state) {
		k_timer_stop(timer);
		sp->state += 1;
	}
}

void main(void)
{
	const unsigned int cps = sys_clock_hw_cycles_per_sec();

	printk("Running with %u timers, %u Hz\n", NUM_TIMERS, cps);

	for (int i = 0; i < NUM_TIMERS; ++i) {
		struct state_s *sp = state + i;

		k_timer_init(&sp->timer, expiry, NULL);
		sp->state = sp->count = 0;
	}

	unsigned int t0 = k_cycle_get_32();
	for (int i = 0; i < NUM_TIMERS; ++i) {
		struct state_s *sp = state + i;

		k_timer_start(&sp->timer, K_NO_WAIT, TIMEOUT_INTERVAL);
	}
	k_sleep(3000);

	for (int i = 0; i < NUM_TIMERS; ++i) {
		struct state_s *sp = state + i;

		sp->state = 1;
	}

	for (int i = 0; i < NUM_TIMERS; ++i) {
		struct state_s *sp = state + i;
		while (sp->state < 2) {
			k_sleep(0);
		}
	}
	unsigned int t1 = k_cycle_get_32();

	unsigned int incrs = 0;
	unsigned int min_incrs;
	unsigned int max_incrs;
	for (int i = 0; i < NUM_TIMERS; ++i) {
		struct state_s *sp = state + i;

		if ((i == 0) || (sp->count < min_incrs)) {
			min_incrs = sp->count;
		}
		if ((i == 0) || (sp->count > max_incrs)) {
			max_incrs = sp->count;
		}
		//printk("T%u incr %u\n", i, sp->count);
		incrs += sp->count;
	}

	u32_t dur_us = (u32_t)((1000000ULL * (t1 - t0)) / cps);
	printk("%u incrs in [%u, %u] over %u cycles = %u us\n", incrs,
	       min_incrs, max_incrs, t1 - t0, dur_us);
	printk("%u incrs/s\n", (u32_t)(incrs * 1000000ULL / dur_us));
}
