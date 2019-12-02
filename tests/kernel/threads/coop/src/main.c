/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <kernel.h>

struct k_thread thr_hi;
struct k_thread thr_lo;
#define STACK_SIZE (512 + CONFIG_TEST_EXTRA_STACKSIZE)
K_THREAD_STACK_DEFINE(stk_hi, STACK_SIZE);
K_THREAD_STACK_DEFINE(stk_lo, STACK_SIZE);

K_SEM_DEFINE(handoff_sem, 0, 1);
K_SEM_DEFINE(sync_sem, 0, 1);
K_SEM_DEFINE(done_sem, 0, 1);

enum {
	ST_INITIAL,
	ST_HI_STARTED,
	ST_LO_SEM_GIVE,
	ST_LO_SEM_YIELD,
	ST_HI_SEM_TAKE,
	ST_LO_DONE,
} volatile state;

static void entry_hi(void *p1, void *p2, void *p3)
{
	int rc;

	zassert_equal(state, ST_INITIAL,
		      "initial != %d", state);
	state = ST_HI_STARTED;
	k_sem_give(&handoff_sem);

	rc = k_sem_take(&sync_sem, K_FOREVER);
	zassert_equal(rc, 0,
		      "sem_take %d", rc);

	zassert_equal(state, ST_LO_SEM_YIELD,
		      "lo sem yield != %d", state);
	state = ST_HI_SEM_TAKE;

	TC_PRINT("hi done\n");
}

static void entry_lo(void *p1, void *p2, void *p3)
{
	int rc = k_sem_take(&handoff_sem, K_FOREVER);

	zassert_equal(rc, 0,
		      "handoff take %d", rc);

	zassert_equal(state, ST_HI_STARTED,
		      "hi started != %d", state);
	state = ST_LO_SEM_GIVE;

	k_sem_give(&sync_sem);
	k_busy_wait(1000U);

	/* If k_sem_give causes a reschedule this assert should
	 * fail.  It doesn't.
	 */
	zassert_equal(state, ST_LO_SEM_GIVE,
		      "lo sem give != %d", state);

	state = ST_LO_SEM_YIELD;
	k_yield();

	zassert_equal(state, ST_HI_SEM_TAKE,
		      "hi sem take != %d", state);

	TC_PRINT("lo done\n");

	k_sem_give(&done_sem);
}

static void test_coop(void)
{
	state = ST_INITIAL;

	k_tid_t lo = k_thread_create(&thr_lo, stk_lo,
				     STACK_SIZE, entry_lo,
				     NULL, NULL, NULL,
				     K_PRIO_COOP(2), 0, 0);
	k_tid_t hi = k_thread_create(&thr_hi, stk_hi,
				     STACK_SIZE, entry_hi,
				     NULL, NULL, NULL,
				     K_PRIO_COOP(1), 0, 0);

	TC_PRINT("lo %d ; hi %d\n", k_thread_priority_get(lo),
		 k_thread_priority_get(hi));

	int rc = k_sem_take(&done_sem, K_MSEC(100));
	zassert_equal(rc, 0,
		      "done sem %d\n", rc);

}

void test_main(void)
{
	k_thread_access_grant(k_current_get(), &thr_lo, stk_lo);
	k_thread_access_grant(k_current_get(), &thr_hi, stk_hi);

	ztest_test_suite(suite_coop,
			 ztest_unit_test(test_coop)
			 );

	ztest_run_test_suite(suite_coop);
}
