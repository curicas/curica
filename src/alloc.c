/**
 * @file alloc.c
 * @brief Memory Management and Arena Allocator.
 * 
 * Implements a custom linear Arena allocator, Garbage Collection placeholders,
 * and JS Object hydration mechanisms using NaN-boxing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alloc.h"
#include "vm.h"
#include "thread_pool.h"
#include "event_loop.h"
#include <sys/mman.h>

static _Thread_local struct {
    char* start;
    char* end;
    char* bump;
} g_nursery = {NULL, NULL, NULL};

static _Thread_local struct {
    char* start;
    char* end;
} g_old_space = {NULL, NULL};
static _Thread_local size_t g_old_space_free_bytes = 0;

// Weak table of interned strings for deduplication
#define MAX_INTERNED_STRINGS 1024
static _Thread_local Value g_interned_strings[MAX_INTERNED_STRINGS];
static _Thread_local int g_interned_count = 0;

// Remembered set for generational GC
#define MAX_REMEMBERED_SET 16384
static _Thread_local Value g_remembered_set[MAX_REMEMBERED_SET];
static _Thread_local int g_remembered_count = 0;

// Global reference to active VM for GC triggering
_Thread_local VM* g_current_vm = NULL;

void arena_init(size_t size_mb) {
    size_t size = size_mb * 1024 * 1024;
    size_t nursery_size = 2 * 1024 * 1024; // 2MB Nursery
    if (nursery_size > size / 4) nursery_size = size / 4;
    size_t old_space_size = size - nursery_size;
    
    static _Atomic int g_arena_thread_count = 0;
    int thread_idx = __atomic_fetch_add(&g_arena_thread_count, 1, __ATOMIC_SEQ_CST);
    void* fixed_addr = (void*)(0x4000000000ULL + ((size_t)thread_idx * 1024ULL * 1024ULL * 1024ULL));
    char* mem = mmap(fixed_addr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mem == MAP_FAILED) {
        fprintf(stderr, "Fatal: failed to mmap fixed memory arena at %p.\n", fixed_addr);
        exit(1);
    }
    
    g_nursery.start = mem;
    g_nursery.end = mem + nursery_size;
    g_nursery.bump = g_nursery.start;
    
    g_old_space.start = mem + nursery_size;
    g_old_space.end = mem + size;
    
    // Initialize the old space as one giant free block
    BlockHeader* initial = (BlockHeader*)g_old_space.start;
    initial->size = old_space_size;
    initial->is_free = 1;
    initial->gc_mark = 0;
    initial->obj_type = 0;
    
    g_old_space_free_bytes = old_space_size;
    
    g_interned_count = 0;
    g_remembered_count = 0;
    g_current_vm = NULL;
}

void arena_free(void) {
    if (g_nursery.start) {
        size_t size = g_old_space.end - g_nursery.start;
        munmap(g_nursery.start, size);
        g_nursery.start = NULL;
        g_nursery.end = NULL;
        g_nursery.bump = NULL;
        g_old_space.start = NULL;
        g_old_space.end = NULL;
    }
}

void arena_snapshot_save(FILE* f) {
    size_t nursery_size = g_nursery.end - g_nursery.start;
    size_t old_space_size = g_old_space.end - g_old_space.start;
    size_t nursery_used = g_nursery.bump - g_nursery.start;
    
    fwrite(&nursery_size, sizeof(size_t), 1, f);
    fwrite(&old_space_size, sizeof(size_t), 1, f);
    fwrite(&nursery_used, sizeof(size_t), 1, f);
    
    fwrite(g_nursery.start, 1, nursery_used, f);
    fwrite(g_old_space.start, 1, old_space_size, f);
}

void arena_snapshot_load(FILE* f) {
    size_t nursery_size, old_space_size, nursery_used;
    fread(&nursery_size, sizeof(size_t), 1, f);
    fread(&old_space_size, sizeof(size_t), 1, f);
    fread(&nursery_used, sizeof(size_t), 1, f);
    
    fread(g_nursery.start, 1, nursery_used, f);
    fread(g_old_space.start, 1, old_space_size, f);
    
    g_nursery.bump = g_nursery.start + nursery_used;
}

bool is_ptr_in_arena(const void* ptr) {
    return ((const char*)ptr >= g_nursery.start && (const char*)ptr < g_nursery.end) ||
           ((const char*)ptr >= g_old_space.start && (const char*)ptr < g_old_space.end);
}

static _Thread_local BlockHeader* g_old_space_next_fit = NULL;

static void* arena_alloc_old_space(ObjType type, size_t size) {
    size_t total_size = sizeof(BlockHeader) + size;
    total_size = (total_size + 7) & ~7; // 8-byte align
    
    if (!g_old_space_next_fit) {
        g_old_space_next_fit = (BlockHeader*)g_old_space.start;
    }
    
    BlockHeader* start_block = g_old_space_next_fit;
    BlockHeader* block = start_block;
    
    do {
        if (block->is_free && block->size >= total_size) {
            if (block->size - total_size >= sizeof(BlockHeader) + 8) {
                BlockHeader* next = (BlockHeader*)((char*)block + total_size);
                next->size = block->size - total_size;
                next->is_free = 1;
                next->gc_mark = 0;
                next->obj_type = 0;
                block->size = total_size;
            }
            block->is_free = 0;
            block->gc_mark = 0;
            block->obj_type = type;
            g_old_space_free_bytes -= total_size;
            
            g_old_space_next_fit = (BlockHeader*)((char*)block + block->size);
            if ((char*)g_old_space_next_fit >= g_old_space.end) {
                g_old_space_next_fit = (BlockHeader*)g_old_space.start;
            }
            
            void* payload = (char*)block + sizeof(BlockHeader);
            memset(payload, 0, block->size - sizeof(BlockHeader));
            return payload;
        }
        
        block = (BlockHeader*)((char*)block + block->size);
        if ((char*)block >= g_old_space.end) {
            block = (BlockHeader*)g_old_space.start;
        }
    } while (block != start_block);
    
    return NULL;
}

static void* arena_alloc_nursery(ObjType type, size_t size) {
    size_t total_size = sizeof(BlockHeader) + size;
    total_size = (total_size + 7) & ~7; // 8-byte align
    
    if (g_nursery.bump + total_size > g_nursery.end) {
        return NULL; // Nursery full
    }
    
    BlockHeader* block = (BlockHeader*)g_nursery.bump;
    block->size = total_size;
    block->is_free = 0;
    block->gc_mark = 0;
    block->obj_type = type;
    
    g_nursery.bump += total_size;
    
    void* payload = (char*)block + sizeof(BlockHeader);
    memset(payload, 0, block->size - sizeof(BlockHeader));
    return payload;
}

extern void gc_minor(VM* vm);
extern void gc_major(VM* vm);

/**
 * @brief Hermetic Arena Memory Allocator
 *
 * `arena_alloc` provisions memory exclusively from a pre-allocated single contiguous
 * block of memory given by the OS at startup, entirely bypassing `malloc`/`free`.
 * 
 * Generational GC strategy:
 * 1. Attempts allocation in the fast bump-pointer Nursery (New Space).
 * 2. If the Nursery is full, it triggers a Minor GC.
 * 3. Objects surviving Minor GC are promoted to Old Space.
 * 4. If Old Space lacks capacity to absorb the Nursery, a Major GC (Mark-Sweep-Compact) is triggered first.
 *
 * @param type The JS Object Type (e.g. OBJ_STRING, OBJ_ARRAY, OBJ_OBJECT)
 * @param size Requested byte size for the payload.
 * @return A pointer to the newly allocated 8-byte aligned memory block.
 */
void* arena_alloc(ObjType type, size_t size) {
    void* ptr = arena_alloc_nursery(type, size);
    if (ptr) return ptr;
    
    if (g_current_vm) {
        // If old space doesn't have enough room for the entire nursery, do major GC first
        if (g_old_space_free_bytes < (size_t)(g_nursery.end - g_nursery.start)) {
            gc_major(g_current_vm);
        }
        
        // Nursery full, do minor GC
        gc_minor(g_current_vm);
        ptr = arena_alloc_nursery(type, size);
        if (ptr) return ptr;
    }
    
    ptr = arena_alloc_old_space(type, size);
    if (ptr) return ptr;
    
    if (g_current_vm) {
        gc_major(g_current_vm);
        ptr = arena_alloc_old_space(type, size);
        if (ptr) return ptr;
    }
    
    fprintf(stderr, "Fatal: Out of Memory in JS Arena (type %d, size %zu)\n", type, size);
    exit(1);
}

// String Hashing (FNV-1a)
uint32_t hash_string(const char* key, int len) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < len; i++) {
        hash ^= (uint8_t)key[i];
        hash *= 16777619u;
    }
    return hash;
}

Value create_string(const char* str, int len) {
    uint32_t hash = hash_string(str, len);
    
    // Look for existing string in interning table
    for (int i = 0; i < g_interned_count; i++) {
        JSString* s = (JSString*)get_pointer(g_interned_strings[i]);
        if (s->length == (uint32_t)len && s->hash == hash && memcmp(s->data, str, len) == 0) {
            return g_interned_strings[i];
        }
    }
    
    // Allocate new string
    JSString* s = arena_alloc(OBJ_STRING, sizeof(JSString) + len + 1);
    s->length = len;
    s->hash = hash;
    memcpy(s->data, str, len);
    s->data[len] = '\0';
    
    Value val = make_pointer(s);
    if (g_interned_count < MAX_INTERNED_STRINGS) {
        g_interned_strings[g_interned_count++] = val;
    }
    return val;
}

Value create_array(uint32_t capacity) {
    Value arr_val = make_pointer(arena_alloc(OBJ_ARRAY, sizeof(JSArray)));
    if (g_current_vm) vm_push_root(g_current_vm, arr_val);
    
    JSArray* arr = (JSArray*)get_pointer(arr_val);
    arr->length = 0;
    arr->capacity = capacity > 0 ? capacity : 4;
    
    Value* elems = arena_alloc(OBJ_ARRAY_DATA, arr->capacity * sizeof(Value));
    
    if (g_current_vm) {
        arr_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        arr = (JSArray*)get_pointer(arr_val);
    }
    
    arr->elements = elems;
    for (uint32_t i = 0; i < arr->capacity; i++) {
        arr->elements[i] = VAL_EMPTY;
    }
    
    if (g_current_vm) vm_pop_root(g_current_vm);
    return arr_val;
}

Value array_push(Value array_val, Value val) {
    if (g_current_vm) {
        vm_push_root(g_current_vm, array_val);
        vm_push_root(g_current_vm, val);
    }
    
    JSArray* arr = (JSArray*)get_pointer(array_val);
    if (arr->length >= arr->capacity) {
        uint32_t old_cap = arr->capacity;
        uint32_t new_cap = old_cap * 2;
        Value* old_elems = arr->elements;
        Value* new_elems = arena_alloc(OBJ_ARRAY_DATA, new_cap * sizeof(Value));
        
        if (g_current_vm) {
            array_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 2];
            val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
            arr = (JSArray*)get_pointer(array_val);
            old_elems = arr->elements; // Re-fetch old_elems too!
        }
        
        memcpy(new_elems, old_elems, old_cap * sizeof(Value));
        for (uint32_t i = old_cap; i < new_cap; i++) {
            new_elems[i] = VAL_EMPTY;
        }
        arr->elements = new_elems;
        arr->capacity = new_cap;
    }
    arr->elements[arr->length] = val;
    arr->length++;
    gc_write_barrier(array_val, val);
    
    if (g_current_vm) {
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
    }
    return make_integer(arr->length);
}

Value create_object(void) {
    Value obj_val = make_pointer(arena_alloc(OBJ_OBJECT, sizeof(JSObject)));
    if (g_current_vm) vm_push_root(g_current_vm, obj_val);
    
    JSObject* obj = (JSObject*)get_pointer(obj_val);
    obj->count = 0;
    obj->capacity = 4;
    
    Property* props = arena_alloc(OBJ_OBJECT_DATA, 4 * sizeof(Property));
    
    if (g_current_vm) {
        obj_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        obj = (JSObject*)get_pointer(obj_val);
    }
    
    obj->properties = props;
    
    if (g_current_vm) vm_pop_root(g_current_vm);
    return obj_val;
}

static inline JSFunction* get_bound_target(JSFunction* f) {
    if (f->bytecode_offset != 0xffffffff) {
        if (f->native_ptr != NULL) {
            return (JSFunction*)f->native_ptr;
        }
    } else {
        if (f->user_data != NULL) {
            return (JSFunction*)f->user_data;
        }
    }
    return NULL;
}

static Value maybe_bind_method(Value res, Value obj_val) {
    if (IS_POINTER(res)) {
        BlockHeader* rh = (BlockHeader*)((char*)get_pointer(res) - sizeof(BlockHeader));
        if (rh->obj_type == OBJ_FUNCTION) {
            JSFunction* f = (JSFunction*)get_pointer(res);
            if (f->bytecode_offset == 0xffffffff) {
                // If it already has a non-undefined env, it's already bound (e.g. to a C handle)
                if (!IS_UNDEFINED(f->env)) return res;
                
                Value bound = create_bound_native_function(f->native_ptr, f->name, obj_val);
                JSFunction* bf = (JSFunction*)get_pointer(bound);
                bf->user_data = f;
                return bound;
            } else if (f->native_ptr == NULL) {
                return create_bound_bytecode_function(f, obj_val);
            }
        }
    }
    return res;
}

Value object_get(Value obj_val, Value key_val) {
    if (!IS_POINTER(obj_val)) return VAL_UNDEFINED;
    
    void* ptr = get_pointer(obj_val);
    BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    
    if (header->obj_type == OBJ_FUNCTION) {
        JSFunction* f = (JSFunction*)ptr;
        JSFunction* target = get_bound_target(f);
        if (target) {
            return object_get(make_pointer(target), key_val);
        }
    }
    if (IS_DOUBLE(key_val) || IS_INTEGER(key_val)) {
        double d = IS_INTEGER(key_val) ? (double)get_integer(key_val) : get_double(key_val);
        long idx = (long)d;
        if (header->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)ptr;
            if (idx >= 0 && (uint32_t)idx < arr->length) {
                return arr->elements[idx];
            }
            return VAL_UNDEFINED;
        } else if (header->obj_type == OBJ_FLOAT16_ARRAY) {
            JSFloat16Array* arr = (JSFloat16Array*)ptr;
            if (idx >= 0 && (uint32_t)idx < arr->length) {
                extern float half_to_float(uint16_t h);
                return make_double(half_to_float(arr->data[idx]));
            }
            return VAL_UNDEFINED;
        } else if (header->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)ptr;
            if (idx >= 0 && (uint32_t)idx < buf->length) {
                return make_integer(buf->data[idx]);
            }
            return VAL_UNDEFINED;
        }
    }
    
    if (!IS_POINTER(key_val)) return VAL_UNDEFINED;
    
    if (header->obj_type == OBJ_FUNCTION) {
        JSFunction* f = (JSFunction*)ptr;
        extern Value js_promise_constructor(struct VM* vm, Value this_val, int arg_count, Value* args);
        if (f->bytecode_offset == 0xffffffff && f->native_ptr == (void*)js_promise_constructor) {
            JSString* str = (JSString*)get_pointer(key_val);
            if (str && str->length == 3 && strcmp(str->data, "try") == 0) {
                extern Value js_promise_try(struct VM* vm, Value this_val, int arg_count, Value* args);
                return create_native_function((void*)js_promise_try, key_val);
            }
        }
        
        if (IS_POINTER(key_val) && ((BlockHeader*)((char*)get_pointer(key_val) - sizeof(BlockHeader)))->obj_type == OBJ_STRING) {
            JSString* ks = (JSString*)get_pointer(key_val);
            if (ks->length == 4 && strcmp(ks->data, "call") == 0) {
                extern Value js_function_call(struct VM* vm, Value this_val, int arg_count, Value* args);
                return create_bound_native_function((void*)js_function_call, key_val, obj_val);
            }
            if (ks->length == 5 && strcmp(ks->data, "apply") == 0) {
                extern Value js_function_apply(struct VM* vm, Value this_val, int arg_count, Value* args);
                return create_bound_native_function((void*)js_function_apply, key_val, obj_val);
            }
            for (uint32_t i = 0; i < f->count; i++) {
                if (!IS_POINTER(f->properties[i].key)) continue;
                JSString* pk = (JSString*)get_pointer(f->properties[i].key);
                if (pk && pk->length == ks->length && memcmp(pk->data, ks->data, ks->length) == 0) {
                    return f->properties[i].value;
                }
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_SET) {
        JSString* str = (JSString*)get_pointer(key_val);
        if (str && str->length == 4 && strcmp(str->data, "size") == 0) {
            JSSet* set = (JSSet*)ptr;
            return make_integer(set->count);
        }
        if (g_current_vm && IS_POINTER(g_current_vm->set_prototype)) {
            Value func = object_get(g_current_vm->set_prototype, key_val);
            if (IS_POINTER(func)) {
                JSFunction* f = (JSFunction*)get_pointer(func);
                if (f->bytecode_offset == 0xffffffff) {
                    return create_bound_native_function(f->native_ptr, f->name, obj_val);
                }
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_PROMISE) {
        JSString* str = (JSString*)get_pointer(key_val);
        if (str && str->length == 4 && strcmp(str->data, "then") == 0) {
            extern Value js_promise_then(struct VM* vm, Value this_val, int arg_count, Value* args);
            return create_bound_native_function((void*)js_promise_then, key_val, obj_val);
        }
        if (str && str->length == 5 && strcmp(str->data, "catch") == 0) {
            extern Value js_promise_catch(struct VM* vm, Value this_val, int arg_count, Value* args);
            return create_bound_native_function((void*)js_promise_catch, key_val, obj_val);
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_STRING) {
        JSString* str = (JSString*)ptr;
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "length") == 0) {
            return make_integer((int32_t)str->length);
        }
        // String prototype methods (delegated via VM's string_prototype)
        if (g_current_vm && IS_POINTER(g_current_vm->string_prototype)) {
            Value func = object_get(g_current_vm->string_prototype, key_val);
            if (IS_POINTER(func)) {
                JSFunction* f = (JSFunction*)get_pointer(func);
                if (f->bytecode_offset == 0xffffffff) {
                    return create_bound_native_function(f->native_ptr, f->name, obj_val);
                }
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_ARRAY) {
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "length") == 0) {
            JSArray* arr = (JSArray*)ptr;
            return make_integer((int32_t)arr->length);
        }
        // Numeric index access
        if (key_str) {
            char* end;
            long idx = strtol(key_str->data, &end, 10);
            if (end != key_str->data && *end == '\0') {
                JSArray* arr = (JSArray*)ptr;
                if (idx >= 0 && (uint32_t)idx < arr->length) {
                    return arr->elements[idx];
                }
                return VAL_UNDEFINED;
            }
        }
        // Array prototype methods
        if (g_current_vm && IS_POINTER(g_current_vm->array_prototype)) {
            Value func = object_get(g_current_vm->array_prototype, key_val);
            if (IS_POINTER(func)) {
                JSFunction* f = (JSFunction*)get_pointer(func);
                if (f->bytecode_offset == 0xffffffff) {
                    return create_bound_native_function(f->native_ptr, f->name, obj_val);
                }
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_BUFFER) {
        if (IS_INTEGER(key_val) || IS_DOUBLE(key_val)) {
            double d = IS_INTEGER(key_val) ? (double)get_integer(key_val) : get_double(key_val);
            long idx = (long)d;
            JSBuffer* buf = (JSBuffer*)ptr;
            if (idx >= 0 && (uint32_t)idx < buf->length) {
                return make_integer(buf->data[idx]);
            }
            return VAL_UNDEFINED;
        }
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "length") == 0) {
            JSBuffer* buf = (JSBuffer*)ptr;
            return make_integer((int32_t)buf->length);
        }
        if (key_str && strcmp(key_str->data, "byteLength") == 0) {
            JSBuffer* buf = (JSBuffer*)ptr;
            return make_integer((int32_t)buf->length);
        }
        if (key_str && strcmp(key_str->data, "byteOffset") == 0) {
            return make_integer(0);
        }
        if (key_str && strcmp(key_str->data, "buffer") == 0) {
            JSBuffer* buf = (JSBuffer*)ptr;
            // Return slab_ref if it exists, otherwise undefined (duck-typing ArrayBuffer)
            return buf->slab_ref != VAL_UNDEFINED ? buf->slab_ref : obj_val;
        }
        // Numeric index access via string
        if (key_str) {
            char* end;
            long idx = strtol(key_str->data, &end, 10);
            if (end != key_str->data && *end == '\0') {
                JSBuffer* buf = (JSBuffer*)ptr;
                if (idx >= 0 && (uint32_t)idx < buf->length) {
                    return make_integer(buf->data[idx]);
                }
                return VAL_UNDEFINED;
            }
        }
        // Buffer prototype methods
        extern _Thread_local Value g_buffer_prototype; // We'll need to define this in vm.h or builtins.c
        if (g_current_vm && IS_POINTER(g_buffer_prototype)) {
            Value func = object_get(g_buffer_prototype, key_val);
            if (IS_POINTER(func)) {
                JSFunction* f = (JSFunction*)get_pointer(func);
                if (f->bytecode_offset == 0xffffffff) {
                    return create_bound_native_function(f->native_ptr, f->name, obj_val);
                }
            }
        }
        return VAL_UNDEFINED;
    }
    if (header->obj_type == OBJ_FLOAT16_ARRAY) {
        if (IS_INTEGER(key_val) || IS_DOUBLE(key_val)) {
            double d = IS_INTEGER(key_val) ? (double)get_integer(key_val) : get_double(key_val);
            long idx = (long)d;
            JSFloat16Array* arr = (JSFloat16Array*)ptr;
            if (idx >= 0 && (uint32_t)idx < arr->length) {
                extern float half_to_float(uint16_t h);
                return make_double(half_to_float(arr->data[idx]));
            }
            return VAL_UNDEFINED;
        }
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "length") == 0) {
            JSFloat16Array* arr = (JSFloat16Array*)ptr;
            return make_integer((int32_t)arr->length);
        }
        if (key_str) {
            char* end;
            long idx = strtol(key_str->data, &end, 10);
            if (end != key_str->data && *end == '\0') {
                JSFloat16Array* arr = (JSFloat16Array*)ptr;
                if (idx >= 0 && (uint32_t)idx < arr->length) {
                    extern float half_to_float(uint16_t h);
                    return make_double(half_to_float(arr->data[idx]));
                }
                return VAL_UNDEFINED;
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type == OBJ_ERROR) {
        JSError* err = (JSError*)ptr;
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "message") == 0) return err->message;
        if (key_str && strcmp(key_str->data, "name") == 0)    return err->name;
        if (key_str && strcmp(key_str->data, "stack") == 0)   return err->stack;
        
        for (uint32_t i = 0; i < err->count; i++) {
            if (err->properties[i].key == key_val) return err->properties[i].value;
        }
        if (key_str) {
            for (uint32_t i = 0; i < err->count; i++) {
                if (!IS_POINTER(err->properties[i].key)) continue;
                JSString* prop_key_str = (JSString*)get_pointer(err->properties[i].key);
                if (prop_key_str->length == key_str->length && memcmp(prop_key_str->data, key_str->data, key_str->length) == 0) {
                    return err->properties[i].value;
                }
            }
        }
        return VAL_UNDEFINED;
    }
    if (header->obj_type == OBJ_FUNCTION) {
        JSFunction* f = (JSFunction*)ptr;
        JSString* key_str = (JSString*)get_pointer(key_val);
        if (key_str && strcmp(key_str->data, "name") == 0) return f->name;
        if (key_str && strcmp(key_str->data, "length") == 0) return make_integer(0);
        
        // Function prototype methods
        if (g_current_vm && IS_POINTER(g_current_vm->function_prototype)) {
            Value func = object_get(g_current_vm->function_prototype, key_val);
            if (IS_POINTER(func)) {
                JSFunction* pf = (JSFunction*)get_pointer(func);
                if (pf->bytecode_offset == 0xffffffff) {
                    return create_bound_native_function(pf->native_ptr, pf->name, obj_val);
                }
            }
        }
        return VAL_UNDEFINED;
    }
    
    if (header->obj_type != OBJ_OBJECT) return VAL_UNDEFINED;
    
    Value curr = obj_val;
    int depth = 0;
    while (IS_POINTER(curr) && depth < 100) {
        void* curr_ptr = get_pointer(curr);
        BlockHeader* curr_header = (BlockHeader*)((char*)curr_ptr - sizeof(BlockHeader));
        if (curr_header->obj_type != OBJ_OBJECT) {
            // If the prototype chain reaches a non-OBJ_OBJECT, look it up normally.
            // But to avoid infinite recursion, we lookup directly using standard logic.
            // For example, if it's an Array or String.
            // Let's do a one-off object_get and return it if not undefined.
            // Wait, we shouldn't recurse infinitely, but since we check next_curr != curr, it's safe.
            Value res = object_get(curr, key_val);
            if (res != VAL_UNDEFINED) return res;
            break;
        }
        
        JSObject* obj = (JSObject*)curr_ptr;
        // Try interned pointer equality first (fast path)
        for (uint32_t i = 0; i < obj->count; i++) {
            if (obj->properties[i].key == key_val) {
                return maybe_bind_method(obj->properties[i].value, obj_val);
            }
        }
        // Fallback: string content equality (for non-interned keys)
        JSString* ks = (JSString*)get_pointer(key_val);
        if (ks) {
            bool found = false;
            Value found_val = VAL_UNDEFINED;
            for (uint32_t i = 0; i < obj->count; i++) {
                if (!IS_POINTER(obj->properties[i].key)) continue;
                JSString* pk = (JSString*)get_pointer(obj->properties[i].key);
                if (pk && pk->length == ks->length && memcmp(pk->data, ks->data, ks->length) == 0) {
                    found_val = obj->properties[i].value;
                    found = true;
                    break;
                }
            }
            if (found) return maybe_bind_method(found_val, obj_val);
        }
        
        // Resolve "__proto__"
        Value next_curr = VAL_UNDEFINED;
        for (uint32_t i = 0; i < obj->count; i++) {
            if (IS_POINTER(obj->properties[i].key)) {
                JSString* pk = (JSString*)get_pointer(obj->properties[i].key);
                if (pk && pk->length == 9 && memcmp(pk->data, "__proto__", 9) == 0) {
                    next_curr = obj->properties[i].value;
                    break;
                }
            }
        }
        if (!IS_POINTER(next_curr) || next_curr == curr) {
            break;
        }
        curr = next_curr;
        depth++;
    }
    return VAL_UNDEFINED;
}

void object_set(Value obj_val, Value key_val, Value value_val) {
    if (!IS_POINTER(obj_val) || !IS_POINTER(key_val)) return;
    
    if (g_current_vm) {
        vm_push_root(g_current_vm, obj_val);
        vm_push_root(g_current_vm, key_val);
        vm_push_root(g_current_vm, value_val);
    }
    
    void* ptr = get_pointer(obj_val);
    BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    
    if (header->obj_type == OBJ_FUNCTION) {
        JSFunction* f = (JSFunction*)ptr;
        JSFunction* target = get_bound_target(f);
        if (target) {
            object_set(make_pointer(target), key_val, value_val);
            if (g_current_vm) {
                vm_pop_root(g_current_vm);
                vm_pop_root(g_current_vm);
                vm_pop_root(g_current_vm);
            }
            return;
        }
    }
    
    Property** props_ptr;
    uint32_t* count_ptr;
    uint32_t* cap_ptr;
    
    if (header->obj_type == OBJ_OBJECT) {
        JSObject* obj = (JSObject*)ptr;
        props_ptr = &obj->properties;
        count_ptr = &obj->count;
        cap_ptr = &obj->capacity;
    } else if (header->obj_type == OBJ_FUNCTION) {
        JSFunction* f = (JSFunction*)ptr;
        props_ptr = &f->properties;
        count_ptr = &f->count;
        cap_ptr = &f->capacity;
    } else if (header->obj_type == OBJ_ERROR) {
        JSError* err = (JSError*)ptr;
        props_ptr = &err->properties;
        count_ptr = &err->count;
        cap_ptr = &err->capacity;
    } else if (header->obj_type == OBJ_BUFFER) {
        if (IS_INTEGER(key_val) || IS_DOUBLE(key_val)) {
            double d = IS_INTEGER(key_val) ? (double)get_integer(key_val) : get_double(key_val);
            long idx = (long)d;
            JSBuffer* buf = (JSBuffer*)ptr;
            if (idx >= 0 && (uint32_t)idx < buf->length) {
                uint8_t byte_val = 0;
                if (IS_INTEGER(value_val)) byte_val = (uint8_t)get_integer(value_val);
                else if (IS_DOUBLE(value_val)) byte_val = (uint8_t)get_double(value_val);
                buf->data[idx] = byte_val;
            }
            if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
            return;
        }
        if (IS_POINTER(key_val)) {
            JSString* ks = (JSString*)get_pointer(key_val);
            if (ks) {
                char* end;
                long idx = strtol(ks->data, &end, 10);
                if (end != ks->data && *end == '\0') {
                    JSBuffer* buf = (JSBuffer*)ptr;
                    if (idx >= 0 && (uint32_t)idx < buf->length) {
                        uint8_t byte_val = 0;
                        if (IS_INTEGER(value_val)) byte_val = (uint8_t)get_integer(value_val);
                        else if (IS_DOUBLE(value_val)) byte_val = (uint8_t)get_double(value_val);
                        buf->data[idx] = byte_val;
                    }
                }
            }
        }
        if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
        return;
    } else if (header->obj_type == OBJ_FLOAT16_ARRAY) {
        if (IS_INTEGER(key_val) || IS_DOUBLE(key_val)) {
            double d = IS_INTEGER(key_val) ? (double)get_integer(key_val) : get_double(key_val);
            long idx = (long)d;
            JSFloat16Array* arr = (JSFloat16Array*)ptr;
            if (idx >= 0 && (uint32_t)idx < arr->length) {
                double d_val = 0.0;
                if (IS_INTEGER(value_val)) d_val = get_integer(value_val);
                else if (IS_DOUBLE(value_val)) d_val = get_double(value_val);
                extern uint16_t float_to_half(float f);
                arr->data[idx] = float_to_half((float)d_val);
            }
            if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
            return;
        }
        if (IS_POINTER(key_val)) {
            JSString* ks = (JSString*)get_pointer(key_val);
            if (ks) {
                char* end;
                long idx = strtol(ks->data, &end, 10);
                if (end != ks->data && *end == '\0') {
                    JSFloat16Array* arr = (JSFloat16Array*)ptr;
                    if (idx >= 0 && (uint32_t)idx < arr->length) {
                        double d_val = 0.0;
                        if (IS_INTEGER(value_val)) d_val = get_integer(value_val);
                        else if (IS_DOUBLE(value_val)) d_val = get_double(value_val);
                        extern uint16_t float_to_half(float f);
                        arr->data[idx] = float_to_half((float)d_val);
                    }
                }
            }
        }
        if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
        return;
    } else {
        if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
        return;
    }
    
    // Update existing key if found
    for (uint32_t i = 0; i < *count_ptr; i++) {
        Value k = (*props_ptr)[i].key;
        int match = 0;
        if (k == key_val) {
            match = 1;
        } else if (IS_POINTER(k) && IS_POINTER(key_val)) {
            BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(k) - sizeof(BlockHeader));
            BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(key_val) - sizeof(BlockHeader));
            if (h1->obj_type == OBJ_STRING && h2->obj_type == OBJ_STRING) {
                JSString* s1 = (JSString*)get_pointer(k);
                JSString* s2 = (JSString*)get_pointer(key_val);
                if (s1->length == s2->length && memcmp(s1->data, s2->data, s1->length) == 0) {
                    match = 1;
                }
            }
        }
        if (match) {
            (*props_ptr)[i].value = value_val;
            gc_write_barrier(obj_val, value_val);
            if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
            return;
        }
    }
    
    // Insert new property
    if (*count_ptr >= *cap_ptr) {
        uint32_t old_cap = *cap_ptr;
        uint32_t new_cap = old_cap == 0 ? 4 : old_cap * 2;
        Property* old_props = *props_ptr;
        Property* new_props = arena_alloc(OBJ_OBJECT_DATA, new_cap * sizeof(Property));
        
        if (g_current_vm) {
            obj_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 3];
            key_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 2];
            value_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
            
            ptr = get_pointer(obj_val);
            header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
            if (header->obj_type == OBJ_OBJECT) {
                JSObject* obj = (JSObject*)ptr;
                props_ptr = &obj->properties;
                count_ptr = &obj->count;
                cap_ptr = &obj->capacity;
            } else if (header->obj_type == OBJ_FUNCTION) {
                JSFunction* f = (JSFunction*)ptr;
                props_ptr = &f->properties;
                count_ptr = &f->count;
                cap_ptr = &f->capacity;
            } else if (header->obj_type == OBJ_ERROR) {
                JSError* err = (JSError*)ptr;
                props_ptr = &err->properties;
                count_ptr = &err->count;
                cap_ptr = &err->capacity;
            }
            old_props = *props_ptr; // Re-fetch old_props
        }
        
        if (old_cap > 0) {
            memcpy(new_props, old_props, old_cap * sizeof(Property));
        }
        *props_ptr = new_props;
        *cap_ptr = new_cap;
    }
    (*props_ptr)[*count_ptr].key = key_val;
    (*props_ptr)[*count_ptr].value = value_val;
    (*count_ptr)++;
    gc_write_barrier(obj_val, key_val);
    gc_write_barrier(obj_val, value_val);
    if (g_current_vm) { vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); vm_pop_root(g_current_vm); }
}

Value create_function(struct CompiledProgram* prog, uint32_t offset, uint32_t regs, uint32_t params, bool is_async, Value env, Value name) {
    if (g_current_vm) {
        vm_push_root(g_current_vm, env);
        vm_push_root(g_current_vm, name);
    }
    
    Value func_val = make_pointer(arena_alloc(OBJ_FUNCTION, sizeof(JSFunction)));
    if (g_current_vm) vm_push_root(g_current_vm, func_val);
    
    JSFunction* f = (JSFunction*)get_pointer(func_val);
    f->prog = prog;
    f->bytecode_offset = offset;
    f->register_count = regs;
    f->param_count = params;
    f->is_async = is_async;
    f->native_ptr = NULL;
    f->count = 0;
    f->capacity = 4;
    f->user_data = NULL;
    
    Property* props = arena_alloc(OBJ_OBJECT_DATA, 4 * sizeof(Property));
    
    if (g_current_vm) {
        func_val = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        name = g_current_vm->gc_roots[g_current_vm->gc_root_count - 2];
        env = g_current_vm->gc_roots[g_current_vm->gc_root_count - 3];
        f = (JSFunction*)get_pointer(func_val);
    }
    
    f->env = env;
    f->name = name;
    f->properties = props;
    
    if (g_current_vm) {
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
    }
    return func_val;
}

Value create_native_function(void* native_ptr, Value name) {
    if (g_current_vm) vm_push_root(g_current_vm, name);
    Value func_val = make_pointer(arena_alloc(OBJ_FUNCTION, sizeof(JSFunction)));
    if (g_current_vm) {
        name = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        vm_pop_root(g_current_vm);
    }
    JSFunction* func = (JSFunction*)get_pointer(func_val);
    func->bytecode_offset = 0xffffffff;
    func->register_count = 0;
    func->param_count = 0;
    func->env = VAL_UNDEFINED;
    func->name = name;
    func->native_ptr = native_ptr;
    func->properties = NULL;
    func->count = 0;
    func->capacity = 0;
    return func_val;
}

Value create_bound_native_function(void* native_ptr, Value name, Value env) {
    if (g_current_vm) {
        vm_push_root(g_current_vm, name);
        vm_push_root(g_current_vm, env);
    }
    Value func_val = make_pointer(arena_alloc(OBJ_FUNCTION, sizeof(JSFunction)));
    if (g_current_vm) {
        env = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        name = g_current_vm->gc_roots[g_current_vm->gc_root_count - 2];
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
    }
    JSFunction* func = (JSFunction*)get_pointer(func_val);
    func->bytecode_offset = 0xffffffff;
    func->register_count = 0;
    func->param_count = 0;
    func->env = env;
    func->name = name;
    func->native_ptr = native_ptr;
    func->properties = NULL;
    func->count = 0;
    func->capacity = 0;
    return func_val;
}

Value create_bound_bytecode_function(JSFunction* target, Value bound_this) {
    if (g_current_vm) {
        vm_push_root(g_current_vm, bound_this);
    }
    Value func_val = make_pointer(arena_alloc(OBJ_FUNCTION, sizeof(JSFunction)));
    if (g_current_vm) {
        bound_this = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        vm_pop_root(g_current_vm);
    }
    JSFunction* func = (JSFunction*)get_pointer(func_val);
    func->prog = target->prog;
    func->bytecode_offset = target->bytecode_offset;
    func->register_count = target->register_count;
    func->param_count = target->param_count;
    func->is_async = target->is_async;
    func->env = bound_this; // Stores bound this
    func->name = target->name;
    func->native_ptr = target; // Pointer to original function
    func->properties = NULL;
    func->count = 0;
    func->capacity = 0;
    func->user_data = NULL;
    return func_val;
}

Value create_environment(Value parent, uint32_t count) {
    if (g_current_vm) vm_push_root(g_current_vm, parent);
    Value env_val = make_pointer(arena_alloc(OBJ_ENV, sizeof(JSEnvironment) + count * sizeof(Value)));
    if (g_current_vm) {
        parent = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        vm_pop_root(g_current_vm);
    }
    JSEnvironment* env = (JSEnvironment*)get_pointer(env_val);
    env->parent = parent;
    env->count = count;
    for (uint32_t i = 0; i < count; i++) {
        env->values[i] = VAL_UNINITIALIZED;  // TDZ: initialized to sentinel
    }
    return env_val;
}

static _Thread_local uint32_t g_next_symbol_id = 1;

Value create_symbol(Value description) {
    JSSymbol* sym = arena_alloc(OBJ_SYMBOL, sizeof(JSSymbol));
    sym->description = description;
    sym->id = g_next_symbol_id++;
    return make_pointer(sym);
}

Value create_iterator_helper(Value source, Value callback, uint8_t helper_type, int32_t count) {
    JSIteratorHelper* h = arena_alloc(OBJ_ITERATOR_HELPER, sizeof(JSIteratorHelper));
    h->source = source;
    h->callback = callback;
    h->count = count;
    h->inner = VAL_NULL;
    h->helper_type = helper_type;
    h->done = 0;
    return make_pointer(h);
}

Value create_error(const char* type_name, Value message) {
    JSError* err = arena_alloc(OBJ_ERROR, sizeof(JSError));
    err->name = create_string(type_name, strlen(type_name));
    err->message = message;
    err->stack = VAL_UNDEFINED;
    err->count = 0;
    err->capacity = 0;
    err->properties = NULL;
    return make_pointer(err);
}

Value create_string_from_chars(const char* str, int len) {
    // Like create_string but skips interning (for dynamic strings)
    JSString* s = arena_alloc(OBJ_STRING, sizeof(JSString) + len + 1);
    s->length = len;
    s->hash = hash_string(str, len);
    memcpy(s->data, str, len);
    s->data[len] = '\0';
    return make_pointer(s);
}

// Value coercion helpers
Value value_to_string(Value v) {
    if (IS_POINTER(v)) {
        void* ptr = get_pointer(v);
        if (!ptr) return create_string("null", 4);
        BlockHeader* hdr = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        if (hdr->obj_type == OBJ_STRING) return v;
        if (hdr->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)ptr;
            // Build comma-joined string
            char buf[256];
            int pos = 0;
            buf[0] = '\0';
            for (uint32_t i = 0; i < arr->length && pos < 250; i++) {
                if (i > 0) buf[pos++] = ',';
                Value elem_str = value_to_string(arr->elements[i]);
                if (IS_POINTER(elem_str)) {
                    JSString* es = (JSString*)get_pointer(elem_str);
                    int rem = 250 - pos;
                    int copy = (int)es->length < rem ? (int)es->length : rem;
                    memcpy(buf + pos, es->data, copy);
                    pos += copy;
                }
            }
            buf[pos] = '\0';
            return create_string(buf, pos);
        }
        if (hdr->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)ptr;
            return create_string_from_chars((const char*)buf->data, buf->length);
        }
        if (hdr->obj_type == OBJ_FUNCTION) return create_string("function", 8);
        if (hdr->obj_type == OBJ_ERROR) {
            JSError* err = (JSError*)ptr;
            // name: message
            char buf[256];
            JSString* name_s = IS_POINTER(err->name) ? (JSString*)get_pointer(err->name) : NULL;
            JSString* msg_s  = IS_POINTER(err->message) ? (JSString*)get_pointer(err->message) : NULL;
            int len = snprintf(buf, sizeof(buf), "%s: %s",
                name_s ? name_s->data : "Error",
                msg_s  ? msg_s->data  : "");
            return create_string(buf, len);
        }
        return create_string("[object Object]", 15);
    }
    if (IS_DOUBLE(v)) {
        char buf[64];
        double d = get_double(v);
        // Check special values
        if (d != d) return create_string("NaN", 3);
        if (d == (double)(int64_t)d && d >= -1e15 && d <= 1e15) {
            int len = snprintf(buf, sizeof(buf), "%.0f", d);
            return create_string(buf, len);
        }
        int len = snprintf(buf, sizeof(buf), "%g", d);
        return create_string(buf, len);
    }
    if (IS_INTEGER(v)) {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%d", get_integer(v));
        return create_string(buf, len);
    }
    if (IS_BOOLEAN(v)) {
        return get_boolean(v) ? create_string("true", 4) : create_string("false", 5);
    }
    if (IS_NULL(v))      return create_string("null", 4);
    if (IS_UNDEFINED(v)) return create_string("undefined", 9);
    if (IS_SYMBOL(v))    return create_string("Symbol()", 8);
    return create_string("", 0);
}

Value value_to_number(Value v) {
    if (IS_DOUBLE(v) || IS_INTEGER(v)) return v;
    if (IS_BOOLEAN(v)) return make_integer(get_boolean(v) ? 1 : 0);
    if (IS_NULL(v))    return make_integer(0);
    if (IS_UNDEFINED(v)) return make_double(0.0/0.0); // NaN
    if (IS_POINTER(v)) {
        Value sv = value_to_string(v);
        JSString* s = (JSString*)get_pointer(sv);
        if (!s || s->length == 0) return make_integer(0);
        char* end;
        double d = strtod(s->data, &end);
        if (end == s->data) return make_double(0.0/0.0);
        return make_double(d);
    }
    return make_double(0.0/0.0);
}

bool values_strict_equal(Value a, Value b) {
    if (IS_DOUBLE(a) && IS_DOUBLE(b)) return get_double(a) == get_double(b);
    if (IS_INTEGER(a) && IS_INTEGER(b)) return get_integer(a) == get_integer(b);
    if (IS_DOUBLE(a) && IS_INTEGER(b)) return get_double(a) == (double)get_integer(b);
    if (IS_INTEGER(a) && IS_DOUBLE(b)) return (double)get_integer(a) == get_double(b);
    if (IS_POINTER(a) && IS_POINTER(b)) {
        if (get_pointer(a) == get_pointer(b)) return true;
        // String value equality
        void* pa = get_pointer(a);
        void* pb = get_pointer(b);
        BlockHeader* ha = (BlockHeader*)((char*)pa - sizeof(BlockHeader));
        BlockHeader* hb = (BlockHeader*)((char*)pb - sizeof(BlockHeader));
        if (ha->obj_type == OBJ_STRING && hb->obj_type == OBJ_STRING) {
            JSString* sa = (JSString*)pa;
            JSString* sb = (JSString*)pb;
            return sa->length == sb->length && memcmp(sa->data, sb->data, sa->length) == 0;
        }
        return false;
    }
    return a == b;
}

bool values_abstract_equal(Value a, Value b) {
    // Same type: use strict equality
    if (GET_TAG(a) == GET_TAG(b) || (IS_DOUBLE(a) && IS_DOUBLE(b))) {
        return values_strict_equal(a, b);
    }
    // null == undefined
    if ((IS_NULL(a) && IS_UNDEFINED(b)) || (IS_UNDEFINED(a) && IS_NULL(b))) return true;
    // number vs string: convert string to number
    if ((IS_DOUBLE(a) || IS_INTEGER(a)) && IS_POINTER(b)) {
        Value nb = value_to_number(b);
        return values_strict_equal(a, nb);
    }
    if (IS_POINTER(a) && (IS_DOUBLE(b) || IS_INTEGER(b))) {
        Value na = value_to_number(a);
        return values_strict_equal(na, b);
    }
    // boolean: convert to number
    if (IS_BOOLEAN(a)) return values_abstract_equal(make_integer(get_boolean(a) ? 1 : 0), b);
    if (IS_BOOLEAN(b)) return values_abstract_equal(a, make_integer(get_boolean(b) ? 1 : 0));
    return false;
}

// GC Mark Helper
/*
 * ===========================================================================
 * GARBAGE COLLECTION: MARK PHASE
 * ===========================================================================
 * The GC relies on NaN-boxing. It traverses the object graph by recursively
 * checking if a Value is a pointer (indicated by the 0xFFF8 tag).
 * 
 * If a valid pointer is found within the arena bounds, the header's gc_mark
 * is set to 1. It then recursively marks all child properties.
 */
void gc_mark_value(Value val) {
    if (!IS_POINTER(val)) return;
    
    void* ptr = get_pointer(val);
    if (!is_ptr_in_arena(ptr)) return;
    
    BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
    if (header->gc_mark) return;
    
    header->gc_mark = 1;
    
    switch (header->obj_type) {
        case OBJ_STRING:
            break;
        case OBJ_ARRAY: {
            JSArray* arr = (JSArray*)ptr;
            gc_mark_value(make_pointer(arr->elements));
            for (uint32_t i = 0; i < arr->length; i++) {
                gc_mark_value(arr->elements[i]);
            }
            break;
        }
        case OBJ_ARRAY_DATA: {
            Value* elements = (Value*)ptr;
            size_t count = (header->size - sizeof(BlockHeader)) / sizeof(Value);
            for (size_t i = 0; i < count; i++) {
                gc_mark_value(elements[i]);
            }
            break;
        }
        case OBJ_OBJECT: {
            JSObject* obj = (JSObject*)ptr;
            gc_mark_value(make_pointer(obj->properties));
            for (uint32_t i = 0; i < obj->count; i++) {
                gc_mark_value(obj->properties[i].key);
                gc_mark_value(obj->properties[i].value);
            }
            break;
        }
        case OBJ_SET: {
            JSSet* set = (JSSet*)ptr;
            gc_mark_value(make_pointer(set->elements));
            for (uint32_t i = 0; i < set->capacity; i++) {
                gc_mark_value(set->elements[i]);
            }
            break;
        }
        case OBJ_FLOAT16_ARRAY: {
            JSFloat16Array* arr = (JSFloat16Array*)ptr;
            gc_mark_value(make_pointer(arr->data));
            break;
        }
        case OBJ_PROMISE: {
            JSPromise* prom = (JSPromise*)ptr;
            gc_mark_value(prom->result);
            gc_mark_value(prom->then_callbacks);
            break;
        }
        case OBJ_OBJECT_DATA: {
            Property* props = (Property*)ptr;
            size_t count = (header->size - sizeof(BlockHeader)) / sizeof(Property);
            for (size_t i = 0; i < count; i++) {
                gc_mark_value(props[i].key);
                gc_mark_value(props[i].value);
            }
            break;
        }
        case OBJ_FUNCTION: {
            JSFunction* func = (JSFunction*)ptr;
            gc_mark_value(func->env);
            gc_mark_value(func->name);
            if (func->bytecode_offset != 0xffffffff && func->native_ptr != NULL) {
                gc_mark_value(make_pointer(func->native_ptr));
            }
            if (func->properties) {
                gc_mark_value(make_pointer(func->properties));
            }
            break;
        }
        case OBJ_ENV: {
            JSEnvironment* env = (JSEnvironment*)ptr;
            gc_mark_value(env->parent);
            for (uint32_t i = 0; i < env->count; i++) {
                gc_mark_value(env->values[i]);
            }
            break;
        }
        case OBJ_ITERATOR_HELPER: {
            JSIteratorHelper* h = (JSIteratorHelper*)ptr;
            gc_mark_value(h->source);
            gc_mark_value(h->callback);
            gc_mark_value(h->inner);
            break;
        }
        case OBJ_SYMBOL: {
            JSSymbol* sym = (JSSymbol*)ptr;
            gc_mark_value(sym->description);
            break;
        }
        case OBJ_REGEXP: {
            JSRegExp* re = (JSRegExp*)ptr;
            gc_mark_value(re->source);
            gc_mark_value(re->flags);
            break;
        }
        case OBJ_ERROR: {
            JSError* err = (JSError*)ptr;
            gc_mark_value(err->name);
            gc_mark_value(err->message);
            gc_mark_value(err->stack);
            if (err->properties) {
                gc_mark_value(make_pointer(err->properties));
            }
            break;
        }
        case OBJ_BUFFER: {
            JSBuffer* buf = (JSBuffer*)ptr;
            if (buf->slab_ref != VAL_UNDEFINED) {
                gc_mark_value(buf->slab_ref);
            }
            break;
        }
        case OBJ_BUFFER_DATA:
            // Raw bytes, nothing to mark
            break;
    }
}

// Precise Mark-and-Sweep GC
/*
 * ===========================================================================
 * GARBAGE COLLECTION: PRECISE MARK-AND-SWEEP
 * ===========================================================================
 * Triggered heuristically when memory pressure builds.
 * 
 * Phase 1: Marking. Recursively marks from the GC Roots (global object,
 * execution stack frames, and active VM registers).
 * 
 * Phase 2: Sweeping. Linearly scans the entire contiguous arena. Any block
 * that lacks a gc_mark is instantly zeroed out and merged with adjacent
 * free blocks to mitigate heap fragmentation without invoking OS free().
 */
static void minor_trace_object(void* ptr, ObjType type);

void gc_mark_value_ptr(Value* val_ptr) {
    gc_mark_value(*val_ptr);
}

void minor_copy_value(Value* val_ptr) {
    Value v = *val_ptr;
    if (!IS_POINTER(v)) return;
    void* ptr = get_pointer(v);
    
    if ((const char*)ptr >= g_nursery.start && (const char*)ptr < g_nursery.end) {
        BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        if (h->gc_mark == 2) {
            *val_ptr = *(Value*)ptr; // Forwarding pointer
        } else {
            void* new_ptr = arena_alloc_old_space(h->obj_type, h->size - sizeof(BlockHeader));
            if (!new_ptr) {
                fprintf(stderr, "Fatal: Old Space exhausted during minor GC\n");
                exit(1);
            }
            memcpy(new_ptr, ptr, h->size - sizeof(BlockHeader));
            h->gc_mark = 2;
            Value new_val = make_pointer(new_ptr);
            *(Value*)ptr = new_val;
            *val_ptr = new_val;
            
            minor_trace_object(new_ptr, h->obj_type);
        }
    }
}

static void minor_trace_object(void* ptr, ObjType type) {
    switch (type) {
        case OBJ_OBJECT: {
            JSObject* obj = (JSObject*)ptr;
            Value props_val = make_pointer(obj->properties);
            minor_copy_value(&props_val);
            obj->properties = (Property*)get_pointer(props_val);
            if (obj->properties && (const char*)obj->properties >= g_old_space.start && (const char*)obj->properties < g_old_space.end) {
                minor_trace_object(obj->properties, OBJ_OBJECT_DATA);
            }
            break;
        }
        case OBJ_OBJECT_DATA: {
            Property* props = (Property*)ptr;
            BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
            size_t count = (h->size - sizeof(BlockHeader)) / sizeof(Property);
            for (size_t i = 0; i < count; i++) {
                minor_copy_value(&props[i].key);
                minor_copy_value(&props[i].value);
            }
            break;
        }
        case OBJ_ARRAY: {
            JSArray* arr = (JSArray*)ptr;
            Value elem_val = make_pointer(arr->elements);
            minor_copy_value(&elem_val);
            arr->elements = (Value*)get_pointer(elem_val);
            if (arr->elements && (const char*)arr->elements >= g_old_space.start && (const char*)arr->elements < g_old_space.end) {
                minor_trace_object(arr->elements, OBJ_ARRAY_DATA);
            }
            break;
        }
        case OBJ_ARRAY_DATA: {
            Value* elements = (Value*)ptr;
            BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
            size_t count = (h->size - sizeof(BlockHeader)) / sizeof(Value);
            for (size_t i = 0; i < count; i++) {
                minor_copy_value(&elements[i]);
            }
            break;
        }
        case OBJ_FUNCTION: {
            JSFunction* f = (JSFunction*)ptr;
            minor_copy_value(&f->env);
            minor_copy_value(&f->name);
            if (f->bytecode_offset != 0xffffffff && f->native_ptr != NULL) {
                Value target_val = make_pointer(f->native_ptr);
                minor_copy_value(&target_val);
                f->native_ptr = get_pointer(target_val);
            }
            if (f->properties) {
                Value prop_val = make_pointer(f->properties);
                minor_copy_value(&prop_val);
                f->properties = (Property*)get_pointer(prop_val);
                if ((const char*)f->properties >= g_old_space.start && (const char*)f->properties < g_old_space.end) {
                    minor_trace_object(f->properties, OBJ_OBJECT_DATA);
                }
            }
            break;
        }
        case OBJ_ENV: {
            JSEnvironment* env = (JSEnvironment*)ptr;
            minor_copy_value(&env->parent);
            for (uint32_t i = 0; i < env->count; i++) {
                minor_copy_value(&env->values[i]);
            }
            break;
        }
        case OBJ_SET: {
            JSSet* set = (JSSet*)ptr;
            Value elem_val = make_pointer(set->elements);
            minor_copy_value(&elem_val);
            set->elements = (Value*)get_pointer(elem_val);
            if (set->elements && (const char*)set->elements >= g_old_space.start && (const char*)set->elements < g_old_space.end) {
                minor_trace_object(set->elements, OBJ_ARRAY_DATA);
            }
            break;
        }
        case OBJ_PROMISE: {
            JSPromise* p = (JSPromise*)ptr;
            minor_copy_value(&p->result);
            minor_copy_value(&p->then_callbacks);
            break;
        }
        case OBJ_ITERATOR_HELPER: {
            JSIteratorHelper* it = (JSIteratorHelper*)ptr;
            minor_copy_value(&it->source);
            minor_copy_value(&it->callback);
            minor_copy_value(&it->inner);
            break;
        }
        case OBJ_ERROR: {
            JSError* err = (JSError*)ptr;
            minor_copy_value(&err->message);
            minor_copy_value(&err->stack);
            if (err->properties) {
                Value prop_val = make_pointer(err->properties);
                minor_copy_value(&prop_val);
                err->properties = (Property*)get_pointer(prop_val);
                if ((const char*)err->properties >= g_old_space.start && (const char*)err->properties < g_old_space.end) {
                    minor_trace_object(err->properties, OBJ_OBJECT_DATA);
                }
            }
            break;
        }
        default: break;
    }
}

void gc_minor(VM* vm) {
    if (!vm) return;
    
    // Mark roots via minor_copy_value
    minor_copy_value(&vm->global_obj);
    minor_copy_value(&vm->module_cache);
    minor_copy_value(&vm->set_prototype);
    minor_copy_value(&vm->array_prototype);
    minor_copy_value(&vm->string_prototype);
    minor_copy_value(&vm->float16_array_prototype);
    minor_copy_value(&vm->promise_prototype);
    minor_copy_value(&vm->iterator_prototype);
    minor_copy_value(&vm->error_prototype);
    minor_copy_value(&vm->symbol_iterator);
    minor_copy_value(&vm->symbol_dispose);
    minor_copy_value(&vm->symbol_async_dispose);
    
    for (uint32_t i = 0; i < vm->reg_count; i++) {
        minor_copy_value(&vm->registers[i]);
    }
    
    for (uint32_t i = 0; i < vm->const_pool_size; i++) {
        minor_copy_value(&vm->const_pool[i]);
    }
    
    MicrotaskEntry* curr_m = vm->microtask_head;
    while (curr_m) {
        minor_copy_value(&curr_m->callback);
        minor_copy_value(&curr_m->arg);
        curr_m = curr_m->next;
    }
    
    NextTickEntry* curr_n = vm->next_tick_head;
    while (curr_n) {
        minor_copy_value(&curr_n->callback);
        minor_copy_value(&curr_n->arg);
        curr_n = curr_n->next;
    }
    
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        minor_copy_value(&vm->frames[i].func);
        minor_copy_value(&vm->frames[i].env);
    }
    
    for (uint32_t i = 0; i < vm->gc_root_count; i++) {
        minor_copy_value(&vm->gc_roots[i]);
    }
    
    // Remembered Set
    for (int i = 0; i < g_remembered_count; i++) {
        Value old_obj = g_remembered_set[i];
        if (!IS_POINTER(old_obj)) continue;
        void* ptr = get_pointer(old_obj);
        BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        h->gc_mark &= ~2; // Clear remembered bit
        minor_trace_object(ptr, h->obj_type); // Trace the old object to copy any updated nursery references
    }
    g_remembered_count = 0;
    
    // Interned strings
    int write_idx = 0;
    for (int i = 0; i < g_interned_count; i++) {
        Value s_val = g_interned_strings[i];
        void* ptr = get_pointer(s_val);
        if ((const char*)ptr >= g_nursery.start && (const char*)ptr < g_nursery.end) {
            BlockHeader* h = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
            if (h->gc_mark == 2) {
                g_interned_strings[write_idx++] = *(Value*)ptr; // Update pointer
            }
        } else {
            g_interned_strings[write_idx++] = s_val; // Keep old space strings
        }
    }
    g_interned_count = write_idx;
    
    // Trace thread pool pending tasks
    tp_mark_gc_roots(minor_copy_value);

    // Trace child process IO tasks
    extern void child_process_mark_gc_roots(void*);
    child_process_mark_gc_roots((void*)minor_copy_value);
    
    extern void net_mark_gc_roots(void*);
    net_mark_gc_roots((void*)minor_copy_value);
    extern void dgram_mark_gc_roots(void*);
    dgram_mark_gc_roots((void*)minor_copy_value);
    extern void zlib_mark_gc_roots(void*);
    zlib_mark_gc_roots((void*)minor_copy_value);
    
    extern void http_mark_gc_roots(void*);
    http_mark_gc_roots((void*)minor_copy_value);
    
    extern void ws_mark_gc_roots(void*);
    ws_mark_gc_roots((void*)minor_copy_value);

    // Trace event loop active tasks/IO/timers
    el_trace_roots(minor_copy_value);

    // Empty the nursery
    g_nursery.bump = g_nursery.start;
}

void gc_major(VM* vm) {
    if (!vm) return;
    
    // Always empty nursery first
    gc_minor(vm);
    
    // Phase 1: Mark roots
    gc_mark_value(vm->global_obj);
    gc_mark_value(vm->module_cache);
    gc_mark_value(vm->set_prototype);
    gc_mark_value(vm->array_prototype);
    gc_mark_value(vm->string_prototype);
    gc_mark_value(vm->float16_array_prototype);
    gc_mark_value(vm->promise_prototype);
    gc_mark_value(vm->iterator_prototype);
    gc_mark_value(vm->error_prototype);
    gc_mark_value(vm->symbol_iterator);
    gc_mark_value(vm->symbol_dispose);
    gc_mark_value(vm->symbol_async_dispose);
    
    // Mark VM registers
    for (uint32_t i = 0; i < vm->reg_count; i++) {
        gc_mark_value(vm->registers[i]);
    }
    
    for (uint32_t i = 0; i < vm->const_pool_size; i++) {
        gc_mark_value(vm->const_pool[i]);
    }
    
    MicrotaskEntry* curr_m = vm->microtask_head;
    while (curr_m) {
        gc_mark_value(curr_m->callback);
        gc_mark_value(curr_m->arg);
        curr_m = curr_m->next;
    }
    
    NextTickEntry* curr_n = vm->next_tick_head;
    while (curr_n) {
        gc_mark_value(curr_n->callback);
        gc_mark_value(curr_n->arg);
        curr_n = curr_n->next;
    }
    
    // Mark frames
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        gc_mark_value(vm->frames[i].func);
        gc_mark_value(vm->frames[i].env);
    }
    
    // Mark explicit C-stack anchored roots
    for (uint32_t i = 0; i < vm->gc_root_count; i++) {
        gc_mark_value(vm->gc_roots[i]);
    }
    
    // Clean up weak references in the string interning table before sweeping.
    // If a string is not marked, remove it from the interning list so it gets collected.
    int write_idx = 0;
    for (int i = 0; i < g_interned_count; i++) {
        void* ptr = get_pointer(g_interned_strings[i]);
        BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        if (header->gc_mark) {
            g_interned_strings[write_idx++] = g_interned_strings[i];
        }
    }
    g_interned_count = write_idx;
    
    // Trace thread pool pending tasks
    tp_mark_gc_roots(gc_mark_value_ptr);
    
    // Trace child process IO tasks
    extern void child_process_mark_gc_roots(void*);
    child_process_mark_gc_roots((void*)gc_mark_value_ptr);
    
    extern void net_mark_gc_roots(void*);
    net_mark_gc_roots((void*)gc_mark_value_ptr);
    extern void dgram_mark_gc_roots(void*);
    dgram_mark_gc_roots((void*)gc_mark_value_ptr);
    extern void zlib_mark_gc_roots(void*);
    zlib_mark_gc_roots((void*)gc_mark_value_ptr);
    
    extern void http_mark_gc_roots(void*);
    http_mark_gc_roots((void*)gc_mark_value_ptr);
    
    extern void ws_mark_gc_roots(void*);
    ws_mark_gc_roots((void*)gc_mark_value_ptr);
    
    // Trace event loop active tasks/IO/timers
    el_trace_roots(gc_mark_value_ptr);
    
    // Phase 1.5: N-API Object Wraps
    // Finalize any wrapped native objects whose JS targets are unreferenced.
    extern void napi_sweep_wraps(void* env);
    napi_sweep_wraps((void*)vm);
    
    // Phase 2: Sweep and coalesce ONLY in old space!
    BlockHeader* block = (BlockHeader*)g_old_space.start;
    BlockHeader* prev = NULL;
    
    while ((char*)block < g_old_space.end) {
        if (!block->is_free) {
            if (block->gc_mark) {
                block->gc_mark = 0; // Unmark for next GC
            } else {
                if (block->obj_type == OBJ_BUFFER) {
                    JSBuffer* buf = (JSBuffer*)((char*)block + sizeof(BlockHeader));
                    if (!buf->is_external && buf->data && buf->slab_ref == VAL_UNDEFINED) {
                        free(buf->data);
                    }
                }
                block->is_free = 1; // Free unreferenced block
                g_old_space_free_bytes += block->size;
            }
        }
        
        if (block->is_free) {
            block->gc_mark = 0;
            // Coalesce with previous block if it is also free
            if (prev && prev->is_free) {
                prev->size += block->size;
                block = prev;
            }
        }
        
        prev = block;
        block = (BlockHeader*)((char*)block + block->size);
    }
}

void gc_write_barrier(Value old_obj, Value new_obj) {
    if (!IS_POINTER(old_obj) || !IS_POINTER(new_obj)) return;
    
    void* old_ptr = get_pointer(old_obj);
    void* new_ptr = get_pointer(new_obj);
    
    // If old object is in old space and new object is in nursery
    if ((const char*)old_ptr >= g_old_space.start && (const char*)old_ptr < g_old_space.end) {
        if ((const char*)new_ptr >= g_nursery.start && (const char*)new_ptr < g_nursery.end) {
            BlockHeader* h = (BlockHeader*)((char*)old_ptr - sizeof(BlockHeader));
            // We use gc_mark = 2 as a 'remembered' bit during normal execution
            if (g_remembered_count < MAX_REMEMBERED_SET && (h->gc_mark & 2) == 0) {
                h->gc_mark |= 2;
                g_remembered_set[g_remembered_count++] = old_obj;
            }
        }
    }
}

// SplitMix64 hash function for Value
static inline uint32_t set_hash_value(Value v) {
    uint64_t x = v;
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (uint32_t)x;
}

Value create_set(void) {
    JSSet* set = (JSSet*)arena_alloc(OBJ_SET, sizeof(JSSet));
    set->count = 0;
    set->capacity = 8; // Initial capacity (must be power of 2)
    set->elements = (Value*)arena_alloc(OBJ_ARRAY_DATA, 8 * sizeof(Value));
    for (int i = 0; i < 8; i++) {
        set->elements[i] = VAL_UNDEFINED; // VAL_UNDEFINED represents empty slot
    }
    return make_pointer(set);
}

static bool set_add_internal(Value* table, uint32_t capacity, Value val) {
    uint32_t h = set_hash_value(val);
    uint32_t mask = capacity - 1;
    uint32_t idx = h & mask;
    while (table[idx] != VAL_UNDEFINED) {
        if (table[idx] == val) {
            return false; // Already exists
        }
        idx = (idx + 1) & mask;
    }
    table[idx] = val;
    return true;
}

static void set_resize(JSSet* set) {
    uint32_t old_cap = set->capacity;
    Value* old_elements = set->elements;
    
    set->capacity = old_cap * 2;
    set->elements = (Value*)arena_alloc(OBJ_ARRAY_DATA, set->capacity * sizeof(Value));
    for (uint32_t i = 0; i < set->capacity; i++) {
        set->elements[i] = VAL_UNDEFINED;
    }
    
    for (uint32_t i = 0; i < old_cap; i++) {
        Value val = old_elements[i];
        if (val != VAL_UNDEFINED) {
            set_add_internal(set->elements, set->capacity, val);
        }
    }
}

bool set_add(Value set_val, Value val) {
    JSSet* set = (JSSet*)get_pointer(set_val);
    // Keep load factor <= 0.7
    if ((set->count + 1) * 10 > set->capacity * 7) {
        set_resize(set);
    }
    if (set_add_internal(set->elements, set->capacity, val)) {
        set->count++;
        return true;
    }
    return false;
}

bool set_has(Value set_val, Value val) {
    JSSet* set = (JSSet*)get_pointer(set_val);
    uint32_t h = set_hash_value(val);
    uint32_t mask = set->capacity - 1;
    uint32_t idx = h & mask;
    while (set->elements[idx] != VAL_UNDEFINED) {
        if (set->elements[idx] == val) {
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

bool set_delete(Value set_val, Value val) {
    JSSet* set = (JSSet*)get_pointer(set_val);
    uint32_t h = set_hash_value(val);
    uint32_t mask = set->capacity - 1;
    uint32_t idx = h & mask;
    while (set->elements[idx] != VAL_UNDEFINED) {
        if (set->elements[idx] == val) {
            // Found! Delete it. To avoid breaking collision chains, we re-hash
            // any elements following this one in the same run.
            set->elements[idx] = VAL_UNDEFINED;
            set->count--;
            
            // Re-hash elements that follow in the cluster
            uint32_t rehash_idx = (idx + 1) & mask;
            while (set->elements[rehash_idx] != VAL_UNDEFINED) {
                Value v = set->elements[rehash_idx];
                set->elements[rehash_idx] = VAL_UNDEFINED;
                set_add_internal(set->elements, set->capacity, v);
                rehash_idx = (rehash_idx + 1) & mask;
            }
            return true;
        }
        idx = (idx + 1) & mask;
    }
    return false;
}

Value create_float16_array(uint32_t length) {
    JSFloat16Array* arr = (JSFloat16Array*)arena_alloc(OBJ_FLOAT16_ARRAY, sizeof(JSFloat16Array));
    arr->length = length;
    size_t data_size = length * sizeof(uint16_t);
    if (data_size == 0) data_size = 8; // Avoid zero allocation size
    arr->data = (uint16_t*)arena_alloc(OBJ_ARRAY_DATA, data_size);
    memset(arr->data, 0, data_size);
    return make_pointer(arr);
}


#define BUFFER_SLAB_SIZE 8192

static _Thread_local uint8_t* current_slab = NULL;
static _Thread_local uint32_t slab_offset = 0;
static _Thread_local Value current_slab_ref = VAL_UNDEFINED;

Value create_buffer(uint32_t size, bool zero_fill) {
    JSBuffer* buf = (JSBuffer*)arena_alloc(OBJ_BUFFER, sizeof(JSBuffer));
    buf->length = size;
    buf->is_external = false;
    buf->slab_ref = VAL_UNDEFINED;
    
    if (size == 0) {
        buf->data = NULL;
        return make_pointer(buf);
    }
    
    // For large buffers or zero-filled buffers, don't use slab
    if (size > (BUFFER_SLAB_SIZE / 2) || zero_fill) {
        buf->data = zero_fill ? (uint8_t*)calloc(1, size) : (uint8_t*)malloc(size);
        return make_pointer(buf);
    }
    
    // Align offset to 8 bytes
    slab_offset = (slab_offset + 7) & ~7;
    
    if (!current_slab || slab_offset + size > BUFFER_SLAB_SIZE) {
        // Allocate a new slab from the arena!
        // We do this by creating a fake object OBJ_BUFFER_DATA to hold the slab.
        void* slab_data = arena_alloc(OBJ_BUFFER_DATA, BUFFER_SLAB_SIZE);
        current_slab = (uint8_t*)slab_data;
        slab_offset = 0;
        current_slab_ref = make_pointer(slab_data);
    }
    
    buf->data = current_slab + slab_offset;
    buf->slab_ref = current_slab_ref;
    slab_offset += size;
    
    return make_pointer(buf);
}

// Create a Buffer from a string with the given encoding.
// Supported encodings: "utf8" (default), "ascii", "hex", "base64".
Value create_buffer_from_string(const char* str, size_t len, const char* encoding) {
    if (!encoding || strcmp(encoding, "utf8") == 0 || strcmp(encoding, "ascii") == 0) {
        Value buf_val = create_buffer((uint32_t)len, false);
        JSBuffer* buf = (JSBuffer*)get_pointer(buf_val);
        memcpy(buf->data, str, len);
        return buf_val;
    }
    if (strcmp(encoding, "hex") == 0) {
        // Each two hex chars decode to one byte
        size_t out_len = len / 2;
        Value buf_val = create_buffer((uint32_t)out_len, false);
        JSBuffer* buf = (JSBuffer*)get_pointer(buf_val);
        for (size_t i = 0; i < out_len; i++) {
            unsigned int byte;
            sscanf(str + i * 2, "%02x", &byte);
            buf->data[i] = (uint8_t)byte;
        }
        return buf_val;
    }
    if (strcmp(encoding, "base64") == 0) {
        // Simple base64 decoder
        static const char b64_table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t out_len = (len * 3) / 4 + 4;
        Value buf_val = create_buffer((uint32_t)out_len, true);
        JSBuffer* buf = (JSBuffer*)get_pointer(buf_val);
        uint32_t w = 0, bits = 0;
        size_t out = 0;
        for (size_t i = 0; i < len; i++) {
            char c = str[i];
            if (c == '=') break;
            const char* p = strchr(b64_table, c);
            if (!p) continue;
            w = (w << 6) | (uint32_t)(p - b64_table);
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                buf->data[out++] = (uint8_t)(w >> bits);
            }
        }
        buf->length = (uint32_t)out;
        return buf_val;
    }
    // Fallback: treat as raw bytes
    return create_buffer_from_string(str, len, "utf8");
}

// Wrap an existing void* pointer as a Buffer without copying.
// The caller is responsible for ensuring the pointer remains valid.
Value create_buffer_external(void* data, size_t len) {
    JSBuffer* buf = (JSBuffer*)arena_alloc(OBJ_BUFFER, sizeof(JSBuffer));
    buf->length = (uint32_t)len;
    buf->data = (uint8_t*)data;
    buf->is_external = true;
    return make_pointer(buf);
}

Value create_promise(uint32_t state, Value result) {
    if (g_current_vm) vm_push_root(g_current_vm, result);
    Value prom_val = make_pointer(arena_alloc(OBJ_PROMISE, sizeof(JSPromise)));
    if (g_current_vm) {
        result = g_current_vm->gc_roots[g_current_vm->gc_root_count - 1];
        vm_pop_root(g_current_vm);
    }
    JSPromise* prom = (JSPromise*)get_pointer(prom_val);
    prom->state = state;
    prom->result = result;
    prom->then_callbacks = VAL_NULL;
    return prom_val;
}


#include <errno.h>

const char* get_system_error_name(int err_code) {
    switch (err_code) {
        case EACCES: return "EACCES";
        case EADDRINUSE: return "EADDRINUSE";
        case EADDRNOTAVAIL: return "EADDRNOTAVAIL";
        case EAGAIN: return "EAGAIN";
        case EBADF: return "EBADF";
        case ECONNREFUSED: return "ECONNREFUSED";
        case ECONNRESET: return "ECONNRESET";
        case EEXIST: return "EEXIST";
        case EISDIR: return "EISDIR";
        case EMFILE: return "EMFILE";
        case ENOENT: return "ENOENT";
        case ENOTDIR: return "ENOTDIR";
        case ENOTEMPTY: return "ENOTEMPTY";
        case EPERM: return "EPERM";
        case EPIPE: return "EPIPE";
        case ETIMEDOUT: return "ETIMEDOUT";
        default: return "UNKNOWN";
    }
}

Value create_system_error(struct VM* vm, int err_code, const char* syscall_name, const char* custom_message) {
    (void)vm;
    JSError* err = arena_alloc(OBJ_ERROR, sizeof(JSError));
    err->name = create_string("SystemError", 11);
    
    char msg_buf[1024];
    const char* sys_err_str = strerror(err_code);
    const char* err_name = get_system_error_name(err_code);
    
    if (custom_message && syscall_name) {
        snprintf(msg_buf, sizeof(msg_buf), "%s: %s, %s '%s'", err_name, sys_err_str, syscall_name, custom_message);
    } else if (syscall_name) {
        snprintf(msg_buf, sizeof(msg_buf), "%s: %s, %s", err_name, sys_err_str, syscall_name);
    } else {
        snprintf(msg_buf, sizeof(msg_buf), "%s: %s", err_name, sys_err_str);
    }
    
    err->message = create_string(msg_buf, strlen(msg_buf));
    err->stack = create_string("", 0); // Simplified for now
    err->properties = NULL;
    err->count = 0;
    err->capacity = 0;
    
    Value err_val = make_pointer(err);
    
    // Set code and syscall properties
    Value code_val = create_string(err_name, strlen(err_name));
    object_set(err_val, create_string("code", 4), code_val);
    
    if (syscall_name) {
        Value syscall_val = create_string(syscall_name, strlen(syscall_name));
        object_set(err_val, create_string("syscall", 7), syscall_val);
    }
    
    if (custom_message) {
        Value path_val = create_string(custom_message, strlen(custom_message));
        object_set(err_val, create_string("path", 4), path_val);
    }
    
    return err_val;
}

