/*
 * Copyright (c) 2019 Peter Bigot Consulting, LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ONOFF_H_
#define ZEPHYR_INCLUDE_SYS_ONOFF_H_

#include <kernel.h>
#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup resource_mgmt_apis Resource Management APIs
 * @ingroup kernel_apis
 * @{
 */

/**
 * @brief Flag fields used to specify on-off service behavior.
 */
enum onoff_service_flags {
	/**
	 * @brief Flag passed to onoff_service_init().
	 *
	 * When provided this indicates the start transition function
	 * may cause the calling thread to wait.  This blocks attempts
	 * to initiate a transition from interrupt context or when the
	 * request specifies @ref ONOFF_CLIENT_NOTIFY_NO_WAIT.
	 */
	ONOFF_SERVICE_START_WAITS = BIT(0),

	/**
	 * @brief Flag passed to onoff_service_init().
	 *
	 * As with @ref ONOFF_SERVICE_START_WAITS but describing the
	 * stop transition function.
	 */
	ONOFF_SERVICE_STOP_WAITS = BIT(1),

	/**
	 * @brief Flag passed to onoff_service_init().
	 *
	 * As with @ref ONOFF_SERVICE_START_WAITS but describing the
	 * reset transition function.
	 */
	ONOFF_SERVICE_RESET_WAITS = BIT(2),

	/* Internal use. */
	ONOFF_SERVICE_HAS_ERROR = BIT(3),

	/* This and higher bits reserved for internal use. */
	ONOFF_SERVICE_INTERNAL_BASE = BIT(4),
};

/* Forward declaration */
struct onoff_service;

/**
 * @brief Signature used to notify an on-off service that a transition
 * has completed.
 *
 * The functions provided for this type are non-suspendable and
 * isr-callable.
 *
 * @param srv the service for which transition was requested.
 *
 * @param res the result of the transition.  This shall be zero on
 * success, or a negative error code.  If an error is indicate the
 * service shall enter an error state.
 */
typedef void (*onoff_service_notify_fn)(struct onoff_service *srv,
					int res);

/**
 * @brief Signature used by service implementations to effect a
 * transition.
 *
 * Service definitions use two function pointers of this type to be
 * notified that a transition is required, and a third optional one to
 * reset service state.
 *
 * The start function will only be called from the off state.
 *
 * The stop function will only be called from the on state.
 *
 * The reset function may only be called when
 * onoff_service_has_error() returns true.
 *
 * Functions will be invoked from interrupt context only if the
 * corresponding isr-capable flag is specified in the service
 * configuration.
 *
 * @param srv the service for which transition was requested.
 *
 * @param notify the function to be invoked when the transition has
 * completed.  The callee shall capture this parameter to notify on
 * completion of asynchronous transitions.  If the transition is not
 * asynchronous, notify shall be invoked before the transition
 * function returns.
 */
typedef void (*onoff_service_transition_fn)(struct onoff_service *srv,
					    onoff_service_notify_fn notify);

/**
 * @brief State associated with an on-off service.
 *
 * No fields in this structure are intended for use by service
 * providers or clients.  The state is to be initialized once, using
 * onoff_service_init(), when the service provider is initialized.
 * In case of error it may be reset through the
 * onoff_service_reset() API.
 */
struct onoff_service {
	/* List of clients waiting for completion of reset or
	 * transition to on.
	 */
	sys_slist_t clients;

	/* Function to invoke to transition the service to on. */
	onoff_service_transition_fn start;

	/* Function to invoke to transition the service to off. */
	onoff_service_transition_fn stop;

	/* Function to force the service state to reset, where
	 * supported.
	 */
	onoff_service_transition_fn reset;

	/* Mutex protection for flags, clients, releaser, and refs. */
	struct k_spinlock lock;

	/* Client to be informed when transition to off completes. */
	struct onoff_client *releaser;

	/* Flags identifying the service state. */
	u16_t flags;

	/* Number of active clients for the service. */
	u16_t refs;
};

#define ONOFF_SERVICE_INITIALIZER(_start, _stop, _reset, _flags) {	\
	.start = _start,						\
	.stop = _stop,							\
	.reset = _reset,						\
	.flags = _flags,						\
}

/**
 * @brief Initialize an on-off service to off state.
 *
 * This function must be invoked exactly once per service instance, by
 * the infrastructure that provides the service, and before any other
 * on-off service API is invoked on the service.
 *
 * This function should never be invoked by clients of an on-off service.
 *
 * @param srv the service definition object to be initialized.
 *
 * @param start the function used to (initiate a) transition from off
 * to on.  This must not be null.  Include @ref ONOFF_SERVICE_START_WAITS as
 * appropriate in flags.
 *
 * @param stop the function used to (initiate a) transition from on to
 * to off.  This must not be null.  Include @ref ONOFF_SERVICE_STOP_WAITS
 * as appropriate in flags.
 *
 * @param reset the function used to clear errors and force the
 * service to an off state.  Pass null if the service cannot or need
 * not be reset.  (Services where a transition operation can complete
 * with an error notification should support the reset operation.)
 * Include @ref ONOFF_SERVICE_RESET_WAITS as appropriate in flags.
 *
 * @param flags any or all of the flags mentioned above,
 * e.g. @ref ONOFF_SERVICE_START_WAITS.  Use of other flags produces an
 * error.
 *
 * @retval 0 on success
 * @retval -EINVAL if start, stop, or flags are invalid
 */
int onoff_service_init(struct onoff_service *srv,
		       onoff_service_transition_fn start,
		       onoff_service_transition_fn stop,
		       onoff_service_transition_fn reset,
		       u32_t flags);

/**
 * @brief Flag fields used to specify on-off client behavior.
 *
 * These flags control whether calls to onoff_service_request() and
 * onoff_service_release() are synchronous or asynchronous, and for
 * asynchronous operations how the operation result is communicated to
 * the client.
 */
enum onoff_client_flags {
	/**
	 * @brief Require synchronous notification.
	 *
	 * Indicates operations are to proceed only if the initiating
	 * function can be executed in a synchronous path: i.e. the
	 * operation will complete before the function returns, though
	 * the operation may fail.
	 *
	 * See onoff_client_init_no_wait().
	 */
	ONOFF_CLIENT_NOTIFY_NO_WAIT = 0,

	/**
	 * @brief Require notification through @ref k_poll signal
	 *
	 * See onoff_client_init_signal().
	 */
#if CONFIG_POLL
	ONOFF_CLIENT_NOTIFY_SIGNAL = 1,
#endif /* CONFIG_POLL */

	/**
	 * @brief Require notification through a user-provided callback.
	 *
	 * See onoff_client_init_callback().
	 */
	ONOFF_CLIENT_NOTIFY_CALLBACK = 2,

	/* This and higher bits reserved for internal use. */
	ONOFF_CLIENT_INTERNAL_BASE = BIT(2),
};

/* Forward declaration */
struct onoff_client;

/**
 * @brief Signature used to notify an on-off service client of the
 * completion of an operation.
 *
 * These functions may be invoked from any context including
 * pre-kernel, ISR, or cooperative or pre-emptible threads.
 * Compatible functions must be isr-callable and non-suspendable.
 *
 * @param srv the service for which the operation was initiated.
 *
 * @param cli the client structure passed to the function that
 * initiated the operation.
 *
 * @param user_data user data provided when the client structure was
 * initialized with onoff_client_init_callback().  A null pointer is
 * provided for other notification methods.
 *
 * @param res the result of the operation.  Expected values are
 * service-specific, but the value shall be non-negative if the
 * operation succeeded, and negative if the operation failed.
 */
typedef void (*onoff_client_callback)(struct onoff_service *srv,
				      struct onoff_client *cli,
				      void *user_data,
				      int res);

/**
 * @brief State associated with a client of an on-off service.
 *
 * Objects of this type are allocated by a client, which must use an
 * initialization function (e.g. onoff_client_init_signal()) to
 * configure them.
 *
 * Control of the object content transfers to the service provider
 * when a pointer to the object is passed to any on-off service
 * function.  While the service provider controls the object the
 * client must not change any object fields.  Control reverts to the
 * client:
 * * if the call to the service API returns an error; or
 * * if the call to the service API succeeds for a no-wait operation;
 *   otherwise
 * * when operation completion is posted (signalled or callback
 *   invoked).
 *
 * Only the result field is intended for direct use by clients, and it
 * is available for inspection only after control reverts to the
 * client.
 */
struct onoff_client {
	/* Links the client into the set of waiting service users. */
	sys_snode_t node;

	union async {
		/* Pointer to signal used to notify client.
		 *
		 * The signal value corresponds to the res parameter
		 * of onoff_client_callback.
		 */
#if CONFIG_POLL
		struct k_poll_signal *signal;
#endif /* CONFIG_POLL */

		/* Handler and argument for callback notification. */
		struct callback {
			onoff_client_callback handler;
			void *user_data;
		} callback;
	} async;

	/**
	 * @brief The result of the operation.
	 *
	 * This is the value that was (or would be) passed to the
	 * async infrastructure.  This field is the sole record of
	 * success or failure for no-wait synchronous operations.
	 */
	int result;

	/* Flags recording client state. */
	u16_t flags;
};

/**
 * @brief Initialize an on-off client for non-blocking operations.
 *
 * Clients that use this initialization will receive `-EWOULDBLOCK`
 * errors as the result of calls to onoff_request() and
 * onoff_release() if the performing the operation might result in
 * the thread blocking.
 *
 * @param cli pointer to the client state object.
 *
 * @retval 0 on success
 * @retval -EINVAL if cli is null
 */
static inline int onoff_client_init_no_wait(struct onoff_client *cli)
{
	if (cli == NULL) {
		return -EINVAL;
	}

	*cli = (struct onoff_client){
		.flags = ONOFF_CLIENT_NOTIFY_NO_WAIT,
	};

	return 0;
}

/**
 * @brief Initialize an on-off client to notify with a signal.
 *
 * Clients that use this initialization will by notified of the
 * completion of operations submitted through onoff_request() and
 * onoff_release() through the provided signal.
 *
 * @param cli pointer to the client state object.
 *
 * @param sigp pointer to the signal to use for notification.  The
 * value must not be null.  The signal must be reset before the client
 * object is passed to the on-off service API.
 *
 * @retval 0 on success
 * @retval -EINVAL if cli or sigp is null
 */
#if CONFIG_POLL
static inline int onoff_client_init_signal(struct onoff_client *cli,
					   struct k_poll_signal *sigp)
{
	if ((cli == NULL) || (sigp == NULL)) {
		return -EINVAL;
	}

	*cli = (struct onoff_client){
		.async = {
			.signal = sigp,
		},
		.flags = ONOFF_CLIENT_NOTIFY_SIGNAL,
	};

	return 0;
}
#endif /* CONFIG_POLL */

/**
 * @brief Initialize an on-off client to notify with a callback.
 *
 * Clients that use this initialization will by notified of the
 * completion of operations submitted through on-off service API
 * through the provided callback.  Note that callbacks may be invoked
 * from various contexts; see onoff_client_callback.
 *
 * @param cli pointer to the client state object.
 *
 * @param handler a function pointer to use for notification.
 *
 * @param user_data an opaque pointer passed to the handler to provide
 * additional context.
 *
 * @retval 0 on success
 * @retval -EINVAL if cli or cb is null
 */
static inline int onoff_client_init_callback(struct onoff_client *cli,
					     onoff_client_callback handler,
					     void *user_data)
{
	if ((cli == NULL) || (handler == NULL)) {
		return -EINVAL;
	}

	*cli = (struct onoff_client){
		.async = {
			.callback = {
				.handler = handler,
				.user_data = user_data,
			},
		},
		.flags = ONOFF_CLIENT_NOTIFY_CALLBACK,
	};

	return 0;
}

/**
 * @brief Request a reservation to use an on-off service.
 *
 * The return value indicates the success or failure of an attempt to
 * initiate an operation to request the resource be made available.
 * If initiation of the operation succeeds the result of the request
 * operation is provided through the configured client notification
 * method, possibly before this call returns.
 *
 * Note that the call to this function may succeed in a case where the
 * actual request fails.  Always check the operation completion
 * result.
 *
 * This function is isr-capable, and may suspend unless client
 * notification specifies no-wait.
 *
 * @param srv the service that will be used.
 *
 * @param cli a non-null pointer to client state providing
 * instructions on synchronous expectations and how to notify the
 * client when the request completes.  Behavior is undefined if client
 * passes a pointer object associated with an incomplete service
 * operation.
 *
 * @retval Non-negative on successful (initiation of) request
 * @retval -EIO if service has recorded an an error
 * @retval -EINVAL if the parameters are invalid
 * @retval -EAGAIN if the reference count would overflow
 * @retval -EWOULDBLOCK if a non-blocking request was made and could
 *         not be satisfied
 */
__syscall int onoff_request(struct onoff_service *srv,
			    struct onoff_client *cli);

/**
 * @brief Release a reserved use of an on-off service.
 *
 * The return value indicates the success or failure of an attempt to
 * initiate an operation to release of the resource.  If initiation of
 * the operation succeeds the result of the release operation itself
 * is provided through the configured client notification method,
 * possibly before this call returns.
 *
 * Note that the call to this function may succeed in a case where the
 * actual release fails.  Always check the operation completion
 * result.
 *
 * This function is isr-capable, and may suspend unless client
 * notification specifies no-wait.
 *
 * @param srv the service that will be used.
 *
 * @param cli a non-null pointer to client state providing
 * instructions on how to notify the client when release completes.
 * Behavior is undefined if cli references an object associated with
 * an incomplete service operation.
 *
 * @retval Non-negative on successful (initiation of) release
 * @retval -EINVAL if the parameters are invalid
 * @retval -EIO if service has recorded an an error
 * @retval -EWOULDBLOCK if a non-blocking request was made and
 *         could not be satisfied without potentially blocking.
 * @retval -EALREADY if the service is already off or transitioning
 *         to off
 * @retval -EBUSY if the service is transitioning to on
 */
__syscall int onoff_release(struct onoff_service *srv,
			    struct onoff_client *cli);
/**
 * @internal
 */
static inline bool z_impl_onoff_service_has_error(const struct onoff_service *srv)
{
	return (srv->flags & ONOFF_SERVICE_HAS_ERROR) != 0;
}

/**
 * @brief Test whether an on-off service has recorded an error.
 *
 * This function can be used to determine whether the service has
 * recorded an error.  Errors may be cleared by invoking
 * onoff_service_reset().
 *
 * @return true if and only if the service has an uncleared error.
 */
__syscall bool onoff_service_has_error(const struct onoff_service *srv);

/**
 * @brief Clear errors on an on-off service and reset it to its off
 * state.
 *
 * A service can only be reset when it is in an error state as
 * indicated by onoff_service_has_error().
 *
 * The return value indicates the success or failure of an attempt to
 * initiate an operation to reset the resource.  If initiation of the
 * operation succeeds the result of result of the reset operation
 * itself is provided through the configured client notification
 * method, possibly before this call returns.  Multiple clients may
 * request a reset; all are notified when it is complete.
 *
 * Note that the call to this function may succeed in a case where the
 * actual reset fails.  Always check the operation completion result.
 *
 * This function is isr-capable, and may suspend unless client
 * notification specifies no-wait.
 *
 * @note Due to the conditions on state transition all incomplete
 * asynchronous operations will have been informed of the error when
 * it occurred.  There need be no concern about dangling requests left
 * after a reset completes.
 *
 * @param srv the service to be reset.
 *
 * @param cli pointer to client state, including instructions on how
 * to notify the client when reset completes.  Behavior is undefined
 * if cli references an object associated with an incomplete service
 * operation.
 *
 * @retval 0 on success
 * @retval -ENOTSUP if reset is not supported
 * @retval -EINVAL if the parameters are invalid, or if the service
 * does not have a recorded error
 */
__syscall int onoff_service_reset(struct onoff_service *srv,
				  struct onoff_client *cli);

/**
 * @brief Cancel an in-progress client operation.
 *
 * It may be that a client has initiated an operation but needs to
 * shut down before the operation has completed.  For example, when a
 * request was made and the need is no longer present.  Issuing a
 * second operation from a client when the first operation has not
 * completed is not supported by the service state machine because
 * some resource state is updated only when the transition completes.
 *
 * The service can be instructed to release the client notification
 * structure by invoking this function.
 *
 * Be aware that any transition that was initiated on behalf of the
 * client will continue to progress.  Successful cancellation of a
 * request may cause the service to turn off; successful cancellation
 * of a release may leave the service off.  If the cancellation fails
 * the application must check the operation result to determine what
 * to do next.
 *
 * @param srv the service for which an operation is to be cancelled.
 *
 * @param cli a pointer to the same client state that was provided
 * when the operation to be cancelled was issued.  If the cancellation
 * is successful the client will be notified with a result of
 * `-ECANCELED` before the function returns.
 *
 * @retval 0 if the cancellation was completed before the client could
 * be notified.  The client will be notified through cli with an
 * operation completion of `-ECANCELED`.
 * @retval -EINVAL if the parameters are invalid.
 * @retval -EALREADY if cli was not a record of an uncompleted
 * notification at the time the cancellation was processed.  This
 * likely indicates that the operation and client notification had
 * already completed.
 */
__syscall int onoff_cancel(struct onoff_service *srv,
			   struct onoff_client *cli);

/** @} */

#include <syscalls/onoff.h>

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ONOFF_H_ */
