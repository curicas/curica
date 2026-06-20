/**
 * @file thread_pool.c
 * @brief POSIX Thread-Pool Implementation.
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
#include "thread_pool.h"
#include "vm.h"

#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── Internal State ─────────────────────────────────────────────────────── */

/** Work queue state — protected by work_mutex. */
static pthread_mutex_t work_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  work_cond   = PTHREAD_COND_INITIALIZER;
static WorkItem*       work_head   = NULL;
static WorkItem*       work_tail   = NULL;
static int             pool_active = 0;   /* Set to 0 to signal shutdown */

/** Completion queue — protected by done_mutex. */
static pthread_mutex_t done_mutex = PTHREAD_MUTEX_INITIALIZER;
static WorkItem*       done_head  = NULL;
static WorkItem*       done_tail  = NULL;

/** Wakeup pipe: write end owned by workers, read end polled by main thread. */
static int wakeup_pipe[2] = { -1, -1 };

/** Worker thread handles. */
static pthread_t workers[TP_NUM_WORKERS];

/** Count of in-flight work items (submitted but not yet drained). */
static int pending_count = 0;
static pthread_mutex_t pending_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Array of actively processing items for GC tracing */
static WorkItem* active_items[TP_NUM_WORKERS] = {NULL};

/* ── Worker Thread ──────────────────────────────────────────────────────── */

/**
 * Main loop for each worker thread.
 * Blocks on work_cond until a WorkItem is available, executes it,
 * then pushes the result to the done_queue and signals the main thread.
 */
static void* worker_main(void* arg) {
    int worker_id = (int)(intptr_t)arg;
    while (1) {
        /* Wait for work or shutdown signal */
        pthread_mutex_lock(&work_mutex);
        while (!work_head && pool_active) {
            pthread_cond_wait(&work_cond, &work_mutex);
        }
        if (!pool_active && !work_head) {
            pthread_mutex_unlock(&work_mutex);
            break; /* Shutdown: no more work */
        }
        /* Dequeue from front of work list */
        WorkItem* item = work_head;
        work_head = item->next;
        if (!work_head) work_tail = NULL;
        item->next = NULL;
        
        // Register active item for GC before unlocking
        active_items[worker_id] = item;
        
        pthread_mutex_unlock(&work_mutex);

        /* Execute blocking work — this may take arbitrary time */
        item->status = 0;
        item->work(item->data, &item->status);

        /* Push to done queue */
        pthread_mutex_lock(&done_mutex);
        item->next = NULL;
        if (done_tail) {
            done_tail->next = item;
            done_tail = item;
        } else {
            done_head = done_tail = item;
        }
        
        // Unregister active item for GC
        active_items[worker_id] = NULL;
        
        pthread_mutex_unlock(&done_mutex);

        /* Wake up the main event loop thread */
        char byte = 1;
        if (write(wakeup_pipe[1], &byte, 1) < 0) {
            /* The pipe may be full (unlikely) — main thread will still drain */
            (void)0;
        }
    }
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void tp_init(void) {
    if (wakeup_pipe[0] >= 0) return; /* Already initialised */

    if (pipe(wakeup_pipe) != 0) {
        perror("tp_init: pipe");
        abort();
    }
    /* Make BOTH ends non-blocking:
     *  - Write end: so workers never stall on a full pipe.
     *  - Read end:  so tp_drain_completions read() returns EAGAIN instead of
     *               blocking when the pipe has no data (worker not done yet). */
    int flags = fcntl(wakeup_pipe[0], F_GETFL, 0);
    fcntl(wakeup_pipe[0], F_SETFL, flags | O_NONBLOCK);
    flags = fcntl(wakeup_pipe[1], F_GETFL, 0);
    fcntl(wakeup_pipe[1], F_SETFL, flags | O_NONBLOCK);

    pool_active = 1;
    for (int i = 0; i < TP_NUM_WORKERS; i++) {
        active_items[i] = NULL;
        if (pthread_create(&workers[i], NULL, worker_main, (void*)(intptr_t)i) != 0) {
            perror("tp_init: pthread_create");
            abort();
        }
    }
}

void tp_submit(WorkItem* item) {
    /* Increment pending count before handing to workers */
    pthread_mutex_lock(&pending_mutex);
    pending_count++;
    pthread_mutex_unlock(&pending_mutex);

    item->next = NULL;
    pthread_mutex_lock(&work_mutex);
    if (work_tail) {
        work_tail->next = item;
        work_tail = item;
    } else {
        work_head = work_tail = item;
    }
    /* Wake exactly one worker */
    pthread_cond_signal(&work_cond);
    pthread_mutex_unlock(&work_mutex);
}

void tp_drain_completions(struct VM* vm) {
    /* Drain the wakeup pipe (may have multiple bytes) */
    char buf[64];
    while (read(wakeup_pipe[0], buf, sizeof(buf)) > 0) { /* consume */ }

    /* Process all done items on the main thread */
    while (1) {
        pthread_mutex_lock(&done_mutex);
        WorkItem* item = done_head;
        if (item) {
            done_head = item->next;
            if (!done_head) done_tail = NULL;
        }
        pthread_mutex_unlock(&done_mutex);
        if (!item) break;

        /* Invoke after() on main thread — may safely touch VM */
        if (item->after) {
            item->after(vm, item->data, item->status);
        }

        /* Decrement pending count after after() completes */
        pthread_mutex_lock(&pending_mutex);
        pending_count--;
        pthread_mutex_unlock(&pending_mutex);
    }
}

int tp_wakeup_fd(void) {
    return wakeup_pipe[0];
}

void tp_free(void) {
    /* Signal workers to stop */
    pthread_mutex_lock(&work_mutex);
    pool_active = 0;
    pthread_cond_broadcast(&work_cond);
    pthread_mutex_unlock(&work_mutex);

    for (int i = 0; i < TP_NUM_WORKERS; i++) {
        pthread_join(workers[i], NULL);
    }
    close(wakeup_pipe[0]);
    close(wakeup_pipe[1]);
    wakeup_pipe[0] = wakeup_pipe[1] = -1;
}

int tp_pending_count(void) {
    pthread_mutex_lock(&pending_mutex);
    int count = pending_count;
    pthread_mutex_unlock(&pending_mutex);
    return count;
}

void tp_mark_gc_roots(GCTraceFn trace) {
    /* Mark work queue */
    pthread_mutex_lock(&work_mutex);
    WorkItem* item = work_head;
    while (item) {
        if (item->gc_mark) {
            item->gc_mark(item->data, trace);
        }
        item = item->next;
    }
    pthread_mutex_unlock(&work_mutex);

    /* Mark done queue */
    pthread_mutex_lock(&done_mutex);
    item = done_head;
    while (item) {
        if (item->gc_mark) {
            item->gc_mark(item->data, trace);
        }
        item = item->next;
    }
    pthread_mutex_unlock(&done_mutex);
    
    /* Mark active items */
    pthread_mutex_lock(&done_mutex); // Use done_mutex as memory barrier
    for (int i = 0; i < TP_NUM_WORKERS; i++) {
        WorkItem* active = active_items[i];
        if (active && active->gc_mark) {
            active->gc_mark(active->data, trace);
        }
    }
    pthread_mutex_unlock(&done_mutex);
}
