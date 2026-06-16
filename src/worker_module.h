#ifndef WORKER_MODULE_H
#define WORKER_MODULE_H

#include "value.h"
#include "vm.h"
#include <pthread.h>

/**
 * Node in a cross-thread message queue.
 * Stores a JSON-serialized string to avoid sharing GC heap pointers.
 */
typedef struct MessageNode {
    char* payload;
    struct MessageNode* next;
} MessageNode;

/**
 * Handle for a Web Worker thread.
 */
typedef struct WorkerHandle {
    pthread_t thread;
    
    // Pipe for Main -> Worker wakeup
    int worker_pipe_rx;
    int worker_pipe_tx;
    
    // Pipe for Worker -> Main wakeup
    int main_pipe_rx;
    int main_pipe_tx;
    
    // Queue for Main -> Worker messages
    pthread_mutex_t worker_mutex;
    MessageNode* worker_queue_head;
    MessageNode* worker_queue_tail;
    
    // Queue for Worker -> Main messages
    pthread_mutex_t main_mutex;
    MessageNode* main_queue_head;
    MessageNode* main_queue_tail;
    
    char* script_path;
    
    Value worker_obj; // Reference to the JS Worker instance on the main thread
    void* main_io;    // IOHandle* on the main thread event loop
    void* worker_io;  // IOHandle* on the worker thread event loop
    bool terminate_requested;
    struct WorkerHandle* next;
    bool in_callback;
    bool needs_free;
} WorkerHandle;

#include "alloc.h"

/**
 * Initialize and register the global Worker constructor on the provided VM.
 */
Value build_worker_constructor(VM* vm);

void worker_mark_gc_roots(GCTraceFn trace);

extern _Thread_local WorkerHandle* current_thread_worker_handle;

#endif // WORKER_MODULE_H
