/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 *
 * Second generation work queue implementation
 */

#include <kernel.h>
#include <kernel_structs.h>
#include <wait_q.h>
#include <spinlock.h>
#include <errno.h>
#include <sys/scheduler.h>
#include <sys/printk.h>

/* Lock to protect the internal state of all work items, and
 * pending_cancels.
 *
 * No queue locks may be held when this is taken, but a queue lock may
 * be taken while this is held.
 */
static struct k_spinlock lock;

/* Invoked by work thread */
static void handle_flush(struct k_work *work)
{
	struct z_work_flusher *flusher
		= CONTAINER_OF(work, struct z_work_flusher, work);

	k_sem_give(&flusher->sem);
}

static inline void init_flusher(struct z_work_flusher *flusher)
{
	k_sem_init(&flusher->sem, 0, 1);
	k_work_init(&flusher->work, handle_flush);
}

/* List of pending cancellations. */
static sys_slist_t pending_cancels;

/* Initialize a canceler record and add it to the list of pending
 * cancels.
 *
 * Invoked with work lock held.
 *
 * @param canceler the structure used to notify a waiting process.
 * @param work the work structure that is to be canceled
 */
static inline void init_work_cancel(struct z_work_canceller *canceler,
				    struct k_work *work)
{
	k_sem_init(&canceler->sem, 0, 1);
	canceler->work = work;
	sys_slist_append(&pending_cancels, &canceler->node);
}

/* Complete cancellation of a work item and unlock held lock.
 *
 * Invoked with work lock held.
 *
 * Invoked from a work queue thread.
 *
 * Releases work lock before returning.
 *
 * Reschedules.
 *
 * @param work the work structre that has completed cancellation
 * @param key the key to the held lock
 */
static void _finalize_cancel_and_unlock(struct k_work *work,
					k_spinlock_key_t key)
{
	struct z_work_canceller *wc, *tmp;
	sys_slist_t cancels;

	/* Clear this first, so released high-priority threads don't
	 * see it when doing things.
	 */
	atomic_clear_bit(&work->flags, K_WORK_CANCELING_BIT);

	/* Move all pending cancels associated with this work item to
	 * another list, under lock.  (We can't signal the release
	 * while the lock is held because it's a reschedule point,
	 * which would release the lock.)
	 */
	sys_slist_init(&cancels);
	SYS_SLIST_FOR_EACH_CONTAINER_SAFE(&pending_cancels, wc, tmp, node) {
		if (wc->work == work) {
			sys_slist_remove(&pending_cancels, NULL, &wc->node);
			k_sem_give(&wc->sem);
		}
	}

	/* Release the lock.  No need to yield here, the work thread
	 * does that.
	 */
	k_spin_unlock(&lock, key);
}

void k_work_init_base(struct k_work *work,
		  k_work_handler_t handler)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(handler != 0);

	*work = (struct k_work)Z_WORK_INITIALIZER(handler);
}

/* Add a flusher work item to the queue.
 *
 * Invoked with work lock and queue lock both held.
 *
 * Caller must notify queue of pending work.
 *
 * @param queue queue on which a work item may appear.
 * @param work the work item that is either queued or running on @p
 * queue
 * @param flusher an uninitialized/unused flusher object
 */
static void _queue_flusher(struct k_work_q *queue,
			   struct k_work *work,
			   struct z_work_flusher *flusher)
{
	bool in_list = false;
	struct k_work *wn;

	/* Determine whether the work item is still queued. */
	SYS_SLIST_FOR_EACH_CONTAINER(&queue->pending, wn, node) {
		if (wn == work) {
			in_list = true;
			break;
		}
	}

	init_flusher(flusher);
	if (in_list) {
		sys_slist_insert(&queue->pending, &work->node,
				 &flusher->work.node);
	} else {
		sys_slist_prepend(&queue->pending, &flusher->work.node);
	}
}

/* Try to remove a work item from the given queue.
 *
 * Invoked with work lock held.
 *
 * Locks queue temporarily.
 *
 * @param queue the queue from which the work should be removed
 * @param work work that may be on the queue
 */
static inline void queue_remove(struct k_work_q *queue,
				struct k_work *work)
{
	if (atomic_test_and_clear_bit(&work->flags, K_WORK_QUEUED_BIT)) {
		k_spinlock_key_t key = k_spin_lock(&queue->lock);
		(void)sys_slist_find_and_remove(&queue->pending, &work->node);
		k_spin_unlock(&queue->lock, key);
	}
}

/* Potentially notify a queue that it needs to look for pending work.
 *
 * Invoked with queue lock held, possibly also work lock held.
 *
 * This may make the work queue thread ready, but as the queue lock is held it
 * will not be a reschedule point.  Callers should yield after the lock is
 * released where appropriate (generally if this returns true).
 *
 * @param queue to be notified.  If this is null no notification is required.
 *
 * @return true iff the queue was notified and woken, i.e. a reschedule is
 * pending.
 */
static inline bool _notify_queue(struct k_work_q *queue)
{
	bool rv = false;

	if (queue != NULL) {
		rv = k_sched_wake(&queue->notifyq, 0, NULL);
	}

	return rv;
}

/* Submit an work item to a queue if queue state allows new work.
 *
 * Submission is rejected if no queue is provided, or if the queue is
 * draining and the work isn't being submitted from the queue's
 * thread (chained submission).
 *
 * Invoked with work lock held.
 * Locks and unlocks queue.
 * Conditionally notifies queue.
 *
 * @param queue the queue to which work should be submitted.  This may
 * be null, in which case the submission will fail.
 *
 * @param work to be submitted
 *
 * @retval 1 if successfully queued
 * @retval -EINVAL if no queue is provided
 * @retval -ENODEV if the queue is not started
 * @retval -EBUSY if the submission was rejected (draining, plugged)
 */
static inline int _queue_submit(struct k_work_q *queue,
				struct k_work *work)
{
	if (queue == NULL) {
		return -EINVAL;
	}

	int ret = -EBUSY;
	bool chained = (_current == &queue->thread) && !k_is_in_isr();
	k_spinlock_key_t key = k_spin_lock(&queue->lock);
	bool draining = atomic_test_bit(&queue->flags, K_WORK_QUEUE_DRAIN_BIT);
	bool plugged = atomic_test_bit(&queue->flags, K_WORK_QUEUE_PLUGGED_BIT);

	/* Test for acceptability, in priority order:
	 *
	 * * -ENODEV if the queue isn't running.
	 * * -EBUSY if draining and not chained
	 * * -EBUSY if plugged and not draining
	 * * otherwise OK
	 */
	if (!atomic_test_bit(&queue->flags, K_WORK_QUEUE_STARTED_BIT)) {
		ret = -ENODEV;
	} else if (draining && !chained) {
		ret = -EBUSY;
	} else if (plugged && !draining) {
		ret = -EBUSY;
	} else {
		sys_slist_append(&queue->pending, &work->node);
		ret = 1;
		(void)_notify_queue(queue);
	}

	k_spin_unlock(&queue->lock, key);

	return ret;
}

/* Attempt to submit work to a queue.
 *
 * The submission can fail if:
 * * the work is cancelling,
 * * no candidate queue can be identified;
 * * the candidate queue rejects the submission.
 *
 * Invoked with work lock held.
 * Locks and unlocks queue (indirectly)
 * Conditionally notifies queue.
 *
 * @param work the work structure to be submitted

 * @param queuep pointer to a queue reference.  On input this should
 * dereference to the proposed queue (which may be null); after completion it
 * will be null if the work was not submitted or if submitted will reference
 * the queue it was submitted to.  That may or may not be the queue provided
 * on input.
 *
 * @retval 0 if already queued
 * @retval 1 if unqueued and not running
 * @retval 2 if unqueued and running
 * @retval -EBUSY if canceling or submission was rejected by queue
 * @retval -EINVAL if no queue is provided
 * @retval -ENODEV if the queue is not started
 */
static int _submit_to_queue(struct k_work *work,
			    struct k_work_q **queuep)
{
	int ret = 0;

	if (atomic_test_bit(&work->flags, K_WORK_CANCELING_BIT)) {
		/* Disallowed */
		ret = -EBUSY;
	} else if (!atomic_test_bit(&work->flags, K_WORK_QUEUED_BIT)) {
		/* Not currently queued */
		ret = 1;

		/* If no queue specified resubmit to last queue.
		 */
		if (*queuep == NULL) {
			*queuep = work->queue;
		}

		/* If the work is currently running we have to use the
		 * queue it's running on to prevent handler
		 * re-entrancy.
		 */
		if (atomic_test_bit(&work->flags, K_WORK_RUNNING_BIT)) {
			__ASSERT_NO_MSG(work->queue != NULL);
			*queuep = work->queue;
			ret = 2;
		}

		int rc = _queue_submit(*queuep, work);

		if (rc < 0) {
			ret = rc;
		} else {
			atomic_set_bit(&work->flags, K_WORK_QUEUED_BIT);
			work->queue = *queuep;
		}
	} else {
		/* Already queued, do nothing. */
	}

	if (ret <= 0) {
		*queuep = NULL;
	}

	return ret;
}

int k_work_submit_to_queue(struct k_work_q *queue,
			    struct k_work *work)
{
	__ASSERT_NO_MSG(work != NULL);

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = _submit_to_queue(work, &queue);

	k_spin_unlock(&lock, key);

	/* If we changed the queue contents (as indicated by a positive ret)
	 * the queue thread may now be ready, but we missed the reschedule
	 * point because the lock was held.  If this is being invoked by a
	 * preemptible thread then yield.
	 */
	if ((ret > 0) && (k_is_preempt_thread() != 0)) {
		k_yield();
	}


	return ret;
}

/* Flush the work item if necessary.
 *
 * Flushing is necessary only if the work is either queued or running.
 *
 * Invoked with work lock held by key.
 * Releases work lock before returning.
 * Sleeps.
 *
 * @param work the work item that is to be flushed
 * @param flusher state used to synchronize the flush
 * @param key the key under which the work lock is held.
 *
 * @retval true if work is queued or running; also waits in this case.
 * @false otherwise
 */
static bool _work_flush_and_unlock(struct k_work *work,
				   struct z_work_flusher *flusher,
				   k_spinlock_key_t key)
{
	struct k_work_q *queue = NULL;
	bool need_flush = (atomic_get(&work->flags)
			   & (K_WORK_QUEUED | K_WORK_RUNNING)) != 0;

	if (need_flush) {
		queue = work->queue;
		__ASSERT_NO_MSG(queue != NULL);

		k_spinlock_key_t qkey = k_spin_lock(&queue->lock);

		_queue_flusher(queue, work, flusher);
		_notify_queue(queue);

		k_spin_unlock(&queue->lock, qkey);
	}

	k_spin_unlock(&lock, key);

	if (need_flush) {
		k_sem_take(&flusher->sem, K_FOREVER);
	}

	return need_flush;
}

bool k_work_flush(struct k_work *work,
		  struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(!atomic_test_bit(&work->flags, K_WORK_DELAYABLE_BIT));
	__ASSERT_NO_MSG(!k_is_in_isr());
	__ASSERT_NO_MSG(sync != NULL);

	return _work_flush_and_unlock(work, &sync->flusher, k_spin_lock(&lock));
}

/* Execute the non-waiting steps necessary to cancel a work item.
 *
 * Invoked with work lock held.
 *
 * @param work the work item to be canceled.
 *
 * @retval true if we need to wait for the work item to finish canceling
 * @retval false if the work item is idle
 *
 * @return k_busy_wait() captured under lock
 */
static int _cancel_async(struct k_work *work)
{
	/* If we haven't already started canceling, do it now. */
	if (!atomic_test_bit(&work->flags, K_WORK_CANCELING_BIT)) {
		/* Remove it from the queue, if it's queued. */
		queue_remove(work->queue, work);
	}

	/* If it's still busy after it's been dequeued, then flag it
	 * as canceling.
	 */
	int ret = k_work_busy(work);

	if (ret != 0) {
		atomic_set_bit(&work->flags, K_WORK_CANCELING_BIT);
		ret = k_work_busy(work);
	}

	return ret;
}

/* Complete cancellation necessary, release work lock, and wait if
 * necessary.
 *
 * Invoked with work lock held by key.
 * Releases lock.
 * Sleeps.
 *
 * @param work work that is being canceled
 * @param canceller state used to synchronize the cancellation
 * @param key used by work lock
 *
 * @return true iff the work was still active on entry (waited)
 * @return false if work was idle on entry (no wait required)
 */
static bool _cancel_sync_and_unlock(struct k_work *work,
				    struct z_work_canceller *canceller,
				    k_spinlock_key_t key)
{
	bool ret = false;

	/* If something's still running then we have to wait for
	 * completion, which is indicated when finish_cancel() gets
	 * invoked.
	 */
	if (atomic_test_bit(&work->flags, K_WORK_CANCELING_BIT)) {
		init_work_cancel(canceller, work);
		ret = true;

		k_spin_unlock(&lock, key);

		k_sem_take(&canceller->sem, K_FOREVER);
	} else {
		k_spin_unlock(&lock, key);
	}

	return ret;
}

int k_work_cancel(struct k_work *work)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(!atomic_test_bit(&work->flags, K_WORK_DELAYABLE_BIT));

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = _cancel_async(work);

	k_spin_unlock(&lock, key);

	return ret;
}

bool k_work_cancel_sync(struct k_work *work,
			struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(work != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!atomic_test_bit(&work->flags, K_WORK_DELAYABLE_BIT));
	__ASSERT_NO_MSG(!k_is_in_isr());

	k_spinlock_key_t key = k_spin_lock(&lock);

	(void)_cancel_async(work);

	return _cancel_sync_and_unlock(work, &sync->canceller, key) != 0;
}

/* Work has been dequeued and is about to be invoked by the work
 * thread.
 *
 * If the work is being canceled the cancellation will be completed
 * here, and the caller told not to use the work item.
 *
 * Invoked by work queue thread.
 * Takes and releases lock.
 * Reschedules via _finalize_cancel_and_unlock
 *
 * @param work work that is changing state
 * @param queue queue that is running work
 *
 * @retval true if work is to be run by the work thread
 * @retval false if it has been canceled and should not be run
 */
static inline bool work_set_running(struct k_work *work,
				    struct k_work_q *queue)
{
	bool ret = false;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Allow the work to be queued again. */
	atomic_clear_bit(&work->flags, K_WORK_QUEUED_BIT);

	/* Normally we indicate that the work is being processed by
	 * setting RUNNING.  However, something may have initiated
	 * cancellation between when the work thread pulled this off
	 * its queue and this claimed the work lock.  If that happened
	 * we complete the cancellation now and tell the work thread
	 * not to do anything.
	 */
	ret = !atomic_test_bit(&work->flags, K_WORK_CANCELING_BIT);
	if (ret) {
		/* Not cancelling: mark running and go */
		atomic_set_bit(&work->flags, K_WORK_RUNNING_BIT);
		k_spin_unlock(&lock, key);
	} else {
		/* Caught the item before being invoked; complete the
		 * cancellation now.
		 */
		_finalize_cancel_and_unlock(work, key);
	}

	return ret;
}

/* Work handler has been called and is about to go idle.
 *
 * If the work is being canceled this will notify anything waiting
 * for the cancellation.
 *
 * Invoked by work queue thread.
 * Takes and releases lock.
 * Reschedules via _finalize_cancel_and_unlock
 *
 * @param work work that is in running state
 */
static inline void work_clear_running(struct k_work *work)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Clear running */
	atomic_clear_bit(&work->flags, K_WORK_RUNNING_BIT);

	if (atomic_test_bit(&work->flags, K_WORK_CANCELING_BIT)) {
		_finalize_cancel_and_unlock(work, key);
	} else {
		k_spin_unlock(&lock, key);
	}
}

/* Loop executed by a work queue thread.
 *
 * @param workq_ptr pointer to the work queue structure
 */
static void work_queue_main(void *workq_ptr, void *p2, void *p3)
{
	struct k_work_q *queue = (struct k_work_q *)workq_ptr;

	while (true) {
		sys_snode_t *node;
		struct k_work *work = NULL;
		k_spinlock_key_t key = k_spin_lock(&queue->lock);

		/* Clear the record of processing any previous work, and check
		 * for new work.
		 */
		node = sys_slist_get(&queue->pending);
		if (node != NULL) {
			/* Mark that there's some work active that's
			 * not on the pending list.
			 */
			atomic_set_bit(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
			work = CONTAINER_OF(node, struct k_work, node);
		} else if (atomic_test_and_clear_bit(&queue->flags,
						     K_WORK_QUEUE_DRAIN_BIT)) {
			/* Not busy and draining: move threads waiting for
			 * drain to ready state.  The held spinlock inhibits
			 * reschedule; released threads get their chance when
			 * this loops back to wait on notify, or the thread
			 * exits.
			 *
			 * We don't touch K_WORK_QUEUE_PLUGGABLE, so getting
			 * here doesn't mean that the queue will allow new
			 * submissions.
			 */
			(void)k_sched_wake_all(&queue->drainq, 1, NULL);
		}

		if (work == NULL) {
			/* Nothing's had a chance to add work since we took
			 * the queue lock, and We didn't find work nor got
			 * asked to stop.  Just go to sleep: when something
			 * happens the work thread will be woken and we can
			 * check again.
			 */

			(void)k_sched_wait(&queue->lock, key, &queue->notifyq,
					   K_FOREVER, NULL);
			continue;
		}

		k_spin_unlock(&queue->lock, key);

		if (work != NULL) {
			k_work_handler_t handler = work->handler;

			__ASSERT_NO_MSG(handler != 0);

			if (work_set_running(work, queue)) {
				handler(work);
				work_clear_running(work);
			}

			/* No longer referencing the work, so we can clear the
			 * BUSY flag while we yield to prevent starving other
			 * threads.
			 */
			key = k_spin_lock(&queue->lock);
			atomic_clear_bit(&queue->flags, K_WORK_QUEUE_BUSY_BIT);
			k_spin_unlock(&queue->lock, key);

			k_yield();
		}
	}
}

void k_work_queue_start(struct k_work_q *queue,
			 k_thread_stack_t *stack,
			 size_t stack_size,
			 int prio,
			 const char *name)
{
	__ASSERT_NO_MSG(queue);
	__ASSERT_NO_MSG(stack);
	__ASSERT_NO_MSG(!atomic_test_bit(&queue->flags,
					 K_WORK_QUEUE_STARTED_BIT));

	sys_slist_init(&queue->pending);
	z_waitq_init(&queue->notifyq);
	z_waitq_init(&queue->drainq);

	/* It hasn't actually been started yet, but all the state is in place
	 * so we can submit things and once the thread gets control it's ready
	 * to roll.
	 */
	atomic_set(&queue->flags, K_WORK_QUEUE_STARTED);

	(void)k_thread_create(&queue->thread, stack, stack_size,
			      work_queue_main, queue, NULL, NULL,
			      prio, 0, K_NO_WAIT);

	if (name) {
		k_thread_name_set(&queue->thread, name);
	}
}

int k_work_queue_drain(struct k_work_q *queue,
		       bool plug)
{
	__ASSERT_NO_MSG(queue);
	__ASSERT_NO_MSG(!k_is_in_isr());

	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&queue->lock);

	if (((atomic_get(&queue->flags)
	      & (K_WORK_QUEUE_BUSY | K_WORK_QUEUE_DRAIN)) != 0U)
	    || plug
	    || !sys_slist_is_empty(&queue->pending)) {
		atomic_set_bit(&queue->flags, K_WORK_QUEUE_DRAIN_BIT);
		if (plug) {
			atomic_set_bit(&queue->flags, K_WORK_QUEUE_PLUGGED_BIT);
		}

		_notify_queue(queue);
		ret = k_sched_wait(&queue->lock, key, &queue->drainq,
				   K_FOREVER, NULL);
	} else {
		k_spin_unlock(&queue->lock, key);
	}

	return ret;
}

int k_work_queue_unplug(struct k_work_q *queue)
{
	__ASSERT_NO_MSG(queue);

	int ret = -EALREADY;
	k_spinlock_key_t key = k_spin_lock(&queue->lock);

	if (atomic_test_and_clear_bit(&queue->flags,
				      K_WORK_QUEUE_PLUGGED_BIT)) {
		ret = 0;
	}

	k_spin_unlock(&queue->lock, key);

	return ret;
}

#ifdef CONFIG_SYS_CLOCK_EXISTS

/* Timeout handler for delayable work.
 *
 * Invoked by timeout infrastructure.
 * Takes and releases work lock.
 * Conditionally reschedules.
 */
static void work_timeout(struct _timeout *to)
{
	struct k_work_delayable *dw
		= CONTAINER_OF(to, struct k_work_delayable, timeout);
	struct k_work *wp = &dw->work;
	k_spinlock_key_t key = k_spin_lock(&lock);
	struct k_work_q *queue = NULL;

	/* If the work is still marked delayed (should be) then clear that
	 * state and submit it to the queue.  If successful the queue will be
	 * notified of new work at the next reschedule point.
	 *
	 * If not successful there is no notification that the work has been
	 * abandoned.  Sorry.
	 */
	if (atomic_test_and_clear_bit(&wp->flags, K_WORK_DELAYED_BIT)) {
		queue = dw->queue;
		(void)_submit_to_queue(wp, &queue);
	}

	k_spin_unlock(&lock, key);
}

void k_work_init_delayable(struct k_work_delayable *dwork,
			    k_work_handler_t handler)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(handler != 0);

	*dwork = (struct k_work_delayable){
		.work = {
			.handler = handler,
			.flags = K_WORK_DELAYABLE,
		},
	};
	z_init_timeout(&dwork->timeout);
	(void)work_timeout;
}

/* Attempt to schedule a work item for future (maybe immediate)
 * submission.
 *
 * Invoked with work lock held.
 *
 * See also _submit_to_queue(), which implements this for a no-wait
 * delay.
 *
 * Invoked with work lock held.
 *
 * @param queuep pointer to a pointer to a queue.  On input this
 * should dereference to the proposed queue (which may be null); after
 * completion it will be null if the work was not submitted or if
 * submitted will reference the queue it was submitted to.  That may
 * or may not be the queue provided on input.
 *
 * @param dwork the delayed work structure
 *
 * @param delay the delay to use before scheduling.
 *
 * @retval from _submit_to_queue() if delay is K_NO_WAIT; otherwise
 * @retval 1 to indicate successfully scheduled.
 */
int _schedule_to_queue(struct k_work_q **queuep,
		       struct k_work_delayable *dwork,
		       k_timeout_t delay)
{
	int ret = 1;
	struct k_work *work = &dwork->work;

	if (K_TIMEOUT_EQ(delay, K_NO_WAIT)) {
		return _submit_to_queue(work, queuep);
	}

	atomic_set_bit(&work->flags, K_WORK_DELAYED_BIT);
	dwork->queue = *queuep;
#ifdef CONFIG_LEGACY_TIMEOUT_API
	delay = _TICK_ALIGN + k_ms_to_ticks_ceil32(delay);
#endif
	/* Add timeout */
	z_add_timeout(&dwork->timeout, work_timeout, delay);

	return ret;
}

/* Unschedule delayable work.
 *
 * If the work is delayed, cancel the timeout and clear the delayed
 * flag.
 *
 * Invoked with work lock held.
 *
 * @param dwork pointer to delayable work structure.
 *
 * @return true iff work had been delayed
 */
static inline bool _unschedule(struct k_work_delayable *dwork)
{
	bool ret = false;
	struct k_work *work = &dwork->work;

	/* If scheduled, try to cancel. */
	if (atomic_test_and_clear_bit(&work->flags, K_WORK_DELAYED_BIT)) {
		z_abort_timeout(&dwork->timeout);
		ret = true;
	}

	return ret;
}

/* Full cancellation of a delayable work item.
 *
 * Unschedules the delayed part then delegates to standard work
 * cancellation.
 *
 * Invoked with work lock held.
 *
 * @param dwork delayable work item
 *
 * @return k_work_busy() flags
 */
static int _cancel_delayable_async(struct k_work_delayable *dwork)
{
	(void)_unschedule(dwork);

	return _cancel_async(&dwork->work);
}

int k_work_schedule_for_queue(struct k_work_q *queue,
			       struct k_work_delayable *dwork,
			       k_timeout_t delay)
{
	__ASSERT_NO_MSG(dwork != NULL);

	struct k_work *work = &dwork->work;
	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Schedule the work item if it's idle. */
	if (k_work_busy(work) == 0U) {
		ret = _schedule_to_queue(&queue, dwork, delay);
	} else {
		queue = NULL;
	}

	k_spin_unlock(&lock, key);

	return ret;
}

int k_work_reschedule_for_queue(struct k_work_q *queue,
				 struct k_work_delayable *dwork,
				 k_timeout_t delay)
{
	__ASSERT_NO_MSG(dwork != NULL);

	int ret = 0;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* Remove any active scheduling. */
	(void)_unschedule(dwork);

	/* Schedule the work item with the new parameters. */
	ret = _schedule_to_queue(&queue, dwork, delay);

	k_spin_unlock(&lock, key);

	return ret;
}

int k_work_cancel_delayable(struct k_work_delayable *dwork)
{
	__ASSERT_NO_MSG(dwork != NULL);

	k_spinlock_key_t key = k_spin_lock(&lock);
	int ret = _cancel_delayable_async(dwork);

	k_spin_unlock(&lock, key);
	return ret;
}

bool k_work_cancel_delayable_sync(struct k_work_delayable *dwork,
				  struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!k_is_in_isr());

	k_spinlock_key_t key = k_spin_lock(&lock);
	(void)_cancel_delayable_async(dwork);

	return _cancel_sync_and_unlock(&dwork->work, &sync->canceller, key);
}

bool k_work_flush_delayable(struct k_work_delayable *dwork,
			    struct k_work_sync *sync)
{
	__ASSERT_NO_MSG(dwork != NULL);
	__ASSERT_NO_MSG(sync != NULL);
	__ASSERT_NO_MSG(!k_is_in_isr());

	struct k_work *work = &dwork->work;
	k_spinlock_key_t key = k_spin_lock(&lock);

	/* If it's idle release the lock and return immediately. */
	if (k_work_busy(work) == 0U) {
		k_spin_unlock(&lock, key);
		return false;
	}

	/* If unscheduling did something then submit it.  Ignore a
	 * failed submission (e.g. when cancelling).
	 */
	if (_unschedule(dwork)) {
		struct k_work_q *queue = dwork->queue;

		(void)_submit_to_queue(work, &queue);
	}

	/* Wait for it to finish */
	return _work_flush_and_unlock(work, &sync->flusher, key);
}

#endif /* CONFIG_SYS_CLOCK_EXISTS */
