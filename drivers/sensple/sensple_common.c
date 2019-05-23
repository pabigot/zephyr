/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <drivers/sensple/impl.h>

void sensple_impl_setup(struct sensple_state *sp,
			struct device *dev,
			k_work_handler_t work_handler)
{
	*sp = (struct sensple_state){
		.dev = dev,
	};
	k_sem_init(&sp->sem, 1, 1);
	k_poll_signal_init(&sp->signal);
	k_delayed_work_init(&sp->delayed_work, work_handler);
}

int sensple_impl_animate(struct sensple_state *sp,
			 k_timeout_t delay)
{
	int rc = -EBUSY;
	k_spinlock_key_t key = k_spin_lock(&sp->lock);

	/* If there's a state-machine specified delay, we can't
	 * proceed until it expires. */
	if ((sp->state & SENSPLE_STATE_DELAYED) == 0) {
		if (!K_TIMEOUT_EQ(delay, K_NO_WAIT)) {
			sp->state |= SENSPLE_STATE_DELAYED;
		}
		rc = k_delayed_work_submit(&sp->delayed_work, delay);
	}
	k_spin_unlock(&sp->lock, key);

	return rc;
}
