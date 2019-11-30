.. _resource_mgmt:

Resource Management
###################

There are various situations where it's necessary to coordinate resource
use at runtime among multiple clients.  This includes power rails,
clocks, other peripherals, and binary device power management. The
complexity of properly managing multiple consumers of a device in a
multithreaded system, especially when transitions may be asynchronous,
suggests that a shared implementation is desirable.

.. contents::
    :local:
    :depth: 2


On-Off Services
***************

An on-off service supports an arbitrary number of clients of a service
which has a binary state.  Example applications are power rails, clocks,
and binary device power management.

The service has the following properties:

* The stable states are OFF, ON, and ERROR.  The service always begins
  in the OFF state.  The service may also be in a transition to a given
  state.
* The core operations are request (add a dependency) and release (remove
  a dependency). The service manages the state based on these
  operations, which are processed in an atomic fashion.
* The service transitions from OFF to ON when first client request is
  received.
* The service transitions from ON to OFF when last client release is
  received.
* Operations are initiated by a function call referencing a specific
  service and given client notification data. The function call will
  succeed or fail. On success, the operation is guaranteed to be
  initiated, but whether the operation succeeds or fails is indicated by
  the client notification.  Operations can be initiated from thread or
  ISR context, with certain restrictions.
* Service configuration provides functions that implement transitions
  from off to on (start), from on to off (stop), and optionally from an
  error state to off (reset). Transitions may be flagged as potentially
  causing the caller to wait, which is used to support invocation from
  interrupt context and to prevent undesirable blocking in thread
  contexts.
* Operations may be queued while the system is transitioning.  An
  operation is immediately queued when the same operation is already in
  progress.  Requests to turn on may be queued while a transition to
  off is in progress; when the service has turned off it will be
  immediately turned on again.

Requests are reference counted, but not tracked. That means clients are
responsible for recording whether their requests were accepted, and for
initiating a release only if they have previously successfully completed
a request.  Improper use of the API can cause an active client to be
shut out, and the service does not maintain a record of specific clients
that have been granted a request.

Failures in executing a transition are recorded and inhibit further
requests or releases until the service is reset. Pending requests are
notified (and cancelled) when errors are discovered.

Transition operations are executed in one of the following modes:

* Non-blocking: if the request cannot be satisfied without blocking it
  fails with -EWOULDBLOCK
* Signals: A pointer to a k_poll_signal is a parameter, and it is raised
  when the transition completes. The operation completion code is stored
  as the signal value.
* Calls back: a function pointer is provided, and the function is
  invoked with the operation completion code.

Synchronous transition may be implemented by a caller (e.g. by using
k_poll() to block until the completion is signalled).

.. doxygengroup:: resource_mgmt_apis
   :project: Zephyr
