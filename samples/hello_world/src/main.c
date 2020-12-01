/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* This application is intended to emulate a basic work queue, where a
 * thread idles until something is submitted to it, then pulls the
 * work item off and performs some function on it, signaling
 * completion.
 *
 * When run under qemu_x86_64 the application fails with no
 * diagnostics after a varying number of iterations.  Rarely, the
 * failure exhibits as a deadlock (the program stops displaying but
 * does not terminate).
 */

#include <zephyr.h>
#include <kernel.h>
#include <sys/printk.h>

#define STACK_SIZE (1024 + CONFIG_TEST_EXTRA_STACKSIZE)
#define COOP_PRIORITY K_PRIO_COOP(1) /* = -3 */

static K_THREAD_STACK_DEFINE(thr_stack, STACK_SIZE);
static struct k_thread thr;
static atomic_t ctr;
static struct k_queue workq;

#define FLAG_PENDING_BIT 0
#define FLAG_PENDING BIT(0)

struct work_item {
	void *_node;		/* Used by k_queue */
	atomic_t flags;
	int in_ctr;
};

static void thread_main(void *p1, void *p2, void *p3)
{
	struct k_queue *queue = p1;

	printk("thread entered\n");
	while (true) {
		struct work_item *work = k_queue_get(queue, K_FOREVER);

		work->in_ctr = atomic_inc(&ctr);
		atomic_clear_bit(&work->flags, FLAG_PENDING_BIT);
	}
}

void main(void)
{
	struct work_item work = { 0 };
	size_t iters = 0;

	printk("Mode %s\n", IS_ENABLED(CONFIG_SMP) ? "SMP" : "UP");

	atomic_set(&ctr, 0);
	k_queue_init(&workq);
	k_thread_create(&thr, thr_stack, STACK_SIZE,
			thread_main, &workq, NULL, NULL,
			K_PRIO_COOP(1), 0, K_NO_WAIT);

	while (true) {
		atomic_val_t last = atomic_get(&ctr);
		size_t spins = 0;

		atomic_set_bit(&work.flags, FLAG_PENDING_BIT);
		k_queue_append(&workq, &work);

		if (!IS_ENABLED(CONFIG_SMP)) {
			k_yield();
		}

		while (atomic_test_bit(&work.flags, FLAG_PENDING_BIT)) {
			++spins;
		}
		if ((work.flags != 0) || (work.in_ctr != last)) {
			printk("Failed\n");
		}
		if ((iters++ % 1) == 0) {
			printk("Completed iter %zu, %zu spins\n", iters, spins);
		}
	}
}
