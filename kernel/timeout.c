/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <timeout_q.h>
#include <drivers/system_timer.h>
#include <sys_clock.h>
#include <spinlock.h>
#include <ksched.h>
#include <syscall_handler.h>

#define LOCKED(lck) for (k_spinlock_key_t __i = {},			\
					  __key = k_spin_lock(lck);	\
			!__i.key;					\
			k_spin_unlock(lck, __key), __i.key = 1)

static u64_t curr_tick;

static sys_dlist_t timeout_list = SYS_DLIST_STATIC_INIT(&timeout_list);

static struct k_spinlock timeout_lock;

static bool can_wait_forever;

#if defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME)
int z_clock_hw_cycles_per_sec = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
#endif

static struct _timeout *first(void)
{
	sys_dnode_t *t = sys_dlist_peek_head(&timeout_list);

	return t == NULL ? NULL : CONTAINER_OF(t, struct _timeout, node);
}

static struct _timeout *next(struct _timeout *t)
{
	sys_dnode_t *n = sys_dlist_peek_next(&timeout_list, &t->node);

	return n == NULL ? NULL : CONTAINER_OF(n, struct _timeout, node);
}

static void remove_timeout(struct _timeout *t)
{
	if (next(t) != NULL) {
		next(t)->dticks += t->dticks;
	}

	sys_dlist_remove(&t->node);
	t->dticks = 0;
}

void _add_timeout(struct _timeout *to, _timeout_func_t fn, s32_t ticks)
{
	__ASSERT(!sys_dnode_is_linked(&to->node), "");
	to->fn = fn;
	ticks = max(1, ticks);

	LOCKED(&timeout_lock) {
		struct _timeout *t;

		// set for rel-current, add to previous for rel-previous
		// this is why the deadline should be absolute!
		//to->dticks += ticks;
		to->dticks = ticks;
		for (t = first(); t != NULL; t = next(t)) {
			if (t->dticks > to->dticks) {
				t->dticks -= to->dticks;
				sys_dlist_insert_before(&timeout_list,
							&t->node, &to->node);
				break;
			}
			to->dticks -= t->dticks;
		}

		if (t == NULL) {
			sys_dlist_append(&timeout_list, &to->node);
		}

		if (to == first()) {
			z_clock_set_timeout(_get_next_timeout_expiry(), false);
		}
	}
}

int _abort_timeout(struct _timeout *to)
{
	int ret = -EINVAL;

	LOCKED(&timeout_lock) {
		if (sys_dnode_is_linked(&to->node)) {
			remove_timeout(to);
			ret = 0;
		}
	}

	return ret;
}

s32_t z_timeout_remaining(struct _timeout *timeout)
{
	s32_t ticks = 0;

	if (_is_inactive_timeout(timeout)) {
		return 0;
	}

	LOCKED(&timeout_lock) {
		for (struct _timeout *t = first(); t != NULL; t = next(t)) {
			ticks += t->dticks;
			if (timeout == t) {
				break;
			}
		}
	}

	return ticks;
}

void z_clock_announce(s32_t ticks)
{
#ifdef CONFIG_TIMESLICING
	z_time_slice(ticks);
#endif

	k_spinlock_key_t key = k_spin_lock(&timeout_lock);
	struct _timeout *t = first();

	curr_tick += ticks;

	if (t) {
		sys_dlist_t ready;
		sys_dnode_t *node;

		while (t) {
			if (t->dticks > ticks) {
				break;
			}
			t = next(t);
		}

		sys_dlist_init(&ready);
		if (t == NULL) {
			sys_dlist_catenate(&ready, &timeout_list);
		} else {
			sys_dlist_split(&timeout_list, &t->node, &ready);
			t->dticks -= ticks;
		}

		node = sys_dlist_peek_head(&ready);
		if (node) {
			k_spin_unlock(&timeout_lock, key);
			while (node) {
				sys_dlist_remove(node);
				t = CONTAINER_OF(node, struct _timeout, node);
				t->dticks -= ticks;

				t->fn(t);

				node = sys_dlist_peek_head(&ready);
			}
			key = k_spin_lock(&timeout_lock);
		}
	}

	z_clock_set_timeout(_get_next_timeout_expiry(), false);

	k_spin_unlock(&timeout_lock, key);
}

s32_t _get_next_timeout_expiry(void)
{
	s32_t ret = 0;
	int maxw = can_wait_forever ? K_FOREVER : INT_MAX;

	LOCKED(&timeout_lock) {
		struct _timeout *to = first();

		ret = to == NULL ? maxw : max(0, to->dticks);
	}

#ifdef CONFIG_TIMESLICING
	if (_current_cpu->slice_ticks && _current_cpu->slice_ticks < ret) {
		ret = _current_cpu->slice_ticks;
	}
#endif
	return ret;
}

void z_set_timeout_expiry(s32_t ticks, bool idle)
{
	LOCKED(&timeout_lock) {
		int next = _get_next_timeout_expiry();

		if ((next == K_FOREVER) || (ticks < next)) {
			z_clock_set_timeout(ticks, idle);
		}
	}
}

int k_enable_sys_clock_always_on(void)
{
	int ret = !can_wait_forever;

	can_wait_forever = 0;
	return ret;
}

void k_disable_sys_clock_always_on(void)
{
	can_wait_forever = 1;
}

s64_t z_tick_get(void)
{
	u64_t t = 0U;

	LOCKED(&timeout_lock) {
		t = curr_tick + z_clock_elapsed();
	}
	return t;
}

u32_t z_tick_get_32(void)
{
#ifdef CONFIG_TICKLESS_KERNEL
	return (u32_t)z_tick_get();
#else
	return (u32_t)curr_tick;
#endif
}

u32_t _impl_k_uptime_get_32(void)
{
	return __ticks_to_ms(z_tick_get_32());
}

#ifdef CONFIG_USERSPACE
Z_SYSCALL_HANDLER(k_uptime_get_32)
{
	return _impl_k_uptime_get_32();
}
#endif

s64_t _impl_k_uptime_get(void)
{
	return __ticks_to_ms(z_tick_get());
}

#ifdef CONFIG_USERSPACE
Z_SYSCALL_HANDLER(k_uptime_get, ret_p)
{
	u64_t *ret = (u64_t *)ret_p;

	Z_OOPS(Z_SYSCALL_MEMORY_WRITE(ret, sizeof(*ret)));
	*ret = _impl_k_uptime_get();
	return 0;
}
#endif
