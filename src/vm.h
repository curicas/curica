/**
 * @file vm.h
 * @brief Virtual Machine and CallFrame structures.
 * 
 * Defines the VM state, Execution Frames, Coroutine boundaries, and bytecode opcodes.
 */
#ifndef VM_H
#define VM_H

#include <stdint.h>
#include <setjmp.h>
#include <ucontext.h>
#include "value.h"
#include "bytecode.h"

typedef struct NapiApiTable NapiApiTable;

// VM Error Handler for try/catch/throw
typedef struct VMErrorHandler {
    jmp_buf jmp;
    Value error;
    int active;
    uint32_t saved_frame_count;
    uint32_t saved_reg_count;
    uint32_t catch_ip;             // IP of the catch block
    struct VMErrorHandler* prev;   // Linked list for nested try blocks
} VMErrorHandler;

// Microtask queue entry
typedef struct MicrotaskEntry {
    Value callback;              // Function value to call
    Value arg;                   // Single argument (Promise result)
    struct MicrotaskEntry* next;
} MicrotaskEntry;

// NextTick queue entry
typedef struct NextTickEntry {
    Value callback;              // Function value to call
    Value arg;                   // Single argument
    struct NextTickEntry* next;
} NextTickEntry;

// Coroutine state for async/await
typedef enum {
    CORO_RUNNING,
    CORO_SUSPENDED,   // awaiting a Promise
    CORO_DONE,
} CoroState;

#define CORO_STACK_SIZE (256 * 1024)  // 256KB coroutine stack

/**
 * @struct CallFrame
 * @brief Represents a single function execution context on the VM stack.
 * 
 * Curica does not use the native C-stack for executing JavaScript. Instead, 
 * it maintains a flat array of CallFrames to prevent C-stack overflows.
 */
typedef struct VMCoroutine VMCoroutine;

typedef struct {
    struct CompiledProgram* prog; // The module executing this frame
    Value func;          // JSFunction pointer (NaN-boxed)
    uint32_t ip;         // Instruction pointer (bytecode offset)
    uint32_t reg_base;   // Index of first register in vm->registers
    uint32_t reg_count;  // Number of registers in this frame
    Value env;           // Current lexical environment (NaN-boxed JSEnvironment)
    VMCoroutine* coro;   // Non-NULL if this frame is inside an async coroutine
    Value new_target;    // The `this` object for constructor (new) calls; VAL_UNDEFINED for normal calls
    Value old_this;      // Saved global `this` value to restore after constructor call
} CallFrame;

/**
 * @struct VMCoroutine
 * @brief Represents a stackful coroutine for async/await execution.
 * 
 * Curica uses `ucontext_t` to allocate isolated C-stacks for async functions,
 * allowing native non-blocking suspension within the VM.
 */
struct VMCoroutine {
    ucontext_t ctx;               // Coroutine execution context
    ucontext_t caller_ctx;        // Context to return to when suspending
    char* stack;                  // Coroutine stack
    CoroState state;
    Value await_value;            // The value/promise being awaited
    Value result;                 // Final resolved value
    Value promise;                // The JSPromise that this coroutine resolves
    bool is_error;                // True if the coroutine threw an uncaught error
    struct VMCoroutine* next;     // Next in resume queue
    Value async_stack_trace;      // Captured async stack trace
    
    CallFrame* frames;
    uint32_t frame_count;
    uint32_t frame_capacity;
    Value* registers;
    uint32_t register_count;
    uint32_t register_capacity;
};



/**
 * @struct VM
 * @brief The central Virtual Machine execution context.
 * 
 * The VM structure is the absolute root of execution. It holds the active
 * call stack, registers, microtasks, and global objects. Note that `napi_api` 
 * must be the first member to allow safe casting from `VM*` to `napi_env`.
 */
typedef struct VM {
    NapiApiTable* napi_api;
    // Globals object (key-value map)
    Value global_obj;
    Value module_cache;
    
    // Call stack
    CallFrame* frames;
    uint32_t frame_count;
    uint32_t frame_capacity;
    
    // Registers (flat stack)
    Value* registers;
    uint32_t reg_count;
    uint32_t reg_capacity;
    
    // Explicit GC Roots (C-stack anchoring)
    Value* gc_roots;
    uint32_t gc_root_count;
    uint32_t gc_root_capacity;
    
    // Constant Pool and Bytecode
    struct CompiledProgram* current_prog;
    Value* const_pool;
    uint32_t const_pool_size;
    uint32_t* bytecode;
    uint32_t bytecode_size;
    void* functions; // CompilerFuncInfo array
    uint32_t function_count;
    
    // Prototypes
    Value set_prototype;
    Value array_prototype;
    Value string_prototype;
    Value float16_array_prototype;
    Value function_prototype;
    bool in_promise_microtask;
    bool sandbox_mode; // Edge Compute Sandboxing Mode
    Value iterator_prototype;
    Value error_prototype;
    Value promise_prototype;
    Value regexp_prototype;
    
    // Exception handling (linked stack)
    VMErrorHandler* error_handler;
    
    // Microtask queue (for Promise resolution + async/await)
    MicrotaskEntry* microtask_head;
    MicrotaskEntry* microtask_tail;
    
    // NextTick queue (Node.js compatibility, runs before microtasks)
    NextTickEntry* next_tick_head;
    NextTickEntry* next_tick_tail;
    
    // Active coroutine (NULL if running synchronously)
    VMCoroutine* current_coro;
    
    // Well-known Symbols
    Value symbol_iterator;
    Value symbol_async_iterator;
    Value symbol_dispose;
    Value symbol_async_dispose;
    
    // Security Sandbox Flags
    bool allow_net;
    bool allow_read;
    bool allow_write;
    bool allow_run;
    bool allow_ffi;
    
    // Virtual Networking Mock Topologies
    struct {
        char host[256];
        int port;
        char unix_socket_path[256];
    } net_mocks[16];
    int net_mock_count;

    int ipc_fd;
    void* ipc_io; // Use void* to avoid EventLoop IOHandle dependency in vm.h
} VM;

// VM Lifecycle and API
void vm_init(VM* vm);
void vm_free(VM* vm);

void vm_push_root(VM* vm, Value val);
void vm_pop_root(VM* vm);

// Modules
void vm_register_module(VM* vm, const char* name, Value module_obj);
void vm_load_program(VM* vm, struct CompiledProgram* prog);
Value vm_run(VM* vm);

// Microtask queue
void vm_enqueue_microtask(VM* vm, Value callback, Value arg);
void vm_drain_microtasks(VM* vm);

void vm_enqueue_next_tick(VM* vm, Value callback, Value arg);
void vm_drain_next_tick(VM* vm);

// Coroutine API
VMCoroutine* vm_coro_create(VM* vm);
void vm_coro_free(VMCoroutine* coro);
void vm_coro_suspend(VM* vm, Value await_val);  // Called from inside coroutine
void vm_coro_resume(VM* vm, VMCoroutine* coro, Value resolved_val);

// Error helpers
void vm_throw_error(VM* vm, Value err);
Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);

extern _Thread_local VM* g_current_vm;

// Snapshots
bool vm_snapshot_save(VM* vm, const char* path);
bool vm_snapshot_load(VM* vm, const char* path);

float half_to_float(uint16_t h);
uint16_t float_to_half(float f);

#endif // VM_H
