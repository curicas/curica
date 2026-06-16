/**
 * @file event_loop.h
 * @brief Async Event Loop — Timers, Immediates, and I/O.
 *
 * The Curica event loop runs on the main VM thread and integrates three
 * event sources into a single poll() call:
 *
 *   1. Timers (setTimeout / setInterval):  TimerHandle sorted linked list.
 *   2. Immediates (setImmediate):           CheckHandle FIFO queue.
 *   3. I/O file descriptors:               IOHandle linked list, polled for
 *      POLLIN/POLLOUT. Includes the thread-pool wakeup pipe.
 *
 * When any fd becomes ready or a timer fires, the corresponding callback
 * is invoked synchronously on the main thread. After each event-loop tick,
 * the VM drains its microtask and next-tick queues.
 */
#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdint.h>
#include <stdbool.h>
#include <poll.h>

#include "value.h"

/* ── Callback types ─────────────────────────────────────────────────────── */

/** Timer / check callback */
typedef void (*TimerCallback)(void* arg);

/** I/O readiness callback. events is the revents mask from poll(). */
typedef void (*IOCallback)(void* user_data, int events);

/* ── Handle types ───────────────────────────────────────────────────────── */

/** Timer handle — sorted by absolute expiry timestamp. */
typedef struct TimerHandle {
    uint64_t      expires_at;       /**< Monotonic expiry time (ms).       */
    TimerCallback cb;               /**< Callback to invoke on expiry.     */
    void*         arg;              /**< Opaque argument passed to cb.     */
    Value         async_stack_trace;/**< Captured async stack for tracing. */
    struct TimerHandle* next;
} TimerHandle;

/** Check handle — for setImmediate, runs after timers each tick. */
typedef struct CheckHandle {
    TimerCallback cb;
    void*         arg;
    Value         async_stack_trace;
    struct CheckHandle* next;
} CheckHandle;

/**
 * I/O handle — wraps a file descriptor to be watched by the event loop.
 * Registered with el_add_io(); removed with el_remove_io().
 * The fd is polled each tick; cb is called with the ready revents mask.
 */
typedef struct IOHandle {
    int        fd;           /**< File descriptor to watch.        */
    int        events;       /**< Requested events: POLLIN/POLLOUT. */
    IOCallback cb;           /**< Called when fd becomes ready.    */
    void*      user_data;    /**< Opaque payload for the callback. */
    bool       active;       /**< If false, skip on next poll tick. */
    struct IOHandle* next;
} IOHandle;

/* ── Event loop state ───────────────────────────────────────────────────── */

typedef struct EventLoop {
    TimerHandle*  timers;          /**< Sorted timer list (next-to-fire first). */
    CheckHandle*  checks_head;     /**< Immediate queue head.                   */
    CheckHandle*  checks_tail;     /**< Immediate queue tail.                   */
    IOHandle*     io_handles;      /**< Linked list of registered I/O handles.  */
    uint32_t      io_count;        /**< Number of active I/O handles.           */
    bool          stop_flag;       /**< Set to true to break el_run().          */
} EventLoop;

/* ── Lifecycle ─────────────────────────────────────────────────────────────*/

/** Initialise the event loop and start the thread pool. */
void el_init(EventLoop* loop);

/** Run the event loop until it is empty or el_stop() is called. */
void el_run(EventLoop* loop);

/** Signal el_run() to exit after the current tick. */
void el_stop(EventLoop* loop);

/* ── Timer API ─────────────────────────────────────────────────────────── */

/** Schedule cb(arg) to be called after timeout_ms milliseconds. */
void el_set_timeout(EventLoop* loop, uint64_t timeout_ms,
                    TimerCallback cb, void* arg, Value async_stack_trace);

/* ── Immediate API ─────────────────────────────────────────────────────── */

/** Schedule cb(arg) to run at the start of the next event-loop Check phase. */
CheckHandle* el_set_immediate(EventLoop* loop, TimerCallback cb, void* arg,
                               Value async_stack_trace);

/** Cancel a pending setImmediate handle. */
void el_clear_immediate(EventLoop* loop, CheckHandle* handle);

/* ── I/O Handle API ────────────────────────────────────────────────────── */

/** Register an IOHandle with the event loop. Caller owns the struct memory. */
void el_add_io(EventLoop* loop, IOHandle* handle);

/** Deregister an IOHandle. The fd is NOT closed by this call. */
void el_remove_io(EventLoop* loop, IOHandle* handle);

#include "alloc.h"
void el_trace_roots(GCTraceFn trace);

/* ── Utility ───────────────────────────────────────────────────────────── */

/** Returns the current monotonic time in milliseconds. */
uint64_t el_current_time_ms(void);

extern _Thread_local EventLoop* g_event_loop;

#endif /* EVENT_LOOP_H */
