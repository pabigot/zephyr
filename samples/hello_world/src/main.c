/*
 * Copyright (c) 2020, Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <stdio.h>
#include <sys/printk.h>
#include <sys/util.h>

#define INTERVAL_MS 1000
#define WORK_DELAY_US 0
#define TIMEOUT_DELAY_US 50

enum mode {
	MODE_INTERVAL,
	MODE_REL_SCH_OFFSET,
	MODE_REL_WRK_OFFSET,
	MODE_REL_NOW_OFFSET,
	MODE_ABS,
	MODE_IMMED,
	MODE_LIMIT,
};

#define ENABLED_MODES (0						\
	| BIT(MODE_INTERVAL)						\
	| BIT(MODE_REL_SCH_OFFSET)					\
	| BIT(MODE_REL_WRK_OFFSET)					\
	| BIT(MODE_ABS)							\
	)

/*
	| BIT(MODE_INTERVAL)						\
	| BIT(MODE_REL_SCH_OFFSET)					\
	| BIT(MODE_REL_WRK_OFFSET)					\
	| BIT(MODE_REL_NOW_OFFSET)					\
	| BIT(MODE_IMMED)						\
	| BIT(MODE_ABS)							\
*/

static const char *const mode_s[] = {
	[MODE_INTERVAL] = "interval",
	[MODE_REL_SCH_OFFSET] = "rel sch",
	[MODE_REL_WRK_OFFSET] = "rel wrk",
	[MODE_REL_NOW_OFFSET] = "rel now",
	[MODE_IMMED] = "immediate",
	[MODE_ABS] = "absolute",
};

struct state {
	struct k_timer timer;
	struct k_work work;
	uint32_t count;
	uint32_t str_ut;	/* time first scheduled */
	uint32_t sch_ut;	/* time last scheduled */
	uint32_t tmr_ut;	/* when timer handler invoked */
	uint32_t wrk_ut;	/* when work handler invoked */
};

static struct state states[MODE_LIMIT];

static const char *ms_str(uint32_t time_ms)
{
	static char buf[16]; /* ...HH:MM:SS.MMM */
	unsigned int ms = time_ms % MSEC_PER_SEC;
	unsigned int s;
	unsigned int min;
	unsigned int h;

	time_ms /= MSEC_PER_SEC;
	s = time_ms % 60U;
	time_ms /= 60U;
	min = time_ms % 60U;
	time_ms /= 60U;
	h = time_ms;

	snprintf(buf, sizeof(buf), "%u:%02u:%02u.%03u",
		 h, min, s, ms);
	return buf;
}

void work_handler(struct k_work* work)
{
	struct state *sp = CONTAINER_OF(work, struct state, work);
	enum mode mode = (enum mode)(sp - states);
	k_timeout_t next_to = K_FOREVER;

	sp->wrk_ut = k_uptime_get();

	int32_t sch_tmr = sp->tmr_ut - sp->sch_ut;
	int32_t sch_wrk = sp->wrk_ut - sp->sch_ut;
	int32_t str_wrk = sp->wrk_ut - sp->str_ut;
	int32_t delay = INTERVAL_MS - sch_wrk;

	printk("%s %s: %u, err %d %d %d : %d\n", ms_str(sp->wrk_ut), mode_s[mode],
	       sp->count, sch_tmr, sch_wrk, str_wrk, delay);

	if (WORK_DELAY_US != 0) {
		k_busy_wait(WORK_DELAY_US);
	}

	uint32_t now = k_uptime_get();
	uint32_t sch_ut = sp->sch_ut;

	sp->sch_ut += INTERVAL_MS;
	if (MODE_REL_SCH_OFFSET == mode) {
		next_to = K_MSEC(INTERVAL_MS - (int32_t)(now - sch_ut));
	} else if (MODE_REL_WRK_OFFSET == mode) {
		next_to = K_MSEC(INTERVAL_MS - (int32_t)(now - sp->wrk_ut));
	} else if (MODE_REL_NOW_OFFSET == mode) {
		next_to = K_MSEC(INTERVAL_MS);
	} else if (MODE_IMMED == mode) {
		next_to = K_NO_WAIT;
	} else if (MODE_ABS == mode) {
		next_to = K_TIMEOUT_ABS_MS(sp->sch_ut);
	}
	if (!K_TIMEOUT_EQ(next_to, K_FOREVER)) {
		k_timer_start(&sp->timer, next_to, K_NO_WAIT);
	}
}

void timeout_handler(struct k_timer* timer)
{
	struct state *sp = CONTAINER_OF(timer, struct state, timer);

	sp->count += 1;
	sp->tmr_ut = k_uptime_get();

	if (TIMEOUT_DELAY_US != 0) {
		k_busy_wait(TIMEOUT_DELAY_US);
	}

	k_work_submit(&sp->work);
}

void main(void)
{
	uint32_t ut = k_uptime_get() ;
	k_timeout_t start_to = K_TIMEOUT_ABS_MS(ut + INTERVAL_MS);

	printk("Timer handler delays %u us\n", TIMEOUT_DELAY_US);
	printk("Work handler delays %u us\n", WORK_DELAY_US);
	for (int i = 0; i < MODE_LIMIT; ++i) {
		struct state *sp = states + i;
		enum mode mode = (enum mode)i;
		k_timeout_t interval_to = K_NO_WAIT;

		if ((BIT(mode) & ENABLED_MODES) == 0) {
			continue;
		}

		sp->str_ut = ut;
		sp->sch_ut = ut + INTERVAL_MS;

		if (mode == MODE_INTERVAL) {
			interval_to = K_MSEC(INTERVAL_MS);
		}

		k_work_init(&sp->work, work_handler);
		k_timer_init(&sp->timer, timeout_handler, 0);
		k_timer_start(&sp->timer, start_to, interval_to);
		printk("scheduled %p as %s\n", sp, mode_s[mode]);
	}

	while (true) {
		k_sleep(K_FOREVER);
	}
}
