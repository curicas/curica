/**
 * @file event_loop.c
 * @brief Async Event Loop — Microkernel OS I/O scheduler.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#include "event_loop.h"
#include "thread_pool.h"
#include "vm.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

_Thread_local EventLoop* g_event_loop = NULL;

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

void el_init(EventLoop* loop) {
    loop->timers      = NULL;
    loop->checks_head = NULL;
    loop->checks_tail = NULL;
    loop->io_handles  = NULL;
    loop->io_count    = 0;
    loop->stop_flag   = false;
    g_event_loop = loop;

    /* Start the background thread pool and register its wakeup fd */
    tp_init();
}

/* ── Time ──────────────────────────────────────────────────────────────── */

uint64_t el_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ── Timer API ─────────────────────────────────────────────────────────── */

void el_set_timeout(EventLoop* loop, uint64_t timeout_ms,
                    TimerCallback cb, void* arg, Value async_stack_trace) {
    TimerHandle* timer = (TimerHandle*)malloc(sizeof(TimerHandle));
    timer->expires_at       = el_current_time_ms() + timeout_ms;
    timer->cb               = cb;
    timer->arg              = arg;
    timer->async_stack_trace = async_stack_trace;

    /* Insert into sorted list (ascending expiry) */
    if (!loop->timers || loop->timers->expires_at >= timer->expires_at) {
        timer->next  = loop->timers;
        loop->timers = timer;
    } else {
        TimerHandle* curr = loop->timers;
        while (curr->next && curr->next->expires_at < timer->expires_at) {
            curr = curr->next;
        }
        timer->next = curr->next;
        curr->next  = timer;
    }
}

/* ── Immediate API ─────────────────────────────────────────────────────── */

CheckHandle* el_set_immediate(EventLoop* loop, TimerCallback cb,
                               void* arg, Value async_stack_trace) {
    CheckHandle* handle = (CheckHandle*)malloc(sizeof(CheckHandle));
    handle->cb                = cb;
    handle->arg               = arg;
    handle->async_stack_trace = async_stack_trace;
    handle->next              = NULL;

    if (loop->checks_tail) {
        loop->checks_tail->next = handle;
        loop->checks_tail       = handle;
    } else {
        loop->checks_head = loop->checks_tail = handle;
    }
    return handle;
}

void el_clear_immediate(EventLoop* loop, CheckHandle* handle) {
    if (!loop->checks_head || !handle) return;

    if (loop->checks_head == handle) {
        loop->checks_head = handle->next;
        if (!loop->checks_head) loop->checks_tail = NULL;
        free(handle);
        return;
    }
    CheckHandle* curr = loop->checks_head;
    while (curr->next) {
        if (curr->next == handle) {
            curr->next = handle->next;
            if (!curr->next) loop->checks_tail = curr;
            free(handle);
            return;
        }
        curr = curr->next;
    }
}

/* ── I/O Handle API ────────────────────────────────────────────────────── */

void el_add_io(EventLoop* loop, IOHandle* handle) {
    if (!handle) return;
    handle->unrefed = false;
    handle->next     = loop->io_handles;
    loop->io_handles = handle;
    loop->io_count++;
}

void el_remove_io(EventLoop* loop, IOHandle* handle) {
    IOHandle** prev = &loop->io_handles;
    while (*prev) {
        if (*prev == handle) {
            *prev = handle->next;
            loop->io_count--;
            break;
        }
        prev = &(*prev)->next;
    }
}

void el_unref_io(EventLoop* loop, IOHandle* handle) {
    (void)loop;
    if (handle) handle->unrefed = true;
}

void el_ref_io(EventLoop* loop, IOHandle* handle) {
    (void)loop;
    if (handle) handle->unrefed = false;
}

/* ── Control ───────────────────────────────────────────────────────────── */

void el_stop(EventLoop* loop) {
    loop->stop_flag = true;
}

/* ── Main Loop ─────────────────────────────────────────────────────────── */

void el_run(EventLoop* loop) {
    /* Maximum number of fds we watch: all IOHandles + the wakeup pipe */
    /* We rebuild the pollfd array every tick to handle dynamic registration */
    struct pollfd* poll_fds = NULL;
    int poll_cap = 0;

    while (!loop->stop_flag) {
        uint64_t now = el_current_time_ms();

        /* ── Compute poll() timeout ── */
        int timeout = -1; /* block indefinitely by default */

        /* Calculate active IO handles (not unrefed) */
        int active_io_count = 0;
        for (IOHandle* h = loop->io_handles; h; h = h->next) {
            if (!h->unrefed) active_io_count++;
        }

        bool has_work = loop->timers || loop->checks_head ||
                        (active_io_count > 0) || (tp_pending_count() > 0);
        if (!has_work) break;

        if (loop->timers) {
            if (loop->timers->expires_at <= now) {
                timeout = 0; /* timer already expired, don't block */
            } else {
                timeout = (int)(loop->timers->expires_at - now);
            }
        }
        if (loop->checks_head) {
            timeout = 0; /* immediates pending: don't block */
        }
        /* When only thread-pool work is in flight, block on poll() for up to
         * 50ms so the wakeup pipe write wakes us up without spinning. */
        if (timeout == -1 && tp_pending_count() > 0) {
            timeout = 50;
        }

        /* ── Build pollfd array ── */
        /* Count: wakeup pipe + active IOHandles */
        int nfds = 0;
        int wakeup = tp_wakeup_fd();
        if (wakeup >= 0) nfds++;

        /* Count active IOHandles */
        for (IOHandle* h = loop->io_handles; h; h = h->next) {
            nfds++;
        }

        /* Grow array if needed */
        if (nfds > poll_cap) {
            free(poll_fds);
            poll_fds = (struct pollfd*)malloc(sizeof(struct pollfd) * (nfds + 4));
            poll_cap = nfds + 4;
        }

        int idx = 0;
        if (wakeup >= 0 && poll_fds) {
            poll_fds[idx].fd      = wakeup;
            poll_fds[idx].events  = POLLIN;
            poll_fds[idx].revents = 0;
            idx++;
        }
        for (IOHandle* h = loop->io_handles; h; h = h->next) {
            if (poll_fds) {
                poll_fds[idx].fd      = h->fd;
                poll_fds[idx].events  = (short)h->events;
                poll_fds[idx].revents = 0;
                idx++;
            }
        }

        /* ── Block in poll() ── */
        if (nfds > 0 || timeout > 0) {
            poll(poll_fds, (nfds_t)idx, timeout);
        }
        now = el_current_time_ms();

        /* ── Phase 1: Timers ── */
        while (loop->timers && loop->timers->expires_at <= now) {
            TimerHandle* t = loop->timers;
            loop->timers   = t->next;
            t->cb(t->arg);
            free(t);
        }

        /* ── Phase 2: I/O callbacks (registered fds) ── */
        /* Walk IOHandle list; skip the wakeup fd (handled in phase 3). */
        idx = (wakeup >= 0) ? 1 : 0; /* skip slot 0 = wakeup */
        IOHandle* h = loop->io_handles;
        while (h) {
            IOHandle* next = h->next;
            if (poll_fds && idx < nfds && poll_fds[idx].revents) {
                h->cb(h->user_data, poll_fds[idx].revents);
            }
            h = next;
            idx++;
        }

        /* ── Phase 3: Thread-pool completions ── */
        /* Always attempt to drain when work is in-flight. The done_queue is
         * empty until a worker pushes to it, so tp_drain is a fast no-op when
         * nothing has completed yet. The wakeup pipe read happens inside drain. 
         *
         * N-API BOUNDARY NOTE: Native modules calling `napi_queue_async_work` 
         * submit tasks to the thread pool. When `tp_drain_completions` is called 
         * here, it executes the `napi_async_complete_callback` on the MAIN thread, 
         * allowing safe invocation of JS engine APIs. This phase is also responsible 
         * for resolving Promises returned by `fs` module operations, triggering 
         * Coroutine resumption via the Microtask queue. */
        if (tp_pending_count() > 0 || (wakeup >= 0 && poll_fds &&
                                        nfds > 0 && poll_fds[0].revents & POLLIN)) {
            tp_drain_completions(g_current_vm);
        }

        /* ── Phase 4: Check (setImmediate) ── */
        while (loop->checks_head) {
            CheckHandle* c   = loop->checks_head;
            loop->checks_head = c->next;
            if (!loop->checks_head) loop->checks_tail = NULL;
            c->cb(c->arg);
            free(c);
        }

        /* ── Phase 5: Microtask drain ──
         * Drain Promises and other microtasks enqueued by I/O callbacks,
         * thread-pool completions, or net event callbacks this tick. */
        if (g_current_vm) {
            vm_drain_microtasks(g_current_vm);
        }
    }

    free(poll_fds);
}

void el_trace_roots(GCTraceFn trace) {
    if (!g_event_loop) return;
    
    // Mark timers
    for (TimerHandle* t = g_event_loop->timers; t; t = t->next) {
        trace((Value*)(uintptr_t)&t->arg);
        trace(&t->async_stack_trace);
    }
    
    // Mark checks
    for (CheckHandle* c = g_event_loop->checks_head; c; c = c->next) {
        trace((Value*)(uintptr_t)&c->arg);
        trace(&c->async_stack_trace);
    }
    
    // Call module-specific IO handle marking functions
    extern void net_mark_gc_roots(GCTraceFn trace);
    net_mark_gc_roots(trace);
    extern void dgram_mark_gc_roots(GCTraceFn trace);
    dgram_mark_gc_roots(trace);
    extern void zlib_mark_gc_roots(GCTraceFn trace);
    zlib_mark_gc_roots(trace);
    extern void http_mark_gc_roots(GCTraceFn trace);
    http_mark_gc_roots(trace);    
    extern void ws_mark_gc_roots(GCTraceFn trace);
    ws_mark_gc_roots(trace);
    extern void child_process_mark_gc_roots(GCTraceFn trace);
    child_process_mark_gc_roots(trace);
    extern void worker_mark_gc_roots(GCTraceFn trace);
    worker_mark_gc_roots(trace);
}

