/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ztest.h>
#include <sys/onoff.h>

static struct onoff_client nowait_cli;

static int callback_res;
static void *callback_ud;
static void callback(struct onoff_service *srv,
		     struct onoff_client *cli,
		     void *ud,
		     int res)
{
	callback_ud = ud;
	callback_res = res;
}

static void init_notify_sig(struct onoff_client *cli,
			    struct k_poll_signal *sig,
			    size_t n)
{
	int rc;

	for (size_t i = 0; i < n; ++i) {
		k_poll_signal_init(&sig[i]);
		rc = onoff_client_init_signal(&cli[i], &sig[i]);
		zassert_equal(rc, 0,
			      "cli signal");
	}
}

struct transit_state {
	const char *tag;
	bool async;
	int retval;
	onoff_service_notify_fn notify;
	struct onoff_service *srv;
};

static void reset_transit_state(struct transit_state *tsp)
{
	tsp->async = false;
	tsp->retval = 0;
	tsp->notify = NULL;
	tsp->srv = NULL;
}

static void run_transit(struct onoff_service *srv,
			onoff_service_notify_fn notify,
			struct transit_state *tsp)
{
	if (tsp->async) {
		TC_PRINT("%s async\n", tsp->tag);
		tsp->notify = notify;
		tsp->srv = srv;
	} else {
		TC_PRINT("%s notify %d\n", tsp->tag, tsp->retval);
		notify(srv, tsp->retval);
	}
}

static void notify(struct transit_state *tsp)
{
	TC_PRINT("%s settle %u\n", tsp->tag, tsp->retval);
	tsp->notify(tsp->srv, tsp->retval);
	tsp->notify = NULL;
	tsp->srv = NULL;
}

static struct k_sem isr_sync;
static struct k_timer isr_timer;

static void isr_notify(struct k_timer *timer)
{
	struct transit_state *tsp =
		(struct transit_state *)timer->user_data;

	TC_PRINT("ISR NOTIFY %s %d\n", tsp->tag, k_is_in_isr());
	notify(tsp);
	k_sem_give(&isr_sync);
}

static struct transit_state start_state = {
	.tag = "start",
};
static void start(struct onoff_service *srv,
		  onoff_service_notify_fn notify)
{
	run_transit(srv, notify, &start_state);
}

static struct transit_state stop_state = {
	.tag = "stop",
};
static void stop(struct onoff_service *srv,
		 onoff_service_notify_fn notify)
{
	run_transit(srv, notify, &stop_state);
}

static struct transit_state reset_state = {
	.tag = "reset",
};
static void reset(struct onoff_service *srv,
		  onoff_service_notify_fn notify)
{
	run_transit(srv, notify, &reset_state);
}

static void clear_transit(void)
{
	callback_res = 0;
	reset_transit_state(&start_state);
	reset_transit_state(&stop_state);
	reset_transit_state(&reset_state);
}

static void test_service_init_validation(void)
{
	int rc;
	struct onoff_service srv;

	clear_transit();

	rc = onoff_service_init(NULL, NULL, NULL, NULL, 0);
	zassert_equal(rc, -EINVAL,
		      "init null srv %d", rc);

	rc = onoff_service_init(&srv, NULL, NULL, NULL, 0);
	zassert_equal(rc, -EINVAL,
		      "init null transit %d", rc);

	rc = onoff_service_init(&srv, start, NULL, NULL, 0);
	zassert_equal(rc, -EINVAL,
		      "init null stop %d", rc);

	rc = onoff_service_init(&srv, NULL, stop, NULL, 0);
	zassert_equal(rc, -EINVAL,
		      "init null start %d", rc);

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_INTERNAL_BASE);
	zassert_equal(rc, -EINVAL,
		      "init bad flags %d", rc);

	u32_t flags = ONOFF_SERVICE_START_WAITS;

	memset(&srv, 0xA5, sizeof(srv));
	zassert_false(sys_slist_is_empty(&srv.clients),
		      "slist empty");

	rc = onoff_service_init(&srv, start, stop, reset, flags);
	zassert_equal(rc, 0,
		      "init good %d", rc);
	zassert_equal(srv.start, start,
		      "init start mismatch");
	zassert_equal(srv.stop, stop,
		      "init stop mismatch");
	zassert_equal(srv.reset, reset,
		      "init reset mismatch");
	zassert_equal(srv.flags, ONOFF_SERVICE_START_WAITS,
		      "init flags mismatch");
	zassert_equal(srv.refs, 0,
		      "init refs mismatch");
	zassert_true(sys_slist_is_empty(&srv.clients),
		     "init slist empty");
}

static void test_client_init_validation(void)
{
	int rc;
	struct onoff_client cli;

	clear_transit();

	memset(&cli, 0xA5, sizeof(cli));
	rc = onoff_client_init_no_wait(NULL);
	zassert_equal(rc, -EINVAL,
		      "cli no_wait cli null");
	rc = onoff_client_init_no_wait(&cli);
	zassert_equal(z_snode_next_peek(&cli.node), NULL,
		      "cli node mismatch");
	zassert_equal(cli.flags, ONOFF_CLIENT_NOTIFY_NO_WAIT,
		      "cli no_wait flags mismatch");

	struct k_poll_signal sig;

	memset(&cli, 0xA5, sizeof(cli));
	rc = onoff_client_init_signal(NULL, NULL);
	zassert_equal(rc, -EINVAL,
		      "cli signal cli null");
	rc = onoff_client_init_signal(&cli, NULL);
	zassert_equal(rc, -EINVAL,
		      "cli signal null");
	rc = onoff_client_init_signal(&cli, &sig);
	zassert_equal(z_snode_next_peek(&cli.node), NULL,
		      "cli signal node");
	zassert_equal(cli.flags, ONOFF_CLIENT_NOTIFY_SIGNAL,
		      "cli signal flags mismatch");
	zassert_equal(cli.async.signal, &sig,
		      "cli signal async mismatch");

	memset(&cli, 0xA5, sizeof(cli));
	rc = onoff_client_init_callback(NULL, NULL, NULL);
	zassert_equal(rc, -EINVAL,
		      "cli callback cli null");
	rc = onoff_client_init_callback(&cli, NULL, NULL);
	zassert_equal(rc, -EINVAL,
		      "cli callback null");

	memset(&cli, 0xA5, sizeof(cli));
	rc = onoff_client_init_callback(&cli, callback, &sig);
	zassert_equal(z_snode_next_peek(&cli.node), NULL,
		      "cli callback node");
	zassert_equal(cli.flags, ONOFF_CLIENT_NOTIFY_CALLBACK,
		      "cli callback flags mismatch");
	zassert_equal(cli.async.callback.handler, callback,
		      "cli callback handler mismatch");
	zassert_equal(cli.async.callback.user_data, &sig,
		      "cli callback user_data mismatch");
}

static void test_reset(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;
	unsigned int signalled = 0;
	int result = 0;

	clear_transit();

	rc = onoff_service_init(&srv, start, stop, NULL, 0);
	zassert_equal(rc, 0,
		      "service init");
	rc = onoff_service_reset(&srv, &cli);
	zassert_equal(rc, -ENOTSUP,
		      "reset: %d", rc);

	rc = onoff_service_init(&srv, start, stop, reset, 0);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_service_reset(&srv, NULL);
	zassert_equal(rc, -EINVAL,
		      "rst no cli");

	rc = onoff_request(&srv, &nowait_cli);
	zassert_true(rc > 0,
		     "req ok");
	zassert_equal(srv.refs, 1U,
		      "reset req refs: %u", srv.refs);

	init_notify_sig(&cli, &sig, 1);

	zassert_false(onoff_service_has_error(&srv),
		      "has error");
	reset_state.retval = 57;
	rc = onoff_service_reset(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "reset: %d", rc);

	stop_state.retval = -23;
	rc = onoff_release(&srv, &cli);
	zassert_equal(rc, 2,
		      "rel trigger: %d", rc);
	zassert_equal(srv.refs, 0U,
		      "reset req refs: %u", srv.refs);
	zassert_true(onoff_service_has_error(&srv),
		     "has error");
	zassert_equal(cli.result, stop_state.retval,
		      "cli result");
	signalled = 0;
	result = -1;
	k_poll_signal_check(&sig, &signalled, &result);
	zassert_true(signalled != 0,
		     "signalled");
	zassert_equal(result, stop_state.retval,
		      "result");
	k_poll_signal_reset(&sig);

	reset_state.retval = -59;
	rc = onoff_service_reset(&srv, &cli);
	zassert_equal(rc, 0U,
		      "reset: %d", rc);
	zassert_equal(cli.result, reset_state.retval,
		      "reset result");
	zassert_equal(srv.refs, 0U,
		      "reset req refs: %u", srv.refs);
	zassert_true(onoff_service_has_error(&srv),
		     "has error");

	reset_state.retval = 62;
	rc = onoff_service_reset(&srv, &cli);
	zassert_equal(rc, 0U,
		      "reset: %d", rc);
	zassert_equal(cli.result, reset_state.retval,
		      "reset result");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	signalled = 0;
	result = -1;
	k_poll_signal_check(&sig, &signalled, &result);
	zassert_true(signalled != 0,
		     "signalled");
	zassert_equal(result, reset_state.retval,
		      "result");

	zassert_equal(srv.refs, 0U,
		      "reset req refs: %u", srv.refs);
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	rc = onoff_service_init(&srv, start, stop, reset,
				ONOFF_SERVICE_RESET_WAITS);
	zassert_equal(rc, 0,
		      "service init");
	start_state.retval = -23;
	zassert_false(onoff_service_has_error(&srv),
		      "has error");
	rc = onoff_request(&srv, &nowait_cli);
	zassert_true(onoff_service_has_error(&srv),
		     "has error");

	rc = onoff_service_reset(&srv, &nowait_cli);
	zassert_equal(rc, -EWOULDBLOCK,
		      "reset nowait async: %d", rc);
}

static void test_request(void)
{
	int rc;
	struct onoff_service srv;

	clear_transit();

	rc = onoff_service_init(&srv, start, stop, reset, 0);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(&srv, &nowait_cli);
	zassert_true(rc >= 0,
		     "reset req: %d", rc);
	zassert_equal(srv.refs, 1U,
		      "reset req refs: %u", srv.refs);
	zassert_equal(nowait_cli.result, 0,
		      "reset req result: %d", nowait_cli.result);

	/* Can't reset when no error present. */
	rc = onoff_service_reset(&srv, &nowait_cli);
	zassert_equal(rc, -EINVAL,
		      "reset null cli");

	/* Reference overflow produces -EAGAIN */
	u32_t refs = srv.refs;

	srv.refs = UINT16_MAX;
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, -EAGAIN,
		      "reset req overflow: %d", rc);
	srv.refs = refs;

	/* Force an error. */
	stop_state.retval = -32;
	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, 2,
		      "error release");
	zassert_equal(nowait_cli.result, stop_state.retval,
		      "error retval");
	zassert_true(onoff_service_has_error(&srv),
		     "has error");

	/* Can't request when error present. */
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, -EIO,
		      "req with error");

	/* Can't release when error present. */
	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, -EIO,
		      "rel with error");

	struct k_poll_signal sig;
	struct onoff_client cli;

	init_notify_sig(&cli, &sig, 1);

	/* Clear the error */
	rc = onoff_service_reset(&srv, &cli);
	zassert_equal(rc, 0,
		      "reset");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	/* Error on start */
	start_state.retval = -12;
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, 2,
		      "req with error");
	zassert_equal(nowait_cli.result, start_state.retval,
		      "req with error");
	zassert_true(onoff_service_has_error(&srv),
		     "has error");

	/* Clear the error */
	rc = onoff_service_reset(&srv, &nowait_cli);
	zassert_equal(rc, 0,
		      "reset");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	/* Diagnose a no-wait delayed start */
	rc = onoff_service_init(&srv, start, stop, reset,
				ONOFF_SERVICE_START_WAITS);
	zassert_equal(rc, 0,
		      "service init");
	start_state.async = true;
	start_state.retval = 12;

	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, -EWOULDBLOCK,
		      "req blocked");
}

static void test_validate_args(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;

	clear_transit();

	/* The internal validate_args is invoked from request,
	 * release, and reset; test it through the request API.
	 */

	rc = onoff_service_init(&srv, start, stop, NULL, 0);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(NULL, NULL);
	zassert_equal(rc, -EINVAL,
		      "validate req null srv");

	rc = onoff_release(NULL, NULL);
	zassert_equal(rc, -EINVAL,
		      "validate rel null srv");

	rc = onoff_release(&srv, NULL);
	zassert_equal(rc, -EINVAL,
		      "validate req null cli");

	rc = onoff_request(&srv, NULL);
	zassert_equal(rc, -EINVAL,
		      "validate rel null cli");

	rc = onoff_request(&srv, &nowait_cli);
	zassert_true(rc > 0,
		     "trigger to on");

	memset(&cli, 0xA3, sizeof(cli));
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "validate req cli flags");

	onoff_client_init_no_wait(&cli);
	cli.flags = ONOFF_CLIENT_INTERNAL_BASE - 1U;
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "validate req cli mode");

	init_notify_sig(&cli, &sig, 1);
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, 0,
		      "validate req cli signal: %d", rc);
	cli.async.signal = NULL;
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "validate req cli signal null");

	rc = onoff_client_init_callback(&cli, callback, NULL);
	zassert_equal(rc, 0,
		      "cli callback");
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, 0,
		      "validate req cli callback");

	cli.async.callback.handler = NULL;
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "validate req cli callback null");

	memset(&cli, 0x3C, sizeof(cli));
	rc = onoff_request(&srv, &cli);
	zassert_equal(rc, -EINVAL,
		      "validate req cli notify mode");
}

static void test_sync(void)
{
	int rc;
	struct onoff_service srv;

	clear_transit();

	rc = onoff_service_init(&srv, start, stop, reset, 0);
	zassert_equal(rc, 0,
		      "service init");

	/* WHITEBOX: request that triggers on returns positive */
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, 2,    /* WHITEBOX starting request */
		      "req ok");
	zassert_equal(srv.refs, 1U,
		      "reset req refs: %u", srv.refs);

	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, 0,    /* WHITEBOX on request */
		      "req ok");
	zassert_equal(srv.refs, 2U,
		      "reset req refs: %u", srv.refs);

	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, 1,    /* WHITEBOX non-stopping release */
		      "rel ok");
	zassert_equal(srv.refs, 1U,
		      "reset rel refs: %u", srv.refs);

	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, 2,    /* WHITEBOX stopping release*/
		      "rel ok: %d", rc);
	zassert_equal(srv.refs, 0U,
		      "reset rel refs: %u", srv.refs);

	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, -EALREADY,
		      "rel noent");
}

static void test_async(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig[2];
	struct onoff_client cli[2];
	unsigned int signalled = 0;
	int result = 0;

	clear_transit();
	start_state.async = true;
	start_state.retval = 23;
	stop_state.async = true;
	stop_state.retval = 17;

	init_notify_sig(cli, sig, ARRAY_SIZE(cli));

	rc = onoff_service_init(&srv, start, stop, reset,
				ONOFF_SERVICE_START_WAITS
				| ONOFF_SERVICE_STOP_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	/* WHITEBOX: request that triggers on returns positive */
	rc = onoff_request(&srv, &cli[0]);
	zassert_equal(rc, 2,    /* WHITEBOX starting request */
		      "req ok");
	k_poll_signal_check(&sig[0], &signalled, &result);
	zassert_equal((bool)signalled, false,
		      "cli signalled");
	zassert_equal(srv.refs, 0U,
		      "reset req refs: %u", srv.refs);
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, -EWOULDBLOCK,
		      "nowait req");

	/* Off while on pending is not supported */
	rc = onoff_release(&srv, &cli[1]);
	zassert_equal(rc, -EBUSY,
		      "rel in to-on");

	/* Second request is delayed for first. */
	rc = onoff_request(&srv, &cli[1]);
	zassert_equal(rc, 1,    /* WHITEBOX pending request */
		      "req ok");
	k_poll_signal_check(&sig[1], &signalled, &result);
	zassert_equal((bool)signalled, false,
		      "cli signalled");
	zassert_equal(srv.refs, 0U,
		      "reset req refs: %u", srv.refs);

	/* Complete the transition. */
	notify(&start_state);
	k_poll_signal_check(&sig[0], &signalled, &result);
	k_poll_signal_reset(&sig[0]);
	zassert_equal((bool)signalled, true,
		      "cli signalled");
	zassert_equal(result, start_state.retval,
		      "cli result");
	k_poll_signal_check(&sig[1], &signalled, &result);
	k_poll_signal_reset(&sig[1]);
	zassert_equal((bool)signalled, true,
		      "cli2 signalled");
	zassert_equal(result, start_state.retval,
		      "cli2 result");
	zassert_equal(srv.refs, 2U,
		      "reset req refs: %u", srv.refs);

	/* Non-final release decrements refs and completes. */
	rc = onoff_release(&srv, &cli[0]);
	zassert_equal(rc, 1,    /* WHITEBOX non-stopping release */
		      "rel ok");
	zassert_equal(srv.refs, 1U,
		      "reset rel refs: %u", srv.refs);
	k_poll_signal_check(&sig[0], &signalled, &result);
	k_poll_signal_reset(&sig[0]);
	zassert_equal((bool)signalled, true,
		      "cli signalled");
	zassert_equal(result, 0,
		      "cli result");

	/* Final release cannot be no-wait */
	rc = onoff_release(&srv, &nowait_cli);
	zassert_equal(rc, -EWOULDBLOCK,
		      "no-wait async rel");

	/* Final async release holds until notify */
	rc = onoff_release(&srv, &cli[1]);
	zassert_equal(rc, 2,    /* WHITEBOX stopping release */
		      "rel ok: %d", rc);
	zassert_equal(srv.refs, 1U,
		      "reset rel refs: %u", srv.refs);

	/* Redundant release in to-off */
	rc = onoff_release(&srv, &cli[0]);
	zassert_equal(rc, -EALREADY,
		      "rel to-off: %d", rc);
	zassert_equal(srv.refs, 1U,
		      "reset rel refs: %u", srv.refs);
	k_poll_signal_check(&sig[0], &signalled, &result);
	zassert_equal((bool)signalled, false,
		      "cli signalled");

	/* Request when turning off is queued */
	rc = onoff_request(&srv, &cli[0]);
	zassert_equal(rc, 3,    /* WHITEBOX stopping request */
		      "req in to-off");

	/* Finalize release, queues start */
	zassert_true(start_state.notify == NULL,
		     "start not invoked");
	notify(&stop_state);
	zassert_false(start_state.notify == NULL,
		      "start invoked");
	zassert_equal(srv.refs, 0U,
		      "reset rel refs: %u", srv.refs);
	k_poll_signal_check(&sig[1], &signalled, &result);
	k_poll_signal_reset(&sig[1]);
	zassert_equal((bool)signalled, true,
		      "cli signalled");
	zassert_equal(result, stop_state.retval,
		      "cli result");

	/* Release when starting is an error */
	rc = onoff_release(&srv, &cli[0]);
	zassert_equal(rc, -EBUSY,
		      "rel to-off: %d", rc);

	/* Finalize queued start, gets us to on */
	cli[0].result = 1 + start_state.retval;
	notify(&start_state);
	zassert_equal(cli[0].result, start_state.retval,
		      "start notified");
	zassert_equal(srv.refs, 1U,
		      "reset rel refs: %u", srv.refs);
}

static void test_half_sync(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;

	clear_transit();
	start_state.retval = 23;
	stop_state.async = true;
	stop_state.retval = 17;

	init_notify_sig(&cli, &sig, 1);

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_STOP_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	/* Test that a no-wait request that would normally be always
	 * allowed by a synchronous start is still blocked if there's
	 * a pending asynchronous stop.
	 */
	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, 2,
		      "req0");
	zassert_equal(srv.refs, 1U,
		      "active");

	zassert_true(stop_state.notify == NULL,
		     "not stopping");
	rc = onoff_release(&srv, &cli);
	zassert_equal(rc, 2,
		      "rel0");
	zassert_equal(srv.refs, 1U,
		      "active");
	zassert_false(stop_state.notify == NULL,
		      "stop pending");

	rc = onoff_request(&srv, &nowait_cli);
	zassert_equal(rc, -EWOULDBLOCK,
		      "req0");
}

static void test_cancel_request_waits(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;

	clear_transit();
	start_state.async = true;
	start_state.retval = 14;
	stop_state.async = true;
	stop_state.retval = 31;

	init_notify_sig(&cli, &sig, 1);

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_START_WAITS
				| ONOFF_SERVICE_STOP_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(&srv, &cli);
	zassert_true(rc > 0,
		"request pending");
	zassert_false(start_state.notify == NULL,
		      "start pending");

	rc = onoff_cancel(&srv, &cli);
	zassert_equal(rc, 0,
		      "cancel failed");
	zassert_equal(cli.result, -ECANCELED,
		      "cancel notified");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	notify(&start_state);
	zassert_true(onoff_service_has_error(&srv),
		     "has error");
}

static void test_cancel_request_ok(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;

	clear_transit();
	start_state.async = true;
	start_state.retval = 14;
	stop_state.retval = 31;

	init_notify_sig(&cli, &sig, 1);

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_START_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(&srv, &cli);
	zassert_true(rc > 0,
		"request pending");
	zassert_false(start_state.notify == NULL,
		      "start pending");

	rc = onoff_cancel(&srv, &cli);
	zassert_equal(rc, 0,
		      "cancel failed");
	zassert_equal(cli.result, -ECANCELED,
		      "cancel notified");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	zassert_equal(srv.refs, 0,
		      "refs empty");
	notify(&start_state);
	zassert_equal(srv.refs, 0,
		      "refs empty");
	zassert_false(onoff_service_has_error(&srv),
		      "has error");
}

static void test_blocked_restart(void)
{
	int rc;
	struct onoff_service srv;
	unsigned int signalled = 0;
	int result;
	struct k_poll_signal sig[2];
	struct onoff_client cli[2];

	clear_transit();
	start_state.async = true;
	start_state.retval = 14;
	stop_state.async = true;
	stop_state.retval = 31;

	init_notify_sig(cli, sig, ARRAY_SIZE(cli));

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_START_WAITS
				| ONOFF_SERVICE_STOP_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(&srv, &cli[0]);
	zassert_true(rc > 0,
		     "started");
	zassert_false(start_state.notify == NULL,
		      "stop pending");
	notify(&start_state);

	result = -start_state.retval;
	k_poll_signal_check(&sig[0], &signalled, &result);
	zassert_true(signalled != 0,
		     "signalled");
	zassert_equal(result, start_state.retval,
		      "result");
	k_poll_signal_reset(&sig[0]);

	start_state.async = true;
	rc = onoff_release(&srv, &cli[0]);
	zassert_true(rc > 0,
		     "stop initiated");
	zassert_false(stop_state.notify == NULL,
		      "stop pending");
	rc = onoff_request(&srv, &cli[1]);
	zassert_true(rc > 0,
		     "start pending");

	result = start_state.retval + stop_state.retval;
	k_poll_signal_check(&sig[0], &signalled, &result);
	zassert_true(signalled == 0,
		     "stop signalled");
	k_poll_signal_check(&sig[1], &signalled, &result);
	zassert_true(signalled == 0,
		     "restart signalled");

	isr_timer.user_data = &stop_state;
	k_timer_start(&isr_timer, K_MSEC(1), K_NO_WAIT);
	rc = k_sem_take(&isr_sync, K_MSEC(10));
	zassert_equal(rc, 0,
		      "isr sync");

	/* Fail-to-restart is not an error */
	zassert_false(onoff_service_has_error(&srv),
		      "has error");

	k_poll_signal_check(&sig[0], &signalled, &result);
	zassert_false(signalled == 0,
		     "stop pending");
	zassert_equal(result, stop_state.retval,
		      "stop succeeded");

	k_poll_signal_check(&sig[1], &signalled, &result);
	zassert_false(signalled == 0,
		     "restart pending");
	zassert_equal(result, -EWOULDBLOCK,
		      "restart failed");
}

static void test_cancel_release(void)
{
	int rc;
	struct onoff_service srv;
	struct k_poll_signal sig;
	struct onoff_client cli;
	unsigned int signalled = 0;
	int result;

	clear_transit();
	start_state.retval = 16;
	stop_state.async = true;
	stop_state.retval = 94;

	init_notify_sig(&cli, &sig, 1);

	rc = onoff_service_init(&srv, start, stop, NULL,
				ONOFF_SERVICE_STOP_WAITS);
	zassert_equal(rc, 0,
		      "service init");

	rc = onoff_request(&srv, &cli);
	zassert_true(rc > 0,
		"request done");
	result = -start_state.retval;
	k_poll_signal_check(&sig, &signalled, &result);
	zassert_true(signalled != 0,
		     "signalled");
	zassert_equal(result, start_state.retval,
		      "result");
	k_poll_signal_reset(&sig);

	rc = onoff_release(&srv, &cli);
	zassert_true(rc > 0,
		     "release pending");
	zassert_false(stop_state.notify == NULL,
		      "release pending");

	rc = onoff_cancel(&srv, &cli);
	zassert_equal(rc, 0,
		      "cancel succeeded");

	result = 0;
	k_poll_signal_check(&sig, &signalled, &result);
	zassert_true(signalled != 0,
		     "signalled");
	zassert_equal(result, -ECANCELED,
		      "result");

	zassert_false(onoff_service_has_error(&srv),
		      "has error");
	notify(&stop_state);
	zassert_false(onoff_service_has_error(&srv),
		      "has error");
}

void test_main(void)
{
	k_sem_init(&isr_sync, 0, 1);
	k_timer_init(&isr_timer, isr_notify, NULL);
	onoff_client_init_no_wait(&nowait_cli);

	ztest_test_suite(onoff_api,
			 ztest_unit_test(test_service_init_validation),
			 ztest_unit_test(test_client_init_validation),
			 ztest_unit_test(test_validate_args),
			 ztest_unit_test(test_reset),
			 ztest_unit_test(test_request),
			 ztest_unit_test(test_sync),
			 ztest_unit_test(test_async),
			 ztest_unit_test(test_half_sync),
			 ztest_unit_test(test_cancel_request_waits),
			 ztest_unit_test(test_cancel_request_ok),
			 ztest_unit_test(test_blocked_restart),
			 ztest_unit_test(test_cancel_release));
	ztest_run_test_suite(onoff_api);
}
