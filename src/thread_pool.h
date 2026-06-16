/**
 * @file thread_pool.h
 * @brief POSIX Thread-Pool for Async I/O Offloading.
 *
 * Provides a fixed-size worker thread pool that executes blocking I/O
 * on background threads and signals completion back to the main event
 * loop thread via a wakeup pipe. The main thread calls poll() on
 * tp_wakeup_fd() as part of the event loop; when readable, it calls
 * tp_drain_completions(vm) to invoke after-callbacks on the main thread.
 *
 * Thread-Safety Contract:
 *   - tp_submit() is called ONLY from the main thread.
 *   - WorkItem.work() is called ONLY from a worker thread.
 *   - WorkItem.after() is called ONLY from the main thread (via tp_drain).
 *   - The wakeup pipe is the only cross-thread synchronization mechanism
 *     visible to the event loop; all else is protected by internal mutexes.
 */
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stddef.h>

/* Forward declarations to avoid circular includes. */
struct VM;

/** Maximum number of background worker threads. */
#define TP_NUM_WORKERS 4

/**
 * Work callback executed on a worker thread.
 * Blocking operations are permitted. Must NOT touch VM state.
 *
 * @param data  User-provided payload.
 * @param status_out  Set to 0 on success, errno value on failure.
 */
typedef void (*tp_work_fn)(void* data, int* status_out);

/**
 * Completion callback executed on the main VM thread after work finishes.
 * May safely call VM functions (vm_enqueue_microtask, etc.).
 *
 * @param vm     The active VM instance.
 * @param data   The same payload passed to tp_work_fn.
 * @param status 0 on success, errno value on failure.
 */
typedef void (*tp_after_fn)(struct VM* vm, void* data, int status);

#include "alloc.h"

/** A single unit of async work. Callers allocate and fill this struct. */
typedef struct WorkItem {
    tp_work_fn   work;      /**< Blocking work, called on worker thread.  */
    tp_after_fn  after;     /**< Completion callback, called on main thread. */
    void         (*gc_mark)(void* data, GCTraceFn trace); /**< GC tracing callback for payloads. */
    void*        data;      /**< Arbitrary user payload.                   */
    int          status;    /**< Set by worker; 0 = ok, else errno.        */
    struct WorkItem* next;  /**< Intrusive list linkage (internal use).    */
} WorkItem;

/**
 * Traces all pending work items and invokes their gc_mark callbacks.
 * Called by the GC (alloc.c) to prevent premature collection of promises/callbacks.
 */
void tp_mark_gc_roots(GCTraceFn trace);

/**
 * Initialize the thread pool and spawn TP_NUM_WORKERS worker threads.
 * Also creates the wakeup pipe. Must be called once before tp_submit().
 */
void tp_init(void);

/**
 * Submit a work item to the thread pool.
 * Ownership of `item` is transferred to the pool until after() returns.
 * Safe to call from the main thread only.
 */
void tp_submit(WorkItem* item);

/**
 * Drain all completed work items and invoke their after() callbacks.
 * Must be called from the main thread, typically when tp_wakeup_fd()
 * becomes readable in poll().
 *
 * @param vm  The active VM, passed through to each after() callback.
 */
void tp_drain_completions(struct VM* vm);

/**
 * Returns the read end of the wakeup pipe.
 * Include this fd in the event loop's poll() call with POLLIN.
 * When readable, call tp_drain_completions().
 */
int tp_wakeup_fd(void);

/**
 * Shut down all worker threads and close the wakeup pipe.
 * Blocks until all workers exit.
 */
void tp_free(void);

/**
 * Returns the number of work items currently in-flight (submitted but
 * not yet drained). The event loop should keep running while this is > 0.
 */
int tp_pending_count(void);

#endif /* THREAD_POOL_H */
