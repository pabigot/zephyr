/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <kernel.h>
#include <sys/onoff.h>
#include <syscall_handler.h>

#define CLIENT_NOTIFY_MASK (ONOFF_CLIENT_INTERNAL_BASE - 1U)

#define SERVICE_CONFIG_FLAGS	     \
	(ONOFF_SERVICE_START_WAITS   \
	 | ONOFF_SERVICE_STOP_WAITS  \
	 | ONOFF_SERVICE_RESET_WAITS)

#define SERVICE_STATE_OFF 0
#define SERVICE_STATE_ON ONOFF_SERVICE_INTERNAL_BASE
#define SERVICE_STATE_TRANSITION (ONOFF_SERVICE_INTERNAL_BASE << 1)
#define SERVICE_STATE_TO_ON (SERVICE_STATE_TRANSITION | SERVICE_STATE_ON)
#define SERVICE_STATE_TO_OFF (SERVICE_STATE_TRANSITION | SERVICE_STATE_OFF)

#define SERVICE_STATE_MASK (SERVICE_STATE_ON | SERVICE_STATE_TRANSITION)

static void set_service_state(struct onoff_service *srv,
			      u32_t state)
{
	srv->flags &= ~SERVICE_STATE_MASK;
	srv->flags |= (state & SERVICE_STATE_MASK);
}

static int validate_args(const struct onoff_service *srv,
			 struct onoff_client *cli,
			 bool *nowait)
{
	if ((srv == NULL) || (cli == NULL)) {
		return -EINVAL;
	}

	int rv = 0;
	u32_t mode = cli->flags;

	/* Reject unexpected flags. */
	if (mode != (cli->flags & CLIENT_NOTIFY_MASK)) {
		return -EINVAL;
	}

	/* Validate configuration based on mode */
	switch (mode) {
	case ONOFF_CLIENT_NOTIFY_NO_WAIT:
		*nowait = true;
		break;
	case ONOFF_CLIENT_NOTIFY_CALLBACK:
		if (cli->async.callback.handler == NULL) {
			rv = -EINVAL;
		}
		break;
#if CONFIG_POLL
	case ONOFF_CLIENT_NOTIFY_SIGNAL:
		if (cli->async.signal == NULL) {
			rv = -EINVAL;
		}
		break;
#endif /* CONFIG_POLL */
	default:
		rv = -EINVAL;
		break;
	}

	/* Clear the result here instead of in all callers. */
	if (rv == 0) {
		cli->result = 0;
	}

	return rv;
}

int onoff_service_init(struct onoff_service *srv,
		       onoff_service_transition_fn start,
		       onoff_service_transition_fn stop,
		       onoff_service_transition_fn reset,
		       u32_t flags)
{
	if ((flags & SERVICE_CONFIG_FLAGS) != flags) {
		return -EINVAL;
	}

	if ((start == NULL) || (stop == NULL)) {
		return -EINVAL;
	}

	*srv = (struct onoff_service)ONOFF_SERVICE_INITIALIZER(start, stop, reset, flags);

	return 0;
}

static void notify_one(struct onoff_service *srv,
		       struct onoff_client *cli,
		       int res)
{
	u32_t mode = cli->flags & CLIENT_NOTIFY_MASK;

	/* Store the result, and notify if requested. */
	cli->result = res;
	if (mode == ONOFF_CLIENT_NOTIFY_CALLBACK) {
		cli->async.callback.handler(srv, cli,
					    cli->async.callback.user_data, res);
#if CONFIG_POLL
	} else if (mode == ONOFF_CLIENT_NOTIFY_SIGNAL) {
		k_poll_signal_raise(cli->async.signal, res);
#endif /* CONFIG_POLL */
	} else if (mode != ONOFF_CLIENT_NOTIFY_NO_WAIT) {
		__ASSERT_NO_MSG(false);
	}
}

static void notify_all(struct onoff_service *srv,
		       sys_slist_t *list,
		       int res)
{
	while (!sys_slist_is_empty(list)) {
		sys_snode_t *node = sys_slist_get_not_empty(list);
		struct onoff_client *cli =
			CONTAINER_OF(node,
				     struct onoff_client,
				     node);

		notify_one(srv, cli, res);
	}
}

static void onoff_stop_notify(struct onoff_service *srv,
			      int res);

static void onoff_start_notify(struct onoff_service *srv,
			       int res)
{
	bool stop = false;
	k_spinlock_key_t key = k_spin_lock(&srv->lock);
	sys_slist_t clients = srv->clients;

	/* Can't have a queued releaser during start */
	__ASSERT_NO_MSG(srv->releaser == NULL);

	/* If the start failed log an error and leave the rest of the
	 * state in place for diagnostics.
	 *
	 * If the start succeeded record a reference for all clients
	 * and set the state to ON.  But if there are no clients,
	 * initiate a transition to off.
	 *
	 * In either case reset the client queue and notify all
	 * clients of operation completion.
	 */
	if (res < 0) {
		srv->flags &= ~SERVICE_STATE_TRANSITION;
		srv->flags |= ONOFF_SERVICE_HAS_ERROR;
	} else {
		sys_snode_t *node;

		SYS_SLIST_FOR_EACH_NODE(&clients, node) {
			srv->refs += 1;
		}
		set_service_state(srv, SERVICE_STATE_ON);
		stop = (srv->refs == 0);
		if ((stop || k_is_in_isr())
		    && ((srv->flags & ONOFF_SERVICE_STOP_WAITS) != 0U)) {
			srv->flags |= ONOFF_SERVICE_HAS_ERROR;
			stop = false;
		}
	}

	sys_slist_init(&srv->clients);

	k_spin_unlock(&srv->lock, key);

	notify_all(srv, &clients, res);
	if (stop) {
		srv->stop(srv, onoff_stop_notify);
	}
}

int z_impl_onoff_request(struct onoff_service *srv,
			 struct onoff_client *cli)
{
	bool add_client = false;        /* add client to pending list */
	bool start = false;             /* invoke start transition */
	bool notify = false;            /* do client notification */
	bool no_wait = false;
	int rv = validate_args(srv, cli, &no_wait);

	if (rv != 0) {
		return rv;
	}

	k_spinlock_key_t key = k_spin_lock(&srv->lock);

	if ((srv->flags & ONOFF_SERVICE_HAS_ERROR) != 0) {
		rv = -EIO;
		goto out;
	}

	if (srv->refs == UINT16_MAX) {
		rv = -EAGAIN;
		goto out;
	}

	u32_t state = srv->flags & SERVICE_STATE_MASK;

	switch (state) {
	case SERVICE_STATE_TO_OFF:
		/* Reject if no-wait because operation will not be
		 * completed when this call returns).
		 */
		if (no_wait) {
			rv = -EWOULDBLOCK;
			break;
		}

		/* Queue to start after release */
		__ASSERT_NO_MSG(srv->releaser != NULL);
		add_client = true;
		rv = 3;
		break;
	case SERVICE_STATE_OFF:
		/* Reject if in a no-wait context and start could
		 * wait.
		 */
		if ((no_wait || k_is_in_isr())
		    && ((srv->flags & ONOFF_SERVICE_START_WAITS) != 0U)) {
			rv = -EWOULDBLOCK;
			break;
		}

		/* Start with first request while off */
		__ASSERT_NO_MSG(srv->refs == 0);
		set_service_state(srv, SERVICE_STATE_TO_ON);
		start = true;
		add_client = true;
		rv = 2;
		break;
	case SERVICE_STATE_TO_ON:
		/* Reject if no-wait because operation will not be
		 * completed when this call returns.
		 */
		if (no_wait) {
			rv = -EWOULDBLOCK;
			break;
		}

		/* Already starting, just queue it */
		add_client = true;
		rv = 1;
		break;
	case SERVICE_STATE_ON:
		/* Just increment the reference count */
		notify = true;
		break;
	default:
		rv = -EINVAL;
		break;
	}

out:
	if (add_client) {
		sys_slist_append(&srv->clients, &cli->node);
	} else if (notify) {
		srv->refs += 1;
	}

	k_spin_unlock(&srv->lock, key);

	if (start) {
		__ASSERT_NO_MSG(srv->start != NULL);
		srv->start(srv, onoff_start_notify);
	} else if (notify) {
		notify_one(srv, cli, 0);
	}

	return rv;
}

static void onoff_stop_notify(struct onoff_service *srv,
			      int res)
{
	bool notify_clients = false;
	int client_res = res;
	bool start = false;
	k_spinlock_key_t key = k_spin_lock(&srv->lock);
	sys_slist_t clients = srv->clients;
	struct onoff_client *releaser = srv->releaser;

	/* If the stop operation failed log an error and leave the
	 * rest of the state in place.
	 *
	 * If it succeeded remove the last reference and transition to
	 * off.
	 *
	 * In either case remove the last reference, and notify all
	 * waiting clients of operation completion.
	 */
	if (res < 0) {
		srv->flags &= ~SERVICE_STATE_TRANSITION;
		srv->flags |= ONOFF_SERVICE_HAS_ERROR;
		notify_clients = true;
	} else if (sys_slist_is_empty(&clients)) {
		set_service_state(srv, SERVICE_STATE_OFF);
	} else if (k_is_in_isr()
		   && ((srv->flags & ONOFF_SERVICE_START_WAITS) != 0U)) {
		set_service_state(srv, SERVICE_STATE_OFF);
		notify_clients = true;
		client_res = -EWOULDBLOCK;
	} else {
		set_service_state(srv, SERVICE_STATE_TO_ON);
		start = true;
	}

	if (releaser) {
		srv->refs -= 1U;
		srv->releaser = NULL;
	}
	__ASSERT_NO_MSG(srv->refs == 0);

	/* Remove the clients if there was an error or a delayed start
	 * couldn't be initiated, because we're resolving their
	 * operation with an error.
	 */
	if (notify_clients) {
		sys_slist_init(&srv->clients);
	}

	k_spin_unlock(&srv->lock, key);

	/* Notify the releaser (if available).  If there was an error,
	 * notify any pending requests; otherwise if there are pending
	 * requests start the transition to ON.
	 */
	if (releaser) {
		notify_one(srv, releaser, res);
	}
	if (notify_clients) {
		notify_all(srv, &clients, client_res);
	} else if (start) {
		srv->start(srv, onoff_start_notify);
	}
}

int z_impl_onoff_release(struct onoff_service *srv,
			 struct onoff_client *cli)
{
	bool stop = false;      /* invoke stop transition */
	bool notify = false;    /* do client notification */
	bool no_wait = false;
	int rv = validate_args(srv, cli, &no_wait);

	if (rv != 0) {
		return rv;
	}

	k_spinlock_key_t key = k_spin_lock(&srv->lock);

	if ((srv->flags & ONOFF_SERVICE_HAS_ERROR) != 0) {
		rv = -EIO;
		goto out;
	}

	u32_t state = srv->flags & SERVICE_STATE_MASK;

	switch (state) {
	case SERVICE_STATE_ON:
		/* Stay on if release leaves a client. */
		if (srv->refs > 1U) {
			notify = true;
			rv = 1;
			break;
		}

		/* Reject if in no-wait context but stop could
		 * wait.
		 */
		if ((no_wait || k_is_in_isr())
		    && ((srv->flags & ONOFF_SERVICE_STOP_WAITS) != 0)) {
			rv = -EWOULDBLOCK;
			break;
		}

		stop = true;

		set_service_state(srv, SERVICE_STATE_TO_OFF);
		srv->releaser = cli;
		rv = 2;

		break;
	case SERVICE_STATE_TO_ON:
		rv = -EBUSY;
		break;
	case SERVICE_STATE_OFF:
	case SERVICE_STATE_TO_OFF:
		rv = -EALREADY;
		break;
	default:
		rv = -EINVAL;
	}

out:
	if (notify) {
		srv->refs -= 1U;
	}

	k_spin_unlock(&srv->lock, key);

	if (stop) {
		__ASSERT_NO_MSG(srv->stop != NULL);
		srv->stop(srv, onoff_stop_notify);
	} else if (notify) {
		notify_one(srv, cli, 0);
	}

	return rv;
}

static void onoff_reset_notify(struct onoff_service *srv,
			       int res)
{
	k_spinlock_key_t key = k_spin_lock(&srv->lock);
	sys_slist_t clients = srv->clients;

	/* If the reset failed clear the transition flag but otherwise
	 * leave the state unchanged.
	 *
	 * If it was successful clear the reference count and all
	 * flags except capability flags.
	 */
	if (res < 0) {
		srv->flags &= ~SERVICE_STATE_TRANSITION;
	} else {
		__ASSERT_NO_MSG(srv->refs == 0U);
		srv->refs = 0U;
		srv->flags &= SERVICE_CONFIG_FLAGS;
	}

	sys_slist_init(&srv->clients);

	k_spin_unlock(&srv->lock, key);

	notify_all(srv, &clients, res);
}

int z_impl_onoff_service_reset(struct onoff_service *srv,
			       struct onoff_client *cli)
{
	if (srv->reset == NULL) {
		return -ENOTSUP;
	}

	bool no_wait = false;
	bool reset = false;
	int rv = validate_args(srv, cli, &no_wait);

	if (rv != 0) {
		return rv;
	}

	/* Reject if in a no-wait context and reset could
	 * wait.
	 */
	if ((no_wait || k_is_in_isr())
	    && ((srv->flags & ONOFF_SERVICE_RESET_WAITS) != 0U)) {
		return -EWOULDBLOCK;
	}

	k_spinlock_key_t key = k_spin_lock(&srv->lock);

	if ((srv->flags & ONOFF_SERVICE_HAS_ERROR) == 0) {
		rv = -EINVAL;
		goto out;
	}

	if ((srv->flags & SERVICE_STATE_TRANSITION) == 0) {
		reset = true;
		srv->flags |= SERVICE_STATE_TRANSITION;
	}

out:
	if (rv >= 0) {
		sys_slist_append(&srv->clients, &cli->node);
	}

	k_spin_unlock(&srv->lock, key);

	if (reset) {
		srv->reset(srv, onoff_reset_notify);
	}

	return rv;
}

int z_impl_onoff_cancel(struct onoff_service *srv,
			struct onoff_client *cli)
{
	bool no_wait = false;
	int rv = validate_args(srv, cli, &no_wait);

	if (rv != 0) {
		return rv;
	}

	rv = -EALREADY;
	k_spinlock_key_t key = k_spin_lock(&srv->lock);

	if (sys_slist_find_and_remove(&srv->clients, &cli->node)) {
		/* If we're transitioning to ON, then removing the
		 * client eliminates the reference increment we would
		 * have done.  The start notification must initiate a
		 * stop if no requests are active when the start
		 * completes.
		 *
		 * If we're resetting no reference adjustment is
		 * necessary.
		 */
		rv = 0;
	} else if (srv->releaser == cli) {
		/* The release is in progress, and we're removing the
		 * indicator that there was an unreleased reference.
		 * Remove the reference now.
		 */
		rv = 0;
		__ASSERT_NO_MSG(srv->refs == 1);
		srv->refs = 0;
		srv->releaser = NULL;
	}

	k_spin_unlock(&srv->lock, key);

	if (rv == 0) {
		notify_one(srv, cli, -ECANCELED);
	}

	return rv;
}
