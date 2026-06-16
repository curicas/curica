/**
 * @file alloc.h
 * @brief Memory and Object structures.
 * 
 * Defines the core JS object types (Strings, Arrays, Functions, Objects) and their
 * underlying C struct representations.
 */
#ifndef ALLOC_H
#define ALLOC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "value.h"

// Object types allocated in the arena
typedef enum {
    OBJ_STRING,
    OBJ_ARRAY,
    OBJ_ARRAY_DATA,       // Raw array data holding JS Values (for resizing)
    OBJ_OBJECT,
    OBJ_OBJECT_DATA,      // Raw key-value property list (for resizing)
    OBJ_FUNCTION,
    OBJ_ENV,              // Environment frame
    OBJ_SET,              // Set object
    OBJ_FLOAT16_ARRAY,    // Float16Array object
    OBJ_PROMISE,          // Promise object
    OBJ_ITERATOR_HELPER,  // Lazy iterator helper (ES2025)
    OBJ_SYMBOL,           // Symbol primitive wrapper
    OBJ_REGEXP,           // Regular expression object
    OBJ_ERROR,            // Error object
    OBJ_BUFFER,           // Buffer object
    OBJ_BUFFER_DATA,      // Raw bytes for buffer
    OBJ_SHARED_ARRAY_BUFFER, // Shared array buffer for multithreading
} ObjType;

/**
 * @struct BlockHeader
 * @brief 8-byte header for every allocated block in the bump-pointer Arena.
 * 
 * Used during the Mark-and-Sweep Garbage Collection phase to trace object 
 * lifetimes and coalesce freed memory.
 */
typedef struct BlockHeader {
    uint32_t size;        // Total block size in bytes (header + payload, 8-byte aligned)
    uint16_t is_free;     // 1 if free, 0 if allocated
    uint8_t gc_mark;      // GC mark bit (0 or 1)
    uint8_t obj_type;     // ObjType
} BlockHeader;

// String object
typedef struct {
    uint32_t length;
    uint32_t hash;
    char data[]; // Null-terminated string data
} JSString;

// Array object
typedef struct {
    uint32_t length;
    uint32_t capacity;
    Value* elements; // Points to an OBJ_ARRAY_DATA block in the arena
} JSArray;

// Property pair
typedef struct {
    Value key;   // OBJ_STRING
    Value value; // Any Value
} Property;

// Object object (key-value map)
typedef struct {
    uint32_t count;
    uint32_t capacity;
    Property* properties; // Points to an OBJ_OBJECT_DATA block in the arena
} JSObject;

// Set object (hash table representation)
typedef struct {
    uint32_t count;
    uint32_t capacity;
    Value* elements; // Points to an OBJ_ARRAY_DATA block containing the hash table slots
} JSSet;

// Float16Array object
typedef struct {
    uint32_t length;
    uint16_t* data; // Points to a block in memory holding 16-bit half floats
} JSFloat16Array;

// Buffer object — wraps a raw byte slab outside the Arena GC heap.
// When is_external is false, data was malloc'd by Curica and must be freed on finalization.
// When is_external is true, data is owned externally (e.g. passed in via N-API) and must NOT be freed.
typedef struct {
    uint32_t length;
    uint8_t* data;      // Pointer to raw byte slab (outside Arena)
    bool is_external;   // If true, data is borrowed and must not be freed by Curica
    Value slab_ref;     // Reference to the underlying slab (OBJ_BUFFER_DATA) if any
} JSBuffer;

// Promise object
typedef struct {
    uint32_t state; // 0: pending, 1: fulfilled, 2: rejected
    Value result;
    Value then_callbacks; // Array of { onFulfilled, onRejected } objects, or VAL_NULL
} JSPromise;

// Iterator helper types
typedef enum {
    ITER_MAP,
    ITER_FILTER,
    ITER_TAKE,
    ITER_DROP,
    ITER_FLAT_MAP,
} IterHelperType;

// Iterator helper object (lazy evaluation)
typedef struct {
    Value source;        // Source iterator Value
    Value callback;      // Callback function Value (or VAL_NULL for take/drop)
    int32_t count;       // Counter for take/drop (remaining), or flat_map inner state
    Value inner;         // Inner iterator for flatMap (VAL_NULL if not active)
    uint8_t helper_type; // IterHelperType
    uint8_t done;        // 1 if exhausted
} JSIteratorHelper;

// Symbol object
typedef struct {
    Value description; // String value or VAL_UNDEFINED
    uint32_t id;       // Unique monotonic ID
} JSSymbol;

// RegExp object
typedef struct {
    Value source;      // Pattern string
    Value flags;       // Flags string ("g", "i", "m", etc.)
    void* compiled;    // Opaque compiled state (regex_t*)
    uint8_t global;
    uint8_t ignore_case;
    uint8_t multiline;
    uint8_t dot_all;
} JSRegExp;

// Error object
typedef struct {
    Value name;    // String: "Error", "TypeError", etc.
    Value message; // String message
    Value stack;   // String stack trace (simplified)
    uint32_t count;
    uint32_t capacity;
    Property* properties;
} JSError;

struct CompiledProgram;

// Function object (closure)
typedef struct {
    struct CompiledProgram* prog; // The module this function belongs to
    uint32_t bytecode_offset; // 0xffffffff if native
    uint32_t register_count;
    uint32_t param_count;
    bool is_async;
    Value env;          // Lexical environment (OBJ_ENV or VAL_NULL)
    Value name;         // Name of the function (OBJ_STRING)
    void* native_ptr;   // Native function pointer (if bytecode_offset == 0xffffffff)
    Property* properties;
    uint32_t count;
    uint32_t capacity;
    void* user_data;    // For N-API or other custom callbacks
} JSFunction;

// Environment object
typedef struct {
    Value parent;       // Parent environment (OBJ_ENV or VAL_NULL)
    uint32_t count;
    Value values[];     // Local variables
} JSEnvironment;

// Forward declaration of VM for GC tracing
struct VM;

// Initialization and allocation
void arena_init(size_t size_mb);
void arena_free(void);
void* arena_alloc(ObjType type, size_t size);
bool is_ptr_in_arena(const void* ptr);
void arena_snapshot_save(FILE* f);
void arena_snapshot_load(FILE* f);

// Object creation helpers
Value create_string(const char* str, int len);
Value create_string_from_chars(const char* str, int len); // always allocates (no intern)
Value create_array(uint32_t capacity);
Value array_push(Value array_val, Value val);
Value create_object(void);
Value object_get(Value obj_val, Value key_val);
void object_set(Value obj_val, Value key_val, Value value_val);
Value create_set(void);
bool set_add(Value set_val, Value val);
bool set_has(Value set_val, Value val);
bool set_delete(Value set_val, Value val);
Value create_float16_array(uint32_t length);
Value create_promise(uint32_t state, Value result);
Value create_function(struct CompiledProgram* prog, uint32_t bytecode_offset, uint32_t register_count, uint32_t param_count, bool is_async, Value env, Value name);

const char* get_system_error_name(int err_code);
Value create_system_error(struct VM* vm, int err_code, const char* syscall_name, const char* custom_message);
Value create_native_function(void* native_ptr, Value name);
Value create_bound_native_function(void* native_ptr, Value name, Value env);
Value create_bound_bytecode_function(JSFunction* target, Value bound_this);
Value create_environment(Value parent, uint32_t count);
Value create_symbol(Value description);
Value create_iterator_helper(Value source, Value callback, uint8_t helper_type, int32_t count);
Value create_error(const char* type_name, Value message);
Value create_buffer(uint32_t size, bool zero_fill);
Value create_buffer_from_string(const char* str, size_t len, const char* encoding);
Value create_buffer_external(void* data, size_t len);

// Type coercion helpers
Value value_to_string(Value v);      // Returns a JSString NaN-boxed Value
Value value_to_number(Value v);      // Returns double or integer Value
bool  values_strict_equal(Value a, Value b);
bool  values_abstract_equal(Value a, Value b);

// Garbage Collector
typedef void (*GCTraceFn)(Value* val_ptr);
void gc_minor(struct VM* vm);
void gc_major(struct VM* vm);
void gc_mark_value(Value val);
void gc_mark_value_ptr(Value* val_ptr);
void minor_copy_value(Value* val_ptr);
void gc_write_barrier(Value old_obj, Value new_obj);

// Helper for strings
uint32_t hash_string(const char* key, int len);

#endif // ALLOC_H
