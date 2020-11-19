/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <sys/printk.h>

#define STACK_SIZE (512 + CONFIG_TEST_EXTRA_STACKSIZE)

#define COOP_PRIORITY K_PRIO_COOP(1) /* = -3 */

static K_SEM_DEFINE(sync, 0, 1);

static void coop_main(void *p1, void *p2, void *p3)
{
	printk("coop_main %p at prio %d\n",
	       k_current_get(), k_thread_priority_get(k_current_get()));
	while (true) {
		printk("coop_main blocking\n");

		int rc = k_sem_take(&sync, K_FOREVER);

		printk("coop_main took: %d\n", rc);
	}
}

static K_THREAD_DEFINE(coop_thr, STACK_SIZE,
		       coop_main, NULL, NULL, NULL,
		       COOP_PRIORITY, 0, 0);

void main(void)
{
	int ctr = 0;

	printk("Main thread prio: %d\n",
	       k_thread_priority_get(k_current_get()));

	while (++ctr <= 3) {
		printk("main giving %d\n", ctr);
		k_sem_give(&sync);
		printk("main back %d\n", ctr);
	}
}
