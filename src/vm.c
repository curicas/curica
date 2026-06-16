/**
 * @file vm.c
 * @brief Core Virtual Machine implementation for Curica.
 * 
 * Handles the bytecode dispatch loop, CallFrame stack management, context switching,
 * NaN-boxed value operations, and async coroutine (ucontext_t) execution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "vm.h"
#include "worker_module.h"
#include "sqlite_module.h"
#include "wasm_module.h"
#include "alloc.h"
#include "compiler.h"
#include "builtins.h"

// Global Prototypes for delegation
_Thread_local Value g_buffer_prototype = 0;

// Define JS console.log native binding
static Value js_console_log(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    (void)this_val;
    for (int i = 0; i < arg_count; i++) {
        Value val = args[i];
        if (i > 0) printf(" ");
        
        if (IS_INTEGER(val)) {
            printf("%d", get_integer(val));
        } else if (IS_DOUBLE(val)) {
            printf("%g", get_double(val));
        } else if (IS_BOOLEAN(val)) {
            printf("%s", get_boolean(val) ? "true" : "false");
        } else if (IS_NULL(val)) {
            printf("null");
        } else if (IS_UNDEFINED(val)) {
            printf("undefined");
        } else if (IS_POINTER(val)) {
            BlockHeader* header = (BlockHeader*)((char*)get_pointer(val) - sizeof(BlockHeader));
            if (header->obj_type == OBJ_STRING) {
                JSString* s = (JSString*)get_pointer(val);
                printf("%s", s->data);
            } else if (header->obj_type == OBJ_ARRAY) {
                JSArray* arr = (JSArray*)get_pointer(val);
                printf("[Array: length=%d]", arr->length);
            } else if (header->obj_type == OBJ_OBJECT) {
                printf("[object Object]");
            } else if (header->obj_type == OBJ_FUNCTION) {
                JSFunction* f = (JSFunction*)get_pointer(val);
                if (f->name != VAL_NULL) {
                    printf("[Function: %s]", ((JSString*)get_pointer(f->name))->data);
                } else {
                    printf("[Function]");
                }
            } else {
                printf("[object]");
            }
        } else {
            printf("[unknown]");
        }
    }
    printf("\n");
    return VAL_UNDEFINED;
}

// Max stack size for recursion protection
#define MAX_CALL_STACK 1000

JSFunction* g_current_native_function = NULL;

void vm_throw_error(VM* vm, Value error) {
    if (vm->error_handler && vm->error_handler->active) {
        vm->error_handler->error = error;
        longjmp(vm->error_handler->jmp, 1);
    } else {
        fprintf(stderr, "Uncaught Error: ");
        if (IS_POINTER(error)) {
            void* p = get_pointer(error);
            BlockHeader* h = (BlockHeader*)((char*)p - sizeof(BlockHeader));
            if (h->obj_type == OBJ_STRING) {
                fprintf(stderr, "%s\n", ((JSString*)p)->data);
            } else if (h->obj_type == OBJ_ERROR) {
                JSError* err = (JSError*)p;
                JSString* name = IS_POINTER(err->name) ? (JSString*)get_pointer(err->name) : NULL;
                JSString* msg  = IS_POINTER(err->message) ? (JSString*)get_pointer(err->message) : NULL;
                fprintf(stderr, "%s: %s\n",
                    name ? name->data : "Error",
                    msg  ? msg->data  : "");
            } else {
                fprintf(stderr, "[object Object]\n");
            }
        } else if (IS_INTEGER(error)) {
            fprintf(stderr, "%d\n", get_integer(error));
        } else if (IS_DOUBLE(error)) {
            fprintf(stderr, "%g\n", get_double(error));
        } else if (IS_BOOLEAN(error)) {
            fprintf(stderr, "%s\n", get_boolean(error) ? "true" : "false");
        } else if (IS_NULL(error)) {
            fprintf(stderr, "null\n");
        } else {
            fprintf(stderr, "undefined\n");
        }
        exit(1);
    }
}

/*
 * ===========================================================================
 * FUNCTION DISPATCH & STACK FRAME ALLOCATION
 * ===========================================================================
 * Handles the invocation of both Native C closures and bytecode functions.
 * 
 * For bytecode functions:
 * 1. Resolves the requested register window size.
 * 2. Dynamically resizes the VM's contiguous register array if out of bounds.
 * 3. Copies arguments into the new register window.
 * 4. Pushes a new CallFrame onto the VM stack and jumps to execution.
 */
static void vm_switch_program(VM* vm, CompiledProgram* prog) {  
    if (prog && vm->current_prog != prog) {
        vm->current_prog = prog;
        vm->const_pool = prog->const_pool;
        vm->const_pool_size = prog->const_pool_size;
        vm->bytecode = prog->bytecode;
        vm->bytecode_size = prog->bytecode_size;
        vm->functions = prog->functions;
        vm->function_count = prog->function_count;
    }
}

/**
 * @brief Universal Function Invocation Interface
 *
 * `vm_call_function` negotiates the dispatching of all JavaScript function types:
 * 1. Native C Callbacks (`native_ptr`).
 * 2. Synchronous JavaScript (Push CallFrame -> `vm_run` -> Pop CallFrame).
 * 3. Asynchronous JavaScript (`is_async`):
 *    - Dynamically provisions a stackful `ucontext_t` Coroutine (`VMCoroutine`).
 *    - Marshals arguments into the Coroutine's isolated register stack.
 *    - Spawns the coroutine via `vm_coro_resume` and synchronously returns an unfulfilled `JSPromise`.
 */
Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args) {
    if (!IS_POINTER(func_val)) {
        vm_throw_error(vm, create_error("TypeError", create_string("caller is not a function", 23)));
        return VAL_UNDEFINED;
    }
    JSFunction* f = (JSFunction*)get_pointer(func_val);  
    BlockHeader* h = (BlockHeader*)((char*)f - sizeof(BlockHeader));
    if (h->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_error("TypeError", create_string("caller is not a function", 23)));
        return VAL_UNDEFINED;
    }
    
    JSFunction* actual_f = f;
    Value bound_this = VAL_UNDEFINED;
    if (f->bytecode_offset != 0xffffffff && f->native_ptr != NULL) {
        actual_f = (JSFunction*)f->native_ptr;
        bound_this = f->env;
    }

    if (actual_f->bytecode_offset == 0xffffffff) {
        Value (*native)(VM*, Value, int, Value*) = (Value (*)(VM*, Value, int, Value*))actual_f->native_ptr;
        return native(vm, bound_this != VAL_UNDEFINED ? bound_this : actual_f->env, arg_count, args);
    }
    
    if (actual_f->is_async) {
        Value promise = create_promise(0, VAL_UNDEFINED);
        VMCoroutine* coro = vm_coro_create(vm);
        coro->promise = promise;
        
        coro->frame_count = 1;
        CallFrame* new_frame = &coro->frames[0];
        new_frame->prog = actual_f->prog;
        new_frame->func = func_val;
        new_frame->ip = actual_f->bytecode_offset;
        new_frame->reg_base = 0;
        new_frame->reg_count = actual_f->register_count;
        new_frame->env = actual_f->env;
        new_frame->coro = coro;
        new_frame->new_target = VAL_UNDEFINED;
        
        Value this_key = create_string("this", 4);
        new_frame->old_this = object_get(vm->global_obj, this_key);
        if (bound_this != VAL_UNDEFINED) {
            object_set(vm->global_obj, this_key, bound_this);
        }
        
        if (actual_f->register_count > coro->register_capacity) {
            coro->register_capacity = actual_f->register_count * 2;
            coro->registers = realloc(coro->registers, coro->register_capacity * sizeof(Value));
        }
        
        for (int i = 0; i < arg_count; i++) {
            if (i < (int)actual_f->param_count) {
                coro->registers[i] = args[i];
            }
        }
        for (uint32_t i = arg_count; i < actual_f->register_count; i++) {
            coro->registers[i] = VAL_UNDEFINED;
        }
        
        vm_coro_resume(vm, coro, VAL_UNDEFINED);
        
        if (bound_this != VAL_UNDEFINED) {
            object_set(vm->global_obj, this_key, new_frame->old_this);
        }
        
        return promise;
    }
    
    uint32_t reg_base = 0;
    
    // Determine a safe register base to avoid overwriting the active caller's registers.
    // If the VM is currently executing (frame_count > 0), allocate the new registers 
    // directly above the top-most active frame's allocated block.
    if (vm->frame_count > 0) {
        CallFrame* active_frame = &vm->frames[vm->frame_count - 1];
        reg_base = active_frame->reg_base + active_frame->reg_count;
    }
    
    uint32_t needed_regs = 1 + actual_f->register_count;
    if (reg_base + needed_regs >= vm->reg_capacity) {
        vm->reg_capacity = reg_base + needed_regs + 16;
        vm->registers = realloc(vm->registers, vm->reg_capacity * sizeof(Value));
    }
    vm->reg_count += needed_regs;
    
    vm->registers[reg_base] = func_val;
    for (int i = 0; i < arg_count; i++) {
        if (i < (int)actual_f->param_count) {
            vm->registers[reg_base + 1 + i] = args[i];
        }
    }
    for (uint32_t i = 1 + arg_count; i < actual_f->register_count; i++) {
        vm->registers[reg_base + i] = VAL_UNDEFINED;
    }
    
    if (vm->frame_count >= vm->frame_capacity) {
        vm->frame_capacity = vm->frame_capacity == 0 ? 8 : vm->frame_capacity * 2;
        vm->frames = realloc(vm->frames, vm->frame_capacity * sizeof(CallFrame));
    }
    CallFrame* new_frame = &vm->frames[vm->frame_count++];
    new_frame->prog = actual_f->prog;
    vm_switch_program(vm, actual_f->prog);
    new_frame->func = func_val;
    new_frame->ip = actual_f->bytecode_offset;
    new_frame->reg_base = reg_base + 1;
    new_frame->reg_count = actual_f->register_count;
    new_frame->env = actual_f->env;
    new_frame->coro = NULL;
    new_frame->new_target = VAL_UNDEFINED;
    
    Value this_key = create_string("this", 4);
    new_frame->old_this = object_get(vm->global_obj, this_key);
    if (bound_this != VAL_UNDEFINED) {
        object_set(vm->global_obj, this_key, bound_this);
    }
    
    fflush(stdout);
    Value res = vm_run(vm);

    if (bound_this != VAL_UNDEFINED) {
        object_set(vm->global_obj, this_key, new_frame->old_this);
    }
    vm->reg_count = reg_base;
    if (vm->frame_count > 0) {
        CallFrame* current_frame = &vm->frames[vm->frame_count - 1];
        vm_switch_program(vm, current_frame->prog);
    } else {
        // Top-level C-to-JS call returned. Drain nextTick and Promises
        while (vm->next_tick_head || vm->microtask_head) {
            if (vm->next_tick_head) {
                vm_drain_next_tick(vm);
            }
            if (vm->microtask_head) {
                vm_drain_microtasks(vm);
            }
        }
    }
    return res;
}

float half_to_float(uint16_t h) {
    uint32_t sign = (h & 0x8000) << 16;
    uint32_t exp = (h & 0x7C00) >> 10;
    uint32_t mant = h & 0x03FF;
    uint32_t f_val;
    if (exp == 0x1F) {
        f_val = sign | 0x7F800000 | (mant << 13);
    } else if (exp == 0) {
        if (mant == 0) {
            f_val = sign;
        } else {
            while ((mant & 0x0400) == 0) {
                mant <<= 1;
                exp--;
            }
            exp++;
            mant &= ~0x0400;
            f_val = sign | (((exp - 15 + 127) & 0xFF) << 23) | (mant << 13);
        }
    } else {
        f_val = sign | (((exp - 15 + 127) & 0xFF) << 23) | (mant << 13);
    }
    float res;
    memcpy(&res, &f_val, sizeof(float));
    return res;
}

uint16_t float_to_half(float f) {
    uint32_t f_val;
    memcpy(&f_val, &f, sizeof(float));
    uint32_t sign = (f_val >> 16) & 0x8000;
    int32_t exp = ((f_val >> 23) & 0xFF) - 127;
    uint32_t mant = f_val & 0x007FFFFF;
    if (exp == 128) {
        return sign | 0x7C00 | (mant ? (0x0200 | (mant >> 13)) : 0);
    }
    exp += 15;
    if (exp >= 31) {
        return sign | 0x7C00;
    } else if (exp <= 0) {
        if (exp < -10) {
            return sign;
        }
        mant |= 0x00800000;
        uint32_t shift = 14 - exp;
        uint32_t round = (mant >> (shift - 1)) & 1;
        uint32_t rem = mant & ((1 << (shift - 1)) - 1);
        uint32_t h_mant = mant >> shift;
        if (round && (rem || (h_mant & 1))) {
            h_mant++;
        }
        return sign | h_mant;
    } else {
        uint32_t h_mant = mant >> 13;
        uint32_t round = (mant >> 12) & 1;
        uint32_t rem = mant & 0x0FFF;
        if (round && (rem || (h_mant & 1))) {
            h_mant++;
            if (h_mant >= 0x0400) {
                h_mant = 0;
                exp++;
            }
        }
        if (exp >= 31) {
            return sign | 0x7C00;
        }
        return sign | (exp << 10) | h_mant;
    }
}

static Value js_set_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    Value set = create_set();
    if (arg_count > 0 && IS_POINTER(args[0])) {
        void* ptr = get_pointer(args[0]);
        BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
        if (header->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)ptr;
            for (uint32_t i = 0; i < arr->length; i++) {
                set_add(set, arr->elements[i]);
            }
        }
    }
    return set;
}

static Value js_set_add(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_SET) return VAL_UNDEFINED;
    Value val = (arg_count > 0) ? args[0] : VAL_UNDEFINED;
    set_add(this_val, val);
    return this_val;
}

static Value js_set_has(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return make_boolean(false);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_SET) return make_boolean(false);
    Value val = (arg_count > 0) ? args[0] : VAL_UNDEFINED;
    return make_boolean(set_has(this_val, val));
}

static Value js_set_delete(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return make_boolean(false);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_SET) return make_boolean(false);
    Value val = (arg_count > 0) ? args[0] : VAL_UNDEFINED;
    return make_boolean(set_delete(this_val, val));
}

static Value js_set_intersection(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_UNDEFINED;
    
    Value res = create_set();
    JSSet* s1 = (JSSet*)get_pointer(this_val);
    for (uint32_t i = 0; i < s1->capacity; i++) {
        Value val = s1->elements[i];
        if (val != VAL_UNDEFINED) {
            if (set_has(args[0], val)) {
                set_add(res, val);
            }
        }
    }
    return res;
}

static Value js_set_union(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_UNDEFINED;
    
    Value res = create_set();
    JSSet* s1 = (JSSet*)get_pointer(this_val);
    for (uint32_t i = 0; i < s1->capacity; i++) {
        Value val = s1->elements[i];
        if (val != VAL_UNDEFINED) {
            set_add(res, val);
        }
    }
    JSSet* s2 = (JSSet*)get_pointer(args[0]);
    for (uint32_t i = 0; i < s2->capacity; i++) {
        Value val = s2->elements[i];
        if (val != VAL_UNDEFINED) {
            set_add(res, val);
        }
    }
    return res;
}

static Value js_set_difference(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_UNDEFINED;
    
    Value res = create_set();
    JSSet* s1 = (JSSet*)get_pointer(this_val);
    for (uint32_t i = 0; i < s1->capacity; i++) {
        Value val = s1->elements[i];
        if (val != VAL_UNDEFINED) {
            if (!set_has(args[0], val)) {
                set_add(res, val);
            }
        }
    }
    return res;
}

static Value js_float16_array_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    uint32_t len = 0;
    if (arg_count > 0) {
        if (IS_INTEGER(args[0])) {
            len = get_integer(args[0]);
        } else if (IS_DOUBLE(args[0])) {
            len = (uint32_t)get_double(args[0]);
        } else if (IS_POINTER(args[0])) {
            void* ptr = get_pointer(args[0]);
            BlockHeader* header = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));
            if (header->obj_type == OBJ_ARRAY) {
                JSArray* arr = (JSArray*)ptr;
                len = arr->length;
                Value res = create_float16_array(len);
                JSFloat16Array* f16 = (JSFloat16Array*)get_pointer(res);
                for (uint32_t i = 0; i < len; i++) {
                    double d_val = 0.0;
                    if (IS_INTEGER(arr->elements[i])) d_val = get_integer(arr->elements[i]);
                    else if (IS_DOUBLE(arr->elements[i])) d_val = get_double(arr->elements[i]);
                    f16->data[i] = float_to_half((float)d_val);
                }
                return res;
            }
        }
    }
    return create_float16_array(len);
}

typedef struct {
    const char* json;
    int cursor;
    VM* vm;
} JSONParser;

static void json_skip_whitespace(JSONParser* parser) {
    while (parser->json[parser->cursor] == ' ' ||
           parser->json[parser->cursor] == '\t' ||
           parser->json[parser->cursor] == '\r' ||
           parser->json[parser->cursor] == '\n') {
        parser->cursor++;
    }
}

static Value json_parse_value(JSONParser* parser);

static Value json_parse_object(JSONParser* parser) {
    parser->cursor++; // Skip '{'
    Value obj = create_object();
    
    while (1) {
        json_skip_whitespace(parser);
        if (parser->json[parser->cursor] == '}') {
            parser->cursor++;
            break;
        }
        
        if (parser->json[parser->cursor] != '"') {
            return VAL_NULL;
        }
        
        parser->cursor++; // Skip '"'
        int start = parser->cursor;
        while (parser->json[parser->cursor] != '"' && parser->json[parser->cursor] != '\0') {
            if (parser->json[parser->cursor] == '\\') {
                parser->cursor++;
            }
            parser->cursor++;
        }
        int len = parser->cursor - start;
        char* buf = malloc(len + 1);
        int dst = 0;
        for (int i = 0; i < len; i++) {
            if (parser->json[start + i] == '\\' && i + 1 < len) {
                i++;
                char escaped = parser->json[start + i];
                if (escaped == 'n') buf[dst++] = '\n';
                else if (escaped == 'r') buf[dst++] = '\r';
                else if (escaped == 't') buf[dst++] = '\t';
                else buf[dst++] = escaped;
            } else {
                buf[dst++] = parser->json[start + i];
            }
        }
        Value key = create_string(buf, dst);
        free(buf);
        parser->cursor++; // Skip '"'
        
        json_skip_whitespace(parser);
        if (parser->json[parser->cursor] != ':') {
            return VAL_NULL;
        }
        parser->cursor++; // Skip ':'
        
        Value val = json_parse_value(parser);
        object_set(obj, key, val);
        
        json_skip_whitespace(parser);
        if (parser->json[parser->cursor] == ',') {
            parser->cursor++;
        } else if (parser->json[parser->cursor] == '}') {
            parser->cursor++;
            break;
        } else {
            return VAL_NULL;
        }
    }
    return obj;
}

static Value json_parse_array(JSONParser* parser) {
    parser->cursor++; // Skip '['
    Value arr = create_array(0);
    
    while (1) {
        json_skip_whitespace(parser);
        if (parser->json[parser->cursor] == ']') {
            parser->cursor++;
            break;
        }
        
        Value val = json_parse_value(parser);
        array_push(arr, val);
        
        json_skip_whitespace(parser);
        if (parser->json[parser->cursor] == ',') {
            parser->cursor++;
        } else if (parser->json[parser->cursor] == ']') {
            parser->cursor++;
            break;
        } else {
            return VAL_NULL;
        }
    }
    return arr;
}

static Value json_parse_string(JSONParser* parser) {
    parser->cursor++; // Skip '"'
    int start = parser->cursor;
    while (parser->json[parser->cursor] != '"' && parser->json[parser->cursor] != '\0') {
        if (parser->json[parser->cursor] == '\\') {
            parser->cursor++;
        }
        parser->cursor++;
    }
    int len = parser->cursor - start;
    char* buf = malloc(len + 1);
    int dst = 0;
    for (int i = 0; i < len; i++) {
        if (parser->json[start + i] == '\\' && i + 1 < len) {
            i++;
            char escaped = parser->json[start + i];
            if (escaped == 'n') buf[dst++] = '\n';
            else if (escaped == 'r') buf[dst++] = '\r';
            else if (escaped == 't') buf[dst++] = '\t';
            else buf[dst++] = escaped;
        } else {
            buf[dst++] = parser->json[start + i];
        }
    }
    Value str = create_string(buf, dst);
    free(buf);
    parser->cursor++; // Skip '"'
    return str;
}

static Value json_parse_number(JSONParser* parser) {
    int start = parser->cursor;
    bool is_double = false;
    if (parser->json[parser->cursor] == '-') {
        parser->cursor++;
    }
    while ((parser->json[parser->cursor] >= '0' && parser->json[parser->cursor] <= '9') ||
           parser->json[parser->cursor] == '.' ||
           parser->json[parser->cursor] == 'e' ||
           parser->json[parser->cursor] == 'E' ||
           parser->json[parser->cursor] == '+' ||
           parser->json[parser->cursor] == '-') {
        if (parser->json[parser->cursor] == '.' ||
            parser->json[parser->cursor] == 'e' ||
            parser->json[parser->cursor] == 'E') {
            is_double = true;
        }
        parser->cursor++;
    }
    int len = parser->cursor - start;
    char* temp = strndup(&parser->json[start], len);
    double val = atof(temp);
    free(temp);
    if (is_double) {
        return make_double(val);
    } else {
        return make_integer((int32_t)val);
    }
}

static Value json_parse_value(JSONParser* parser) {
    json_skip_whitespace(parser);
    char c = parser->json[parser->cursor];
    if (c == '{') {
        return json_parse_object(parser);
    } else if (c == '[') {
        return json_parse_array(parser);
    } else if (c == '"') {
        return json_parse_string(parser);
    } else if ((c >= '0' && c <= '9') || c == '-') {
        return json_parse_number(parser);
    } else if (c == 't') {
        if (strncmp(&parser->json[parser->cursor], "true", 4) == 0) {
            parser->cursor += 4;
            return VAL_TRUE;
        }
    } else if (c == 'f') {
        if (strncmp(&parser->json[parser->cursor], "false", 5) == 0) {
            parser->cursor += 5;
            return VAL_FALSE;
        }
    } else if (c == 'n') {
        if (strncmp(&parser->json[parser->cursor], "null", 4) == 0) {
            parser->cursor += 4;
            return VAL_NULL;
        }
    }
    return VAL_UNDEFINED;
}

static Value js_import_module(VM* vm, Value env, int arg_count, Value* args) {
    (void)env;
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) {
        vm_throw_error(vm, create_string("TypeError: import_module expects path and attributes", 54));
        return VAL_UNDEFINED;
    }
    
    JSString* path_str = (JSString*)get_pointer(args[0]);
    JSString* type_str = (JSString*)get_pointer(args[1]);
    
    if (strcmp(type_str->data, "json") != 0) {
        vm_throw_error(vm, create_string("TypeError: import attribute type must be json", 45));
        return VAL_UNDEFINED;
    }
    
    FILE* f = fopen(path_str->data, "r");
    if (!f) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "Error: Failed to open JSON module file '%s'", path_str->data);
        vm_throw_error(vm, create_string(err_msg, strlen(err_msg)));
        return VAL_UNDEFINED;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(size + 1);
    size_t read_bytes = fread(content, 1, size, f);
    content[read_bytes] = '\0';
    fclose(f);
    
    JSONParser parser;
    parser.json = content;
    parser.cursor = 0;
    parser.vm = vm;
    
    Value res = json_parse_value(&parser);
    free(content);
    
    return res;
}

Value js_array_map(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    
    JSArray* arr = (JSArray*)get_pointer(this_val);
    Value callback = args[0];
    
    Value res_val = create_array(arr->length);
    JSArray* res_arr = (JSArray*)get_pointer(res_val);
    res_arr->length = arr->length;
    
    for (uint32_t i = 0; i < arr->length; i++) {
        Value cb_args[3];
        cb_args[0] = arr->elements[i];
        cb_args[1] = make_integer(i);
        cb_args[2] = this_val;
        res_arr->elements[i] = vm_call_function(vm, callback, 3, cb_args);
    }
    return res_val;
}

Value js_array_filter(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    
    JSArray* arr = (JSArray*)get_pointer(this_val);
    Value callback = args[0];
    
    Value res_val = create_array(0);
    
    for (uint32_t i = 0; i < arr->length; i++) {
        Value cb_args[3];
        cb_args[0] = arr->elements[i];
        cb_args[1] = make_integer(i);
        cb_args[2] = this_val;
        Value match = vm_call_function(vm, callback, 3, cb_args);
        if (is_truthy(match)) {
            array_push(res_val, arr->elements[i]);
        }
    }
    return res_val;
}

static Value js_throw_error(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    Value err = (arg_count > 0) ? args[0] : create_string("Error", 5);
    vm_throw_error(vm, err);
    return VAL_UNDEFINED;
}

// ── Microtask Queue ────────────────────────────────────────────────────────
void vm_enqueue_microtask(VM* vm, Value callback, Value arg) {
    MicrotaskEntry* entry = malloc(sizeof(MicrotaskEntry));
    entry->callback = callback;
    entry->arg = arg;
    entry->next = NULL;
    if (vm->microtask_tail) {
        vm->microtask_tail->next = entry;
        vm->microtask_tail = entry;
    } else {
        vm->microtask_head = entry;
        vm->microtask_tail = entry;
    }
}

void vm_drain_microtasks(VM* vm) {
    while (vm->microtask_head) {
        MicrotaskEntry* entry = vm->microtask_head;
        vm->microtask_head = entry->next;
        if (!vm->microtask_head) vm->microtask_tail = NULL;
        
        // Execute callback
        Value args[1] = { entry->arg };
        vm_call_function(vm, entry->callback, 1, args);
        
        free(entry);
    }
}

// ── NextTick Queue ─────────────────────────────────────────────────────────
void vm_enqueue_next_tick(VM* vm, Value callback, Value arg) {
    NextTickEntry* entry = malloc(sizeof(NextTickEntry));
    entry->callback = callback;
    entry->arg = arg;
    entry->next = NULL;
    if (vm->next_tick_tail) {
        vm->next_tick_tail->next = entry;
        vm->next_tick_tail = entry;
    } else {
        vm->next_tick_head = entry;
        vm->next_tick_tail = entry;
    }
}

void vm_drain_next_tick(VM* vm) {
    while (vm->next_tick_head) {
        NextTickEntry* entry = vm->next_tick_head;
        vm->next_tick_head = entry->next;
        if (!vm->next_tick_head) vm->next_tick_tail = NULL;
        
        // Execute callback
        Value args[1] = { entry->arg };
        vm_call_function(vm, entry->callback, 1, args);
        
        free(entry);
    }
}

// ── Coroutines (ucontext) ──────────────────────────────────────────────────
static void coro_trampoline(uint32_t vm_ptr_lo, uint32_t vm_ptr_hi) {
    uint64_t ptr_val = ((uint64_t)vm_ptr_hi << 32) | vm_ptr_lo;
    VM* vm = (VM*)ptr_val;
    VMCoroutine* coro = vm->current_coro;
    
    // Set up local error handler so coroutine exceptions don't crash the VM
    VMErrorHandler handler;
    handler.active = true;
    handler.prev = vm->error_handler;
    vm->error_handler = &handler;
    
    if (setjmp(handler.jmp) == 0) {
        // Start VM run for this coroutine frame
        coro->result = vm_run(vm);
        coro->is_error = false; coro->async_stack_trace = VAL_UNDEFINED;
    } else {
        // Uncaught exception inside the coroutine
        coro->result = vm->error_handler->error;
        coro->is_error = true;
    }
    
    vm->error_handler = handler.prev;
    coro->state = CORO_DONE;
    
    // Resolve or reject the coroutine's promise
    extern Value js_promise_resolve_func(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_promise_reject_func(VM* vm, Value this_val, int arg_count, Value* args);
    if (coro->is_error) {
        js_promise_reject_func(vm, coro->promise, 1, &coro->result);
    } else {
        js_promise_resolve_func(vm, coro->promise, 1, &coro->result);
    }
    
    // Yield back to the caller
    swapcontext(&coro->ctx, &coro->caller_ctx);
}

VMCoroutine* vm_coro_create(VM* vm) {
    VMCoroutine* coro = malloc(sizeof(VMCoroutine));
    coro->stack = malloc(CORO_STACK_SIZE);
    coro->state = CORO_RUNNING;
    coro->await_value = VAL_UNDEFINED;
    coro->result = VAL_UNDEFINED;
    coro->promise = VAL_UNDEFINED;
    coro->is_error = false; coro->async_stack_trace = VAL_UNDEFINED;
    coro->next = NULL;
    
    // Allocate isolated VM frame/register stacks for the coroutine
    coro->frame_capacity = 64;
    coro->frames = malloc(sizeof(CallFrame) * coro->frame_capacity);
    coro->frame_count = 0;
    
    coro->register_capacity = 256;
    coro->registers = malloc(sizeof(Value) * coro->register_capacity);
    coro->register_count = 0;
    
    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = CORO_STACK_SIZE;
    coro->ctx.uc_link = &coro->caller_ctx;
    
    uint64_t vm_ptr = (uint64_t)vm;
    uint32_t lo = (uint32_t)(vm_ptr & 0xFFFFFFFF);
    uint32_t hi = (uint32_t)(vm_ptr >> 32);
    
    makecontext(&coro->ctx, (void (*)(void))coro_trampoline, 2, lo, hi);
    return coro;
}

void vm_coro_free(VMCoroutine* coro) {
    free(coro->frames);
    free(coro->registers);
    free(coro->stack);
    free(coro);
}

void vm_coro_suspend(VM* vm, Value await_val) {
    VMCoroutine* coro = vm->current_coro;
    if (!coro) return; // Cannot suspend top-level synchronous code natively
    
    coro->state = CORO_SUSPENDED;
    coro->await_value = await_val;
    
    // Swap back to whoever resumed this coroutine
    swapcontext(&coro->ctx, &coro->caller_ctx);
}

void vm_coro_resume(VM* vm, VMCoroutine* coro, Value resolved_val) {
    VMCoroutine* prev_coro = vm->current_coro;
    
    // 1. Save current active VM execution state
    CallFrame* saved_frames = vm->frames;
    uint32_t saved_fc = vm->frame_count;
    uint32_t saved_fcap = vm->frame_capacity;
    Value* saved_regs = vm->registers;
    uint32_t saved_rc = vm->reg_count;
    uint32_t saved_rcap = vm->reg_capacity;
    
    // 2. Load the coroutine's execution state into the VM
    vm->frames = coro->frames;
    vm->frame_count = coro->frame_count;
    vm->frame_capacity = coro->frame_capacity;
    vm->registers = coro->registers;
    vm->reg_count = coro->register_count;
    vm->reg_capacity = coro->register_capacity;
    vm->current_coro = coro;
    
    coro->state = CORO_RUNNING;
    coro->result = resolved_val; // Passes the resolved promise value back to OP_AWAIT
    
    // 3. Swap context and start/resume execution
    swapcontext(&coro->caller_ctx, &coro->ctx);
    
    // 4. Coroutine suspended or finished. Save its modified state back.
    coro->frames = vm->frames;
    coro->frame_count = vm->frame_count;
    coro->frame_capacity = vm->frame_capacity;
    coro->registers = vm->registers;
    coro->register_count = vm->reg_count;
    coro->register_capacity = vm->reg_capacity;
    
    // 5. Restore the original caller's VM state
    vm->frames = saved_frames;
    vm->frame_count = saved_fc;
    vm->frame_capacity = saved_fcap;
    vm->registers = saved_regs;
    vm->reg_count = saved_rc;
    vm->reg_capacity = saved_rcap;
    vm->current_coro = prev_coro;
    
    if (coro->state == CORO_DONE) {
        vm_coro_free(coro);
    }
}

/*
 * ===========================================================================
 * VM INITIALIZATION & NATIVE BINDINGS
 * ===========================================================================
 * Bootstraps the JavaScript environment. Sets up the Global Object and
 * binds all standard ES2025 library functions (Math, Array, Object, Set,
 * Promises, Iterator Helpers) to their native C implementations.
 */
void vm_init(VM* vm) {
    memset(vm, 0, sizeof(VM));
    
    extern NapiApiTable g_napi_api;
    vm->napi_api = &g_napi_api;
    
    vm->global_obj = create_object();
    vm->module_cache = create_object();
    vm->frames = malloc(1024 * sizeof(CallFrame));
    vm->frame_count = 0;
    vm->frame_capacity = 1024;
    vm->registers = malloc(1024 * sizeof(Value));
    vm->reg_count = 0;
    vm->reg_capacity = 1024;
    
    vm->gc_root_capacity = 1024;
    vm->gc_root_count = 0;
    vm->gc_roots = malloc(vm->gc_root_capacity * sizeof(Value));
    vm->const_pool = NULL;
    vm->const_pool_size = 0;
    vm->bytecode = NULL;
    vm->bytecode_size = 0;
    vm->functions = NULL;
    vm->function_count = 0;
    vm->microtask_head = NULL;
    vm->microtask_tail = NULL;
    vm->next_tick_head = NULL;
    vm->next_tick_tail = NULL;
    
    // Default Security Flags (Secure by default)
    vm->allow_net = false;
    vm->allow_read = false;
    vm->allow_write = false;
    vm->allow_run = false;
    vm->allow_ffi = false;
    
    // Bind global 'console' object with 'log' method
    Value console_name = create_string("console", 7);
    Value log_name = create_string("log", 3);
    
    Value console_obj = create_object();
    Value log_fn = create_native_function((void*)js_console_log, log_name);
    object_set(console_obj, log_name, log_fn);
    
    Value error_name = create_string("error", 5);
    object_set(console_obj, error_name, log_fn); // Map console.error to log_fn for now
    
    object_set(vm->global_obj, console_name, console_obj);
    object_set(vm->global_obj, create_string("print", 5), create_native_function((void*)js_console_log, create_string("print", 5)));
    
    extern Value js_global_set_timeout(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(vm->global_obj, create_string("setTimeout", 10), create_native_function((void*)js_global_set_timeout, create_string("setTimeout", 10)));
    
    extern Value js_global_set_immediate(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(vm->global_obj, create_string("setImmediate", 12), create_native_function((void*)js_global_set_immediate, create_string("setImmediate", 12)));

    extern Value js_global_clear_immediate(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(vm->global_obj, create_string("clearImmediate", 14), create_native_function((void*)js_global_clear_immediate, create_string("clearImmediate", 14)));

    // Bind global 'process' object
    Value process_obj = create_object();
    extern Value js_process_next_tick(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_process_dlopen(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_process_require(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_process_cwd(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(process_obj, create_string("cwd", 3), create_native_function((void*)js_process_cwd, create_string("cwd", 3)));
    object_set(process_obj, create_string("nextTick", 8), create_native_function((void*)js_process_next_tick, create_string("nextTick", 8)));
    object_set(process_obj, create_string("dlopen", 6), create_native_function((void*)js_process_dlopen, create_string("dlopen", 6)));
    object_set(process_obj, create_string("__require", 9), create_native_function((void*)js_process_require, create_string("__require", 9)));
    object_set(vm->global_obj, create_string("process", 7), process_obj);

    extern Value js_queue_microtask(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(vm->global_obj, create_string("queueMicrotask", 14), create_native_function((void*)js_queue_microtask, create_string("queueMicrotask", 14)));

    // Bind global 'Buffer' object
    Value buffer_obj = create_object();
    extern Value js_buffer_alloc_unsafe(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_buffer_alloc(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_buffer_from(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_buffer_is_buffer(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_buffer_concat(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(buffer_obj, create_string("allocUnsafe", 11), create_native_function((void*)js_buffer_alloc_unsafe, create_string("allocUnsafe", 11)));
    object_set(buffer_obj, create_string("alloc", 5), create_native_function((void*)js_buffer_alloc, create_string("alloc", 5)));
    object_set(buffer_obj, create_string("from", 4), create_native_function((void*)js_buffer_from, create_string("from", 4)));
    object_set(buffer_obj, create_string("isBuffer", 8), create_native_function((void*)js_buffer_is_buffer, create_string("isBuffer", 8)));
    object_set(buffer_obj, create_string("concat", 6), create_native_function((void*)js_buffer_concat, create_string("concat", 6)));

    g_buffer_prototype = create_object();
    extern Value js_buffer_prototype_to_string(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(g_buffer_prototype, create_string("toString", 8), create_native_function((void*)js_buffer_prototype_to_string, create_string("toString", 8)));

    object_set(buffer_obj, create_string("prototype", 9), g_buffer_prototype);
    object_set(vm->global_obj, create_string("Buffer", 6), buffer_obj);

    // ── Set prototype ─────────────────────────────────────────────────────────
    vm->set_prototype = create_object();
#define SET_PROTO(name, len, fn) object_set(vm->set_prototype, create_string(name,len), create_native_function((void*)fn, create_string(name,len)))
    SET_PROTO("add", 3, js_set_add);
    SET_PROTO("has", 3, js_set_has);
    SET_PROTO("delete", 6, js_set_delete);
    SET_PROTO("intersection", 12, js_set_intersection);
    SET_PROTO("union", 5, js_set_union);
    SET_PROTO("difference", 10, js_set_difference);
    SET_PROTO("symmetricDifference", 19, js_set_symmetric_difference);
    SET_PROTO("isSubsetOf", 10, js_set_is_subset_of);
    SET_PROTO("isSupersetOf", 12, js_set_is_superset_of);
    SET_PROTO("isDisjointFrom", 14, js_set_is_disjoint_from);
    SET_PROTO("forEach", 7, js_set_for_each);
    SET_PROTO("values", 6, js_set_values);
    SET_PROTO("keys", 4, js_set_values);  // alias
#undef SET_PROTO
    object_set(vm->global_obj, create_string("Set", 3), create_native_function((void*)js_set_constructor, create_string("Set", 3)));

    // ── Array prototype ───────────────────────────────────────────────────────
    vm->array_prototype = create_object();
#define ARR_PROTO(name, len, fn) object_set(vm->array_prototype, create_string(name,len), create_native_function((void*)fn, create_string(name,len)))
    ARR_PROTO("push", 4, js_array_push_method);
    ARR_PROTO("pop", 3, js_array_pop);
    ARR_PROTO("map", 3, js_array_map);
    ARR_PROTO("filter", 6, js_array_filter);
    ARR_PROTO("forEach", 7, js_array_for_each);
    ARR_PROTO("find", 4, js_array_find);
    ARR_PROTO("findIndex", 9, js_array_find_index);
    ARR_PROTO("indexOf", 7, js_array_index_of);
    ARR_PROTO("includes", 8, js_array_includes);
    ARR_PROTO("some", 4, js_array_some);
    ARR_PROTO("every", 5, js_array_every);
    ARR_PROTO("reduce", 6, js_array_reduce);
    ARR_PROTO("flat", 4, js_array_flat);
    ARR_PROTO("flatMap", 7, js_array_flat_map);
    ARR_PROTO("sort", 4, js_array_sort);
    ARR_PROTO("fill", 4, js_array_fill);
    ARR_PROTO("join", 4, js_array_join);
    ARR_PROTO("slice", 5, js_array_slice);
    ARR_PROTO("splice", 6, js_array_splice);
    ARR_PROTO("reverse", 7, js_array_reverse);
    ARR_PROTO("at", 2, js_array_at);
    ARR_PROTO("concat", 6, js_array_concat_method);
    ARR_PROTO("toString", 8, js_array_to_string);
#undef ARR_PROTO

    // ── Function prototype ────────────────────────────────────────────────────
    vm->function_prototype = create_object();
    extern Value js_function_call(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_function_apply(VM* vm, Value this_val, int arg_count, Value* args);
    object_set(vm->function_prototype, create_string("call", 4), create_native_function((void*)js_function_call, create_string("call", 4)));
    object_set(vm->function_prototype, create_string("apply", 5), create_native_function((void*)js_function_apply, create_string("apply", 5)));
    
    Value function_ctor = create_native_function(NULL, create_string("Function", 8));
    object_set(function_ctor, create_string("prototype", 9), vm->function_prototype);
    object_set(vm->global_obj, create_string("Function", 8), function_ctor);

    Value array_ctor = create_native_function((void*)js_array_constructor, create_string("Array", 5));
    object_set(array_ctor, create_string("isArray", 7), create_native_function((void*)js_array_is_array, create_string("isArray", 7)));
    object_set(array_ctor, create_string("from", 4), create_native_function((void*)js_array_from, create_string("from", 4)));
    object_set(array_ctor, create_string("of", 2), create_native_function((void*)js_array_of, create_string("of", 2)));
    object_set(array_ctor, create_string("prototype", 9), vm->array_prototype);
    object_set(vm->global_obj, create_string("Array", 5), array_ctor);

    // ── String prototype ──────────────────────────────────────────────────────
    vm->string_prototype = create_object();
#define STR_PROTO(name, len, fn) object_set(vm->string_prototype, create_string(name,len), create_native_function((void*)fn, create_string(name,len)))
    STR_PROTO("slice", 5, js_string_slice);
    STR_PROTO("substring", 9, js_string_substring);
    STR_PROTO("indexOf", 7, js_string_index_of);
    STR_PROTO("lastIndexOf", 11, js_string_last_index_of);
    STR_PROTO("includes", 8, js_string_includes);
    STR_PROTO("startsWith", 10, js_string_starts_with);
    STR_PROTO("endsWith", 8, js_string_ends_with);
    STR_PROTO("split", 5, js_string_split);
    STR_PROTO("trim", 4, js_string_trim);
    STR_PROTO("trimStart", 9, js_string_trim_start);
    STR_PROTO("trimEnd", 7, js_string_trim_end);
    STR_PROTO("padStart", 8, js_string_pad_start);
    STR_PROTO("padEnd", 6, js_string_pad_end);
    STR_PROTO("repeat", 6, js_string_repeat);
    STR_PROTO("replace", 7, js_string_replace);
    STR_PROTO("replaceAll", 10, js_string_replace_all);
    STR_PROTO("toUpperCase", 11, js_string_to_upper);
    STR_PROTO("toLowerCase", 11, js_string_to_lower);
    STR_PROTO("at", 2, js_string_at);
    STR_PROTO("charCodeAt", 10, js_string_char_code_at);
    STR_PROTO("charAt", 6, js_string_char_at);
    STR_PROTO("concat", 6, js_string_concat_method);
    STR_PROTO("toString", 8, js_string_to_string);
    STR_PROTO("valueOf", 7, js_string_to_string);
#undef STR_PROTO
    Value string_ctor = create_native_function((void*)js_string_constructor, create_string("String", 6));
    object_set(string_ctor, create_string("fromCharCode", 12), create_native_function((void*)js_string_from_char_code, create_string("fromCharCode", 12)));
    object_set(string_ctor, create_string("prototype", 9), vm->string_prototype);
    object_set(vm->global_obj, create_string("String", 6), string_ctor);

    // ── Math object ───────────────────────────────────────────────────────────
    Value math_obj = create_object();
#define MATH_FN(name, len, fn) object_set(math_obj, create_string(name,len), create_native_function((void*)fn, create_string(name,len)))
    MATH_FN("abs", 3, js_math_abs);
    MATH_FN("floor", 5, js_math_floor);
    MATH_FN("ceil", 4, js_math_ceil);
    MATH_FN("round", 5, js_math_round);
    MATH_FN("sqrt", 4, js_math_sqrt);
    MATH_FN("cbrt", 4, js_math_cbrt);
    MATH_FN("pow", 3, js_math_pow);
    MATH_FN("max", 3, js_math_max);
    MATH_FN("min", 3, js_math_min);
    MATH_FN("random", 6, js_math_random);
    MATH_FN("log", 3, js_math_log);
    MATH_FN("log2", 4, js_math_log2);
    MATH_FN("log10", 5, js_math_log10);
    MATH_FN("exp", 3, js_math_exp);
    MATH_FN("trunc", 5, js_math_trunc);
    MATH_FN("sign", 4, js_math_sign);
    MATH_FN("sin", 3, js_math_sin);
    MATH_FN("cos", 3, js_math_cos);
    MATH_FN("tan", 3, js_math_tan);
    MATH_FN("asin", 4, js_math_asin);
    MATH_FN("acos", 4, js_math_acos);
    MATH_FN("atan", 4, js_math_atan);
    MATH_FN("atan2", 5, js_math_atan2);
    MATH_FN("hypot", 5, js_math_hypot);
    MATH_FN("imul", 4, js_math_imul);
    MATH_FN("clz32", 5, js_math_clz32);
    MATH_FN("f16round", 8, js_math_f16round);
#undef MATH_FN
    object_set(math_obj, create_string("PI", 2), make_double(3.14159265358979323846));
    object_set(math_obj, create_string("E", 1), make_double(2.71828182845904523536));
    object_set(math_obj, create_string("LN2", 3), make_double(0.69314718055994530941));
    object_set(math_obj, create_string("LN10", 4), make_double(2.30258509299404568401));
    object_set(math_obj, create_string("SQRT2", 5), make_double(1.41421356237309504880));
    object_set(vm->global_obj, create_string("Math", 4), math_obj);

    // ── Number object ─────────────────────────────────────────────────────────
    Value number_ctor = create_native_function((void*)js_number_constructor, create_string("Number", 6));
    object_set(number_ctor, create_string("isFinite", 8), create_native_function((void*)js_number_is_finite, create_string("isFinite", 8)));
    object_set(number_ctor, create_string("isNaN", 5), create_native_function((void*)js_number_is_nan, create_string("isNaN", 5)));
    object_set(number_ctor, create_string("isInteger", 9), create_native_function((void*)js_number_is_integer, create_string("isInteger", 9)));
    object_set(number_ctor, create_string("isSafeInteger", 13), create_native_function((void*)js_number_is_safe_integer, create_string("isSafeInteger", 13)));
    object_set(number_ctor, create_string("parseInt", 8), create_native_function((void*)js_parse_int, create_string("parseInt", 8)));
    object_set(number_ctor, create_string("parseFloat", 10), create_native_function((void*)js_parse_float, create_string("parseFloat", 10)));
    object_set(number_ctor, create_string("MAX_VALUE", 9), make_double(1.7976931348623157e+308));
    object_set(number_ctor, create_string("MIN_VALUE", 9), make_double(5e-324));
    object_set(number_ctor, create_string("EPSILON", 7), make_double(2.2204460492503131e-16));
    object_set(number_ctor, create_string("MAX_SAFE_INTEGER", 16), make_double(9007199254740991.0));
    object_set(number_ctor, create_string("MIN_SAFE_INTEGER", 16), make_double(-9007199254740991.0));
    object_set(number_ctor, create_string("POSITIVE_INFINITY", 17), make_double(1.0/0.0));
    object_set(number_ctor, create_string("NEGATIVE_INFINITY", 17), make_double(-1.0/0.0));
    object_set(number_ctor, create_string("NaN", 3), make_double(0.0/0.0));
    object_set(vm->global_obj, create_string("Number", 6), number_ctor);

    // ── Object constructor ────────────────────────────────────────────────────
    Value object_ctor = create_native_function((void*)js_object_constructor, create_string("Object", 6));
    object_set(object_ctor, create_string("keys", 4), create_native_function((void*)js_object_keys, create_string("keys", 4)));
    object_set(object_ctor, create_string("values", 6), create_native_function((void*)js_object_values, create_string("values", 6)));
    object_set(object_ctor, create_string("entries", 7), create_native_function((void*)js_object_entries, create_string("entries", 7)));
    object_set(object_ctor, create_string("assign", 6), create_native_function((void*)js_object_assign, create_string("assign", 6)));
    object_set(object_ctor, create_string("freeze", 6), create_native_function((void*)js_object_freeze, create_string("freeze", 6)));
    object_set(object_ctor, create_string("create", 6), create_native_function((void*)js_object_create, create_string("create", 6)));
    object_set(object_ctor, create_string("fromEntries", 11), create_native_function((void*)js_object_from_entries, create_string("fromEntries", 11)));
    object_set(vm->global_obj, create_string("Object", 6), object_ctor);

    // ── JSON object ───────────────────────────────────────────────────────────
    Value json_obj = create_object();
    object_set(json_obj, create_string("parse", 5), create_native_function((void*)js_json_parse, create_string("parse", 5)));
    object_set(json_obj, create_string("stringify", 9), create_native_function((void*)js_json_stringify, create_string("stringify", 9)));
    object_set(vm->global_obj, create_string("JSON", 4), json_obj);

    // ── Error constructors ────────────────────────────────────────────────────
    object_set(vm->global_obj, create_string("Error", 5), create_native_function((void*)js_error_constructor, create_string("Error", 5)));
    object_set(vm->global_obj, create_string("TypeError", 9), create_native_function((void*)js_type_error_constructor, create_string("TypeError", 9)));
    object_set(vm->global_obj, create_string("RangeError", 10), create_native_function((void*)js_range_error_constructor, create_string("RangeError", 10)));
    object_set(vm->global_obj, create_string("ReferenceError", 14), create_native_function((void*)js_reference_error_constructor, create_string("ReferenceError", 14)));
    object_set(vm->global_obj, create_string("SyntaxError", 11), create_native_function((void*)js_syntax_error_constructor, create_string("SyntaxError", 11)));
    object_set(vm->global_obj, create_string("SuppressedError", 15), create_native_function((void*)js_suppressed_error_constructor, create_string("SuppressedError", 15)));

    // ── Float16Array ──────────────────────────────────────────────────────────
    vm->float16_array_prototype = VAL_NULL;
    object_set(vm->global_obj, create_string("Float16Array", 12), create_native_function((void*)js_float16_array_constructor, create_string("Float16Array", 12)));

    // ── Promise ───────────────────────────────────────────────────────────────
    vm->promise_prototype = VAL_NULL;
    extern Value js_promise_constructor(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_promise_try(VM* vm, Value this_val, int arg_count, Value* args);
    Value promise_constructor = create_native_function((void*)js_promise_constructor, create_string("Promise", 7));
    object_set(promise_constructor, create_string("try", 3), create_native_function((void*)js_promise_try, create_string("try", 3)));
    object_set(promise_constructor, create_string("withResolvers", 13), create_native_function((void*)js_promise_with_resolvers, create_string("withResolvers", 13)));
    object_set(vm->global_obj, create_string("Promise", 7), promise_constructor);

    // ── Well-known Symbols ────────────────────────────────────────────────────
    vm->symbol_iterator    = create_symbol(create_string("Symbol.iterator", 15));
    vm->symbol_dispose     = create_symbol(create_string("Symbol.dispose", 14));
    vm->symbol_async_dispose = create_symbol(create_string("Symbol.asyncDispose", 19));

    Value symbol_ctor = create_native_function((void*)js_symbol_constructor, create_string("Symbol", 6));
    object_set(symbol_ctor, create_string("iterator", 8), vm->symbol_iterator);
    object_set(symbol_ctor, create_string("dispose", 7), vm->symbol_dispose);
    object_set(symbol_ctor, create_string("asyncDispose", 12), vm->symbol_async_dispose);
    object_set(vm->global_obj, create_string("Symbol", 6), symbol_ctor);

    // ── Global helpers / constants ────────────────────────────────────────────
    object_set(vm->global_obj, create_string("globalThis", 10), vm->global_obj);
    object_set(vm->global_obj, create_string("NaN", 3), make_double(0.0/0.0));
    object_set(vm->global_obj, create_string("Infinity", 8), make_double(1.0/0.0));
    object_set(vm->global_obj, create_string("undefined", 9), VAL_UNDEFINED);


    // ── SQLite ────────────────────────────────────────────────────────────────
    extern Value build_sqlite_constructor(VM* vm);
    Value sqlite_ctor = build_sqlite_constructor(vm);
    object_set(vm->global_obj, create_string("Database", 8), sqlite_ctor);

    // ── WebAssembly ───────────────────────────────────────────────────────────
    extern Value build_wasm_global(VM* vm);
    Value wasm_obj = build_wasm_global(vm);
    object_set(vm->global_obj, create_string("WebAssembly", 11), wasm_obj);

    // ── WASI ──────────────────────────────────────────────────────────────────
    extern void vm_register_wasi(VM* vm);
    vm_register_wasi(vm);

    extern void vm_register_kv_store(VM* vm);
    vm_register_kv_store(vm);

    extern void vm_register_worker_threads(VM* vm);
    vm_register_worker_threads(vm);
    
    extern void vm_register_webview(VM* vm);
    vm_register_webview(vm);

    object_set(vm->global_obj, create_string("parseInt", 8), create_native_function((void*)js_parse_int, create_string("parseInt", 8)));
    object_set(vm->global_obj, create_string("parseFloat", 10), create_native_function((void*)js_parse_float, create_string("parseFloat", 10)));
    object_set(vm->global_obj, create_string("isNaN", 5), create_native_function((void*)js_global_is_nan, create_string("isNaN", 5)));
    object_set(vm->global_obj, create_string("isFinite", 8), create_native_function((void*)js_global_is_finite, create_string("isFinite", 8)));
    object_set(vm->global_obj, create_string("throwError", 10), create_native_function((void*)js_throw_error, create_string("throwError", 10)));
    object_set(vm->global_obj, create_string("import_module", 13), create_native_function((void*)js_import_module, create_string("import_module", 13)));

    // ── RegExp ────────────────────────────────────────────────────────────────
    Value regexp_ctor = create_native_function((void*)js_regexp_constructor, create_string("RegExp", 6));
    Value regexp_proto = create_object();
    object_set(regexp_proto, create_string("test", 4), create_native_function((void*)js_regexp_test, create_string("test", 4)));
    object_set(regexp_proto, create_string("exec", 4), create_native_function((void*)js_regexp_exec, create_string("exec", 4)));
    object_set(regexp_ctor, create_string("prototype", 9), regexp_proto);
    object_set(vm->global_obj, create_string("RegExp", 6), regexp_ctor);

    // ── Web Worker ────────────────────────────────────────────────────────────
    extern Value build_worker_constructor(VM* vm);
    Value worker_ctor = build_worker_constructor(vm);
    object_set(vm->global_obj, create_string("Worker", 6), worker_ctor);

    // ── Microtask queue ───────────────────────────────────────────────────────
    vm->microtask_head = NULL;
    vm->microtask_tail = NULL;
    vm->current_coro   = NULL;
    vm->error_handler  = NULL;

    // ── URL and URLSearchParams ───────────────────────────────────────────────
    extern Value js_process_require(VM* vm, Value this_val, int arg_count, Value* args);
    Value url_args[2] = { create_string("url", 3), create_string(".", 1) };
    Value url_module = js_process_require(vm, VAL_UNDEFINED, 2, url_args);
    if (url_module != VAL_UNDEFINED) {
        object_set(vm->global_obj, create_string("URL", 3), object_get(url_module, create_string("URL", 3)));
        object_set(vm->global_obj, create_string("URLSearchParams", 15), object_get(url_module, create_string("URLSearchParams", 15)));
    }
    
    // ── TTY / Process Streams ─────────────────────────────────────────────────
    Value tty_args[2] = { create_string("tty", 3), create_string(".", 1) };
    Value tty_module = js_process_require(vm, VAL_UNDEFINED, 2, tty_args);
    if (tty_module != VAL_UNDEFINED) {
        Value process_obj = object_get(vm->global_obj, create_string("process", 7));
        if (process_obj != VAL_UNDEFINED) {
            Value read_stream_fn = object_get(tty_module, create_string("ReadStream", 10));
            Value write_stream_fn = object_get(tty_module, create_string("WriteStream", 11));
            
            Value stdin_fd = make_integer(0);
            extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
            Value process_stdin = vm_call_function(vm, read_stream_fn, 1, &stdin_fd);
            object_set(process_obj, create_string("stdin", 5), process_stdin);
            
            Value stdout_fd = make_integer(1);
            Value process_stdout = vm_call_function(vm, write_stream_fn, 1, &stdout_fd);
            object_set(process_obj, create_string("stdout", 6), process_stdout);
        }
    }
    
    // ── Native Bindings ───────────────────────────────────────────────────────
    extern Value build_child_process_module(VM* vm);
    object_set(vm->global_obj, create_string("__child_process", 15), build_child_process_module(vm));
    
    extern Value build_net_module(VM* vm);
    object_set(vm->global_obj, create_string("__net", 5), build_net_module(vm));
    extern Value build_http_module(VM* vm);
    object_set(vm->global_obj, create_string("__http", 6), build_http_module(vm));

    extern Value build_websocket_module(VM* vm);
    object_set(vm->global_obj, create_string("WebSocket", 9), build_websocket_module(vm));
    // ── Iterator / Error prototypes ───────────────────────────────────────────
    vm->iterator_prototype = VAL_NULL;
    vm->error_prototype    = VAL_NULL;
}

void vm_free(VM* vm) {
    free(vm->frames);
    free(vm->registers);
    free(vm->gc_roots);
    // Note: constant pool and bytecode are owned by the CompiledProgram, which main.c cleans up
}

void vm_push_root(VM* vm, Value val) {
    if (vm->gc_root_count >= vm->gc_root_capacity) {
        vm->gc_root_capacity *= 2;
        vm->gc_roots = realloc(vm->gc_roots, vm->gc_root_capacity * sizeof(Value));
    }
    vm->gc_roots[vm->gc_root_count++] = val;
}

void vm_pop_root(VM* vm) {
    if (vm->gc_root_count > 0) {
        vm->gc_root_count--;
    }
}

void vm_load_program(VM* vm, CompiledProgram* prog) {
    vm_switch_program(vm, prog);
}

// Math Helpers for arithmetic operations
static Value add_values(Value v1, Value v2) {
    if (IS_INTEGER(v1) && IS_INTEGER(v2)) {
        int64_t sum = (int64_t)get_integer(v1) + get_integer(v2);
        if (sum >= INT32_MIN && sum <= INT32_MAX) {
            return make_integer((int32_t)sum);
        }
        return make_double((double)sum);
    }
    
    double d1 = 0, d2 = 0;
    bool n1 = false, n2 = false;
    if (IS_DOUBLE(v1)) { d1 = get_double(v1); n1 = true; }
    else if (IS_INTEGER(v1)) { d1 = get_integer(v1); n1 = true; }
    
    if (IS_DOUBLE(v2)) { d2 = get_double(v2); n2 = true; }
    else if (IS_INTEGER(v2)) { d2 = get_integer(v2); n2 = true; }
    
    if (n1 && n2) {
        return make_double(d1 + d2);
    }
    
    // JS String concatenation fallback
    Value str1_val = value_to_string(v1);
    if (g_current_vm) vm_push_root(g_current_vm, str1_val);
    Value str2_val = value_to_string(v2);
    if (g_current_vm) vm_push_root(g_current_vm, str2_val);
    
    JSString* js_s1 = (JSString*)get_pointer(str1_val);
    JSString* js_s2 = (JSString*)get_pointer(str2_val);
    
    char* concat = malloc(js_s1->length + js_s2->length + 1);
    memcpy(concat, js_s1->data, js_s1->length);
    memcpy(concat + js_s1->length, js_s2->data, js_s2->length);
    concat[js_s1->length + js_s2->length] = '\0';
    
    Value res = create_string(concat, js_s1->length + js_s2->length);
    free(concat);
    if (g_current_vm) {
        vm_pop_root(g_current_vm);
        vm_pop_root(g_current_vm);
    }
    return res;
}

static Value sub_values(Value v1, Value v2) {
    if (IS_INTEGER(v1) && IS_INTEGER(v2)) {
        int64_t sum = (int64_t)get_integer(v1) - get_integer(v2);
        if (sum >= INT32_MIN && sum <= INT32_MAX) {
            return make_integer((int32_t)sum);
        }
    }
    double d1 = IS_INTEGER(v1) ? get_integer(v1) : (IS_DOUBLE(v1) ? get_double(v1) : 0.0);
    double d2 = IS_INTEGER(v2) ? get_integer(v2) : (IS_DOUBLE(v2) ? get_double(v2) : 0.0);
    return make_double(d1 - d2);
}

static Value mul_values(Value v1, Value v2) {
    if (IS_INTEGER(v1) && IS_INTEGER(v2)) {
        int64_t sum = (int64_t)get_integer(v1) * get_integer(v2);
        if (sum >= INT32_MIN && sum <= INT32_MAX) {
            return make_integer((int32_t)sum);
        }
    }
    double d1 = IS_INTEGER(v1) ? get_integer(v1) : (IS_DOUBLE(v1) ? get_double(v1) : 0.0);
    double d2 = IS_INTEGER(v2) ? get_integer(v2) : (IS_DOUBLE(v2) ? get_double(v2) : 0.0);
    return make_double(d1 * d2);
}

static Value div_values(Value v1, Value v2) {
    double d1 = IS_INTEGER(v1) ? get_integer(v1) : (IS_DOUBLE(v1) ? get_double(v1) : 0.0);
    double d2 = IS_INTEGER(v2) ? get_integer(v2) : (IS_DOUBLE(v2) ? get_double(v2) : 0.0);
    return make_double(d1 / d2);
}

static Value mod_values(Value v1, Value v2) {
    if (IS_INTEGER(v1) && IS_INTEGER(v2) && get_integer(v2) != 0) {
        return make_integer(get_integer(v1) % get_integer(v2));
    }
    double d1 = IS_INTEGER(v1) ? get_integer(v1) : (IS_DOUBLE(v1) ? get_double(v1) : 0.0);
    double d2 = IS_INTEGER(v2) ? get_integer(v2) : (IS_DOUBLE(v2) ? get_double(v2) : 0.0);
    return make_double(fmod(d1, d2));
}

static bool compare_values(Opcode op, Value v1, Value v2) {
    if (v1 == v2) {
        if (op == OP_EQ || op == OP_LE || op == OP_GE) return true;
        if (op == OP_NE || op == OP_LT || op == OP_GT) return false;
    }
    
    // JS loose equality for null and undefined:
    // In JavaScript, `null == undefined` is true, and `null == null` is true.
    // However, `null` or `undefined` loosely compared to any other object type
    // (such as a function, array, or object) must always evaluate to false.
    if ((v1 == VAL_NULL || v1 == VAL_UNDEFINED) && (v2 == VAL_NULL || v2 == VAL_UNDEFINED)) {
        if (op == OP_EQ) return true;
        if (op == OP_NE) return false;
    }
    
    // If one is null/undefined and the other is an object/primitive, they are not equal.
    if (v1 == VAL_NULL || v1 == VAL_UNDEFINED || v2 == VAL_NULL || v2 == VAL_UNDEFINED) {
        if (op == OP_EQ) return false;
        if (op == OP_NE) return true;
    }
    
    if (IS_POINTER(v1) && IS_POINTER(v2)) {
        BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(v1) - sizeof(BlockHeader));
        BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(v2) - sizeof(BlockHeader));
        if (h1->obj_type == OBJ_STRING && h2->obj_type == OBJ_STRING) {
            JSString* s1 = (JSString*)get_pointer(v1);
            JSString* s2 = (JSString*)get_pointer(v2);
            int cmp = strcmp(s1->data, s2->data);
            switch (op) {
                case OP_LT: return cmp < 0;
                case OP_LE: return cmp <= 0;
                case OP_GT: return cmp > 0;
                case OP_GE: return cmp >= 0;
                case OP_EQ: return cmp == 0;
                case OP_NE: return cmp != 0;
                default: return false;
            }
        }
        // If they are pointers but not the same, and not both strings, they are not equal
        if (op == OP_EQ) return false;
        if (op == OP_NE) return true;
    }
    
    double d1 = IS_INTEGER(v1) ? get_integer(v1) : (IS_DOUBLE(v1) ? get_double(v1) : 0.0);
    double d2 = IS_INTEGER(v2) ? get_integer(v2) : (IS_DOUBLE(v2) ? get_double(v2) : 0.0);
    switch (op) {
        case OP_LT: return d1 < d2;
        case OP_LE: return d1 <= d2;
        case OP_GT: return d1 > d2;
        case OP_GE: return d1 >= d2;
        case OP_EQ: return d1 == d2;
        case OP_NE: return d1 != d2;
        default: return false;
    }
}

// Core VM Execution Loop using Computed Gotos (Direct Threading)
Value coro_resume_helper(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)arg_count;
    VMCoroutine* coro = (VMCoroutine*)get_pointer(this_val);
    Value res = arg_count > 0 ? args[0] : VAL_UNDEFINED;
    coro->is_error = false;
    vm_coro_resume(vm, coro, res);
    return VAL_UNDEFINED;
}

Value coro_resume_throw_helper(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)arg_count;
    VMCoroutine* coro = (VMCoroutine*)get_pointer(this_val);
    Value err = arg_count > 0 ? args[0] : VAL_UNDEFINED;
    coro->is_error = true;
    vm_coro_resume(vm, coro, err);
    return VAL_UNDEFINED;
}

/*
 * ===========================================================================
 * PRIMARY BYTECODE EXECUTION LOOP (DIRECT THREADING)
 * ===========================================================================
 * Curica abandons standard switch-based evaluation in favor of GCC's 
 * Computed Goto extension. This builds a static array of instruction pointers
 * mapping opcodes directly to C memory addresses.
 * 
 * At the end of every opcode, `DISPATCH()` immediately leaps to the next
 * instruction address, bypassing branch decoding and achieving hardware-level
 * performance.
 */
static void print_vm_callstack(VM* vm) {
    printf("VM Callstack:\n");
    for (uint32_t i = 0; i < vm->frame_count; i++) {
        CallFrame* frame = &vm->frames[i];
        if (IS_POINTER(frame->func)) {
            BlockHeader* h = (BlockHeader*)((char*)get_pointer(frame->func) - sizeof(BlockHeader));
            if (h->obj_type == OBJ_FUNCTION) {
                JSFunction* f = (JSFunction*)get_pointer(frame->func);
                const char* name = "anonymous";
                if (IS_POINTER(f->name)) {
                    BlockHeader* nh = (BlockHeader*)((char*)get_pointer(f->name) - sizeof(BlockHeader));
                    if (nh->obj_type == OBJ_STRING) {
                        name = ((JSString*)get_pointer(f->name))->data;
                    }
                }
                printf("  [%u] function '%s' (ip: %u, reg_base: %u, new_target: 0x%lx, old_this: 0x%lx)\n", i, name, frame->ip, frame->reg_base, frame->new_target, frame->old_this);
            } else {
                printf("  [%u] non-function frame, type=%d\n", i, h->obj_type);
            }
        } else {
            printf("  [%u] non-pointer frame func\n", i);
        }
    }
}

/**
 * @brief Core Bytecode Dispatch Loop
 *
 * `vm_run` iterates through the pre-compiled CBC (Curica Bytecode) array.
 * It uses a flat register array (a Register-Based ISA) which eliminates
 * transient stack allocations for variable storage. 
 *
 * Execution halts when a RETURN instruction drops the active CallFrame 
 * back to the initial `stop_frame_count`.
 * 
 * @param vm Pointer to the Virtual Machine instance.
 * @return The final evaluated Value, or VAL_UNDEFINED if none.
 */
Value vm_run(VM* vm) {
    // Set active VM reference for GC allocation traps
    g_current_vm = vm;
    uint32_t stop_frame_count = vm->frame_count > 0 ? vm->frame_count - 1 : 0;
    
    // Initialize the main call frame if called for the first time
    if (vm->frame_count == 0) {
        CompilerFuncInfo* f_meta = (CompilerFuncInfo*)vm->functions;
        Value main_name = create_string("main", 4);
        Value main_fn = create_function(
            vm->current_prog,
            f_meta[0].bytecode_offset,
            f_meta[0].register_count,
            f_meta[0].param_count,
            f_meta[0].is_async,
            VAL_NULL,
            main_name
        );
        
        vm->frame_count = 1;
        if (vm->frame_capacity < 8) {
            vm->frame_capacity = 8;
            vm->frames = malloc(8 * sizeof(CallFrame));
        }
        
        vm->frames[0].func = main_fn;
        vm->frames[0].prog = vm->current_prog;
        vm->frames[0].ip = f_meta[0].bytecode_offset;
        vm->frames[0].reg_base = 0;
        vm->frames[0].reg_count = f_meta[0].register_count;
        vm->frames[0].env = VAL_NULL;
        vm->frames[0].coro = NULL;
        vm->frames[0].new_target = VAL_UNDEFINED;
        vm->frames[0].old_this = VAL_UNDEFINED;
        
        uint32_t needed_regs = f_meta[0].register_count;
        if (needed_regs > vm->reg_capacity) {
            vm->reg_capacity = needed_regs * 2;
            vm->registers = malloc(vm->reg_capacity * sizeof(Value));
        }
        for (uint32_t i = 0; i < vm->reg_capacity; i++) {
            vm->registers[i] = VAL_UNDEFINED;
        }
        vm->reg_count = needed_regs;
    }
    
    CallFrame* frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);
    const uint32_t* pc = vm->bytecode + frame->ip;
    Value* regs = vm->registers + frame->reg_base;
    
    // Direct threaded dispatch labels array (Computed Gotos — must match Opcode enum order)
    static void* dispatch_table[] = {
        &&do_load_const,        // OP_LOAD_CONST
        &&do_load_int,          // OP_LOAD_INT
        &&do_load_bool,         // OP_LOAD_BOOL
        &&do_load_null,         // OP_LOAD_NULL
        &&do_load_undefined,    // OP_LOAD_UNDEFINED
        &&do_load_global,       // OP_LOAD_GLOBAL
        &&do_store_global,      // OP_STORE_GLOBAL
        &&do_load_env,          // OP_LOAD_ENV
        &&do_store_env,         // OP_STORE_ENV
        &&do_load_prop,         // OP_LOAD_PROP
        &&do_store_prop,        // OP_STORE_PROP
        &&do_delete_prop,       // OP_DELETE_PROP
        &&do_move,              // OP_MOVE
        &&do_add,               // OP_ADD
        &&do_sub,               // OP_SUB
        &&do_mul,               // OP_MUL
        &&do_div,               // OP_DIV
        &&do_mod,               // OP_MOD
        &&do_pow,               // OP_POW
        &&do_concat,            // OP_CONCAT
        &&do_lt,                // OP_LT
        &&do_le,                // OP_LE
        &&do_gt,                // OP_GT
        &&do_ge,                // OP_GE
        &&do_eq,                // OP_EQ
        &&do_ne,                // OP_NE
        &&do_eq_loose,          // OP_EQ_LOOSE
        &&do_ne_loose,          // OP_NE_LOOSE
        &&do_in,                // OP_IN
        &&do_instanceof,        // OP_INSTANCEOF
        &&do_not,               // OP_NOT
        &&do_neg,               // OP_NEG
        &&do_typeof,            // OP_TYPEOF
        &&do_void,              // OP_VOID
        &&do_bitand,            // OP_BITAND
        &&do_bitor,             // OP_BITOR
        &&do_bitxor,            // OP_BITXOR
        &&do_bitnot,            // OP_BITNOT
        &&do_shl,               // OP_SHL
        &&do_shr,               // OP_SHR
        &&do_ushr,              // OP_USHR
        &&do_inc,               // OP_INC
        &&do_dec,               // OP_DEC
        &&do_jump,              // OP_JUMP
        &&do_jump_if_false,     // OP_JUMP_IF_FALSE
        &&do_jump_if_true,      // OP_JUMP_IF_TRUE
        &&do_jump_if_nullish,   // OP_JUMP_IF_NULLISH
        &&do_call,              // OP_CALL
        &&do_new_call,          // OP_NEW_CALL
        &&do_return,            // OP_RETURN
        &&do_throw,             // OP_THROW
        &&do_try_begin,         // OP_TRY_BEGIN
        &&do_try_end,           // OP_TRY_END
        &&do_catch_begin,       // OP_CATCH_BEGIN
        &&do_new_object,        // OP_NEW_OBJECT
        &&do_new_array,         // OP_NEW_ARRAY
        &&do_new_function,      // OP_NEW_FUNCTION
        &&do_new_env,           // OP_NEW_ENV
        &&do_new_regex,         // OP_NEW_REGEX
        &&do_array_push,        // OP_ARRAY_PUSH
        &&do_array_spread,      // OP_ARRAY_SPREAD
        &&do_obj_spread,        // OP_OBJ_SPREAD
        &&do_iter_next,         // OP_ITER_NEXT
        &&do_for_in_next,       // OP_FOR_IN_NEXT
        &&do_get_iter,          // OP_GET_ITER
        &&do_print,             // OP_PRINT
        &&do_await,             // OP_AWAIT
        &&do_yield,             // OP_YIELD
    };
#ifdef DEBUG_VM
    #define DISPATCH() do { printf("OPCODE: %d, A: %d, B: %d, C: %d\n", INST_OP(*pc), INST_A(*pc), INST_B(*pc), INST_C(*pc)); goto *dispatch_table[INST_OP(*pc)]; } while(0)
#else
    #define DISPATCH() goto *dispatch_table[INST_OP(*pc)]
#endif
    #define NEXT() pc++; DISPATCH()
    
    DISPATCH();
    
do_load_const: {
    uint8_t a = INST_A(*pc);
    uint16_t bx = INST_BX(*pc);
    regs[a] = vm->const_pool[bx];
    NEXT();
}

do_load_int: {
    uint8_t a = INST_A(*pc);
    int16_t sbx = INST_SBX(*pc);
    regs[a] = make_integer(sbx);
    NEXT();
}

do_load_bool: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    regs[a] = make_boolean(b != 0);
    NEXT();
}

do_load_null: {
    uint8_t a = INST_A(*pc);
    regs[a] = VAL_NULL;
    NEXT();
}

do_load_undefined: {
    uint8_t a = INST_A(*pc);
    regs[a] = VAL_UNDEFINED;
    NEXT();
}

do_load_global: {
    uint8_t a = INST_A(*pc);
    uint16_t bx = INST_BX(*pc);
    Value key = vm->const_pool[bx];
    regs[a] = object_get(vm->global_obj, key);
    
    if (regs[a] == VAL_UNDEFINED && IS_POINTER(key)) {
        JSString* ks = (JSString*)get_pointer(key);
        printf("WARNING: OP_LOAD_GLOBAL failed to find '%s'!\n", ks->data);
    }
    
    NEXT();
}

do_store_global: {
    uint8_t a = INST_A(*pc);
    uint16_t bx = INST_BX(*pc);
    Value key = vm->const_pool[bx];
    object_set(vm->global_obj, key, regs[a]);
    NEXT();
}

do_load_env: {
    uint8_t a = INST_A(*pc);
    uint8_t idx = INST_B(*pc);
    uint8_t depth = INST_C(*pc);
    Value env_val = frame->env;
    for (uint8_t i = 0; i < depth; i++) {
        JSEnvironment* env = (JSEnvironment*)get_pointer(env_val);
        env_val = env->parent;
    }
    JSEnvironment* env = (JSEnvironment*)get_pointer(env_val);
    Value loaded = env->values[idx];
    if (loaded == VAL_UNINITIALIZED) {
        vm_throw_error(vm, create_error("ReferenceError",
            create_string("Cannot access variable before initialization", 44)));
    }
    regs[a] = loaded;
    NEXT();
}

do_store_env: {
    uint8_t a = INST_A(*pc);
    uint8_t idx = INST_B(*pc);
    uint8_t depth = INST_C(*pc);
    Value env_val = frame->env;
    for (uint8_t i = 0; i < depth; i++) {
        JSEnvironment* env = (JSEnvironment*)get_pointer(env_val);
        env_val = env->parent;
    }
    JSEnvironment* env = (JSEnvironment*)get_pointer(env_val);
    env->values[idx] = regs[a];
    gc_write_barrier(env_val, regs[a]);
    NEXT();
}

do_load_prop: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    Value obj = regs[b];
    Value prop = regs[c];
    
    if (IS_POINTER(obj)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(obj) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_OBJECT) {
            JSObject* o = (JSObject*)get_pointer(obj);
            uint32_t pc_offset = pc - vm->bytecode;
            if (!frame->prog->ic_table) {
                printf("FATAL: ic_table is NULL at pc_offset %u!\n", pc_offset);
                exit(1);
            }
            uint32_t cached_index = frame->prog->ic_table[pc_offset];
            
            if (cached_index < o->count && o->properties[cached_index].key == prop) {
                regs[a] = o->properties[cached_index].value;
                NEXT();
            }
        } else if (h->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(obj);
            if (IS_INTEGER(prop)) {
                int32_t idx = get_integer(prop);
                regs[a] = (idx >= 0 && (uint32_t)idx < arr->length) ? arr->elements[idx] : VAL_UNDEFINED;
                NEXT();
            }
        } else if (h->obj_type == OBJ_FLOAT16_ARRAY) {
            JSFloat16Array* arr = (JSFloat16Array*)get_pointer(obj);
            if (IS_INTEGER(prop)) {
                int32_t idx = get_integer(prop);
                regs[a] = (idx >= 0 && (uint32_t)idx < arr->length) ? make_double(half_to_float(arr->data[idx])) : VAL_UNDEFINED;
                NEXT();
            }
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(obj);
            int32_t idx = -1;
            if (IS_INTEGER(prop)) idx = get_integer(prop);
            else if (IS_DOUBLE(prop)) idx = (int32_t)get_double(prop);
            else if (IS_POINTER(prop)) {
                BlockHeader* ph = (BlockHeader*)((char*)get_pointer(prop) - sizeof(BlockHeader));
                if (ph->obj_type == OBJ_STRING) {
                    char* end;
                    JSString* ks = (JSString*)get_pointer(prop);
                    long parsed = strtol(ks->data, &end, 10);
                    if (end != ks->data && *end == '\0') idx = (int32_t)parsed;
                }
            }
            
            if (idx >= 0) {
                regs[a] = (idx < (int32_t)buf->length) ? make_integer(buf->data[idx]) : VAL_UNDEFINED;
                NEXT();
            }
        } else if (h->obj_type == OBJ_STRING) {
            JSString* str = (JSString*)get_pointer(obj);
            int32_t idx = -1;
            if (IS_INTEGER(prop)) idx = get_integer(prop);
            else if (IS_DOUBLE(prop)) idx = (int32_t)get_double(prop);
            
            if (idx >= 0 && (size_t)idx < str->length) {
                regs[a] = create_string(&str->data[idx], 1);
                NEXT();
            }
        }
    }
    
    if (IS_INTEGER(prop)) {
        // Convert integer prop to string for general lookup
        char idx_buf[16];
        int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", get_integer(prop));
        prop = create_string(idx_buf, idx_len);
    }
    
    regs[a] = object_get(obj, prop);
    
    // Update cache if object was plain object and property was found
    if (IS_POINTER(obj)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(obj) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_OBJECT) {
            JSObject* o = (JSObject*)get_pointer(obj);
            for (uint32_t i = 0; i < o->count; i++) {
                if (o->properties[i].key == prop) {
                    uint32_t pc_offset = pc - vm->bytecode;
                    frame->prog->ic_table[pc_offset] = i;
                    break;
                }
            }
        }
    }
    
    NEXT();
}

do_store_prop: {
    uint8_t a = INST_A(*pc); // Object
    uint8_t b = INST_B(*pc); // Property key
    uint8_t c = INST_C(*pc); // Value
    Value obj = regs[a];
    Value prop = regs[b];
    Value val = regs[c];
    
    if (IS_POINTER(obj)) {
        BlockHeader* header = (BlockHeader*)((char*)get_pointer(obj) - sizeof(BlockHeader));
        if (header->obj_type == OBJ_OBJECT) {
            JSObject* o = (JSObject*)get_pointer(obj);
            uint32_t pc_offset = pc - vm->bytecode;
            uint32_t cached_index = frame->prog->ic_table[pc_offset];
            
            if (cached_index < o->count && o->properties[cached_index].key == prop) {
                o->properties[cached_index].value = val;
                NEXT();
            }
            
            object_set(obj, prop, val);
            
            // Update cache
            for (uint32_t i = 0; i < o->count; i++) {
                if (o->properties[i].key == prop) {
                    frame->prog->ic_table[pc_offset] = i;
                    break;
                }
            }
            NEXT();
        } else if (header->obj_type == OBJ_FUNCTION || header->obj_type == OBJ_ERROR) {
            object_set(obj, prop, val);
        } else if (header->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(obj);
            int32_t idx = -1;
            if (IS_INTEGER(prop)) idx = get_integer(prop);
            else if (IS_DOUBLE(prop)) idx = (int32_t)get_double(prop);
            
            if (idx >= 0) {
                if (idx >= (int32_t)arr->capacity) {
                    uint32_t new_cap = idx + 1;
                    if (new_cap < arr->capacity * 2) new_cap = arr->capacity * 2;
                    Value* old_elems = arr->elements;
                    // Trigger GC inside if needed
                    Value* new_elems = arena_alloc(OBJ_ARRAY_DATA, new_cap * sizeof(Value));
                    memcpy(new_elems, old_elems, arr->capacity * sizeof(Value));
                    for (uint32_t i = arr->capacity; i < new_cap; i++) {
                        new_elems[i] = VAL_EMPTY;
                    }
                    arr->elements = new_elems;
                    arr->capacity = new_cap;
                }
                arr->elements[idx] = val;
                if (idx >= (int32_t)arr->length) {
                    arr->length = idx + 1;
                }
                NEXT();
            }
            object_set(obj, prop, val);
        } else if (header->obj_type == OBJ_FLOAT16_ARRAY) {
            JSFloat16Array* arr = (JSFloat16Array*)get_pointer(obj);
            int32_t idx = -1;
            if (IS_INTEGER(prop)) idx = get_integer(prop);
            else if (IS_DOUBLE(prop)) idx = (int32_t)get_double(prop);
            
            if (idx >= 0 && idx < (int32_t)arr->length) {
                double d_val = 0.0;
                if (IS_INTEGER(val)) d_val = get_integer(val);
                else if (IS_DOUBLE(val)) d_val = get_double(val);
                arr->data[idx] = float_to_half((float)d_val);
                NEXT();
            }
            object_set(obj, prop, val);
        } else if (header->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(obj);
            int32_t idx = -1;
            if (IS_INTEGER(prop)) idx = get_integer(prop);
            else if (IS_DOUBLE(prop)) idx = (int32_t)get_double(prop);
            
            if (idx >= 0 && idx < (int32_t)buf->length) {
                uint8_t byte_val = 0;
                if (IS_INTEGER(val)) byte_val = (uint8_t)get_integer(val);
                else if (IS_DOUBLE(val)) byte_val = (uint8_t)get_double(val);
                buf->data[idx] = byte_val;
                NEXT();
            }
            object_set(obj, prop, val);
        }
    }
    NEXT();
}

do_move: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    regs[a] = regs[b];
    NEXT();
}

do_add: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = add_values(regs[b], regs[c]);
    NEXT();
}

do_sub: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = sub_values(regs[b], regs[c]);
    NEXT();
}

do_mul: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = mul_values(regs[b], regs[c]);
    NEXT();
}

do_div: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = div_values(regs[b], regs[c]);
    NEXT();
}

do_mod: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = mod_values(regs[b], regs[c]);
    NEXT();
}

do_lt: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_LT, regs[b], regs[c]));
    NEXT();
}

do_le: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_LE, regs[b], regs[c]));
    NEXT();
}

do_gt: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_GT, regs[b], regs[c]));
    NEXT();
}

do_ge: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_GE, regs[b], regs[c]));
    NEXT();
}

do_eq: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_EQ, regs[b], regs[c]));
    NEXT();
}

do_ne: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(compare_values(OP_NE, regs[b], regs[c]));
    NEXT();
}

do_not: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    regs[a] = make_boolean(!is_truthy(regs[b]));
    NEXT();
}

do_neg: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    double d = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : get_double(regs[b]);
    regs[a] = make_double(-d);
    NEXT();
}

do_jump: {
    int16_t sbx = INST_SBX(*pc);
    pc += sbx;
    NEXT();
}

do_jump_if_false: {
    uint8_t a = INST_A(*pc);
    int16_t sbx = INST_SBX(*pc);
    if (!is_truthy(regs[a])) {
        pc += sbx;
    }
    NEXT();
}

do_jump_if_true: {
    uint8_t a = INST_A(*pc);
    int16_t sbx = INST_SBX(*pc);
    if (is_truthy(regs[a])) {
        pc += sbx;
    }
    NEXT();
}

do_call: {
    uint8_t a = INST_A(*pc); // Call target result register
    uint8_t b = INST_B(*pc); // Register containing function closure
    uint8_t c = INST_C(*pc); // Number of parameters
    
    Value func_val = regs[b];
    
    if (!IS_POINTER(func_val)) {
        vm_throw_error(vm, create_string("TypeError: caller is not a function", 35));
        DISPATCH(); // skip
    }
    
    JSFunction* f = (JSFunction*)get_pointer(func_val);  
    BlockHeader* h = (BlockHeader*)((char*)f - sizeof(BlockHeader));
    if (h->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_string("TypeError: caller is not a function", 35));
        DISPATCH(); // skip
    }
    JSFunction* actual_f = f;
    Value old_this = VAL_UNDEFINED;
    if (f->bytecode_offset != 0xffffffff && f->native_ptr != NULL) {
        actual_f = (JSFunction*)f->native_ptr;
        Value this_key = create_string("this", 4);
        /* Refresh pointers after GC-triggering create_string */
        f = (JSFunction*)get_pointer(func_val);
        actual_f = (JSFunction*)f->native_ptr;
        old_this = object_get(vm->global_obj, this_key);
        object_set(vm->global_obj, this_key, f->env);
        /* Refresh again after object_get/set */
        f = (JSFunction*)get_pointer(func_val);
        actual_f = (JSFunction*)f->native_ptr;
    }
    
    if (actual_f->bytecode_offset == 0xffffffff) {
        // Execute native C function directly without pushing new VM call stack frame
        // Re-derive actual_f from func_val in case GC moved the object since the initial pointer load
        JSFunction* fresh_f = (JSFunction*)get_pointer(func_val);
        if (f == actual_f) {
            actual_f = fresh_f; /* direct native: actual_f == original f */
        } else {
            actual_f = (JSFunction*)fresh_f->native_ptr; /* bound wrapper: actual_f is the inner function */
        }
        Value (*native)(VM*, Value, int, Value*) = (Value (*)(VM*, Value, int, Value*))actual_f->native_ptr;
        g_current_native_function = actual_f;
        Value res = native(vm, actual_f->env, c, &regs[b + 1]);
        g_current_native_function = NULL;
        regs = vm->registers + frame->reg_base;
        regs[a] = res;
        NEXT();
    } else if (actual_f->is_async) {
        // Create Promise and Coroutine for async function
        Value promise = create_promise(0 /* PENDING */, VAL_UNDEFINED);
        VMCoroutine* coro = vm_coro_create(vm);
        coro->promise = promise;
        
        // Push initial frame onto coroutine's stack
        coro->frame_count = 1;
        CallFrame* new_frame = &coro->frames[0];
        new_frame->prog = actual_f->prog;
        new_frame->func = func_val;
        new_frame->ip = actual_f->bytecode_offset;
        new_frame->reg_base = 0;
        new_frame->reg_count = actual_f->register_count;
        new_frame->env = actual_f->env;
        new_frame->coro = coro;
        new_frame->new_target = VAL_UNDEFINED;
        new_frame->old_this = old_this;
        
        // Ensure coroutine registers are sufficient
        if (actual_f->register_count > coro->register_capacity) {
            coro->register_capacity = actual_f->register_count * 2;
            coro->registers = realloc(coro->registers, coro->register_capacity * sizeof(Value));
        }
        
        // Copy arguments from current VM stack to coroutine's register stack
        for (uint32_t i = 0; i < c; i++) {
            coro->registers[i] = regs[b + 1 + i];
        }
        for (uint32_t i = c; i < actual_f->register_count; i++) {
            coro->registers[i] = VAL_UNDEFINED;
        }
        
        // Put the promise into the caller's result register BEFORE swapping
        regs[a] = promise;
        
        // Yield execution to the newly spawned coroutine
        vm_coro_resume(vm, coro, VAL_UNDEFINED);
        
        NEXT();
    } else {
        // Save current instruction pointer back to call frame before pushing next
        frame->ip = pc - vm->bytecode;
        
        if (vm->frame_count >= vm->frame_capacity) {
            vm->frame_capacity = vm->frame_capacity == 0 ? 8 : vm->frame_capacity * 2;
            vm->frames = realloc(vm->frames, vm->frame_capacity * sizeof(CallFrame));
            frame = &vm->frames[vm->frame_count - 1];
        }
        
        CallFrame* new_frame = &vm->frames[vm->frame_count++];
        new_frame->func = func_val;
        new_frame->prog = actual_f->prog;
        new_frame->ip = actual_f->bytecode_offset;
        new_frame->reg_base = frame->reg_base + b + 1;
        new_frame->reg_count = actual_f->register_count;
        new_frame->env = actual_f->env;
        new_frame->coro = frame->coro;
        new_frame->new_target = VAL_UNDEFINED;
        new_frame->old_this = old_this;
        
        uint32_t needed_regs = new_frame->reg_base + new_frame->reg_count;
        if (needed_regs > vm->reg_capacity) {
            uint32_t old_cap = vm->reg_capacity;
            vm->reg_capacity = needed_regs * 2;
            vm->registers = realloc(vm->registers, vm->reg_capacity * sizeof(Value));
            for (uint32_t i = old_cap; i < vm->reg_capacity; i++) {
                vm->registers[i] = VAL_UNDEFINED;
            }
        }
        
        // Initialize rest of the local registers to undefined
        for (uint32_t i = c; i < actual_f->register_count; i++) {
            vm->registers[new_frame->reg_base + i] = VAL_UNDEFINED;
        }
        
        frame = new_frame;
        vm_switch_program(vm, frame->prog);
        regs = vm->registers + frame->reg_base;
        pc = vm->bytecode + frame->ip;
        DISPATCH();
    }
}

do_new_call: {
    uint8_t a = INST_A(*pc); // result register
    uint8_t b = INST_B(*pc); // register containing constructor function
    uint8_t c = INST_C(*pc); // argument count
    
    Value ctor_val = regs[b];
    if (!IS_POINTER(ctor_val)) {
        vm_throw_error(vm, create_string("TypeError: constructor is not a function", 39));
    }
    JSFunction* ctor_f = (JSFunction*)get_pointer(ctor_val);
    BlockHeader* ctor_h = (BlockHeader*)((char*)ctor_f - sizeof(BlockHeader));
    if (ctor_h->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_string("TypeError: constructor is not a function", 39));
    }
    
    JSFunction* actual_ctor = ctor_f;
    if (ctor_f->bytecode_offset != 0xffffffff && ctor_f->native_ptr != NULL) {
        actual_ctor = (JSFunction*)ctor_f->native_ptr;
    }
    
    // 1. Create a new object instance
    Value new_obj = create_object();
    vm_push_root(vm, new_obj);
    
    // 2. Set __proto__ from Constructor.prototype if available
    Value proto_key = create_string("prototype", 9);
    Value ctor_proto = object_get(make_pointer(actual_ctor), proto_key);
    if (IS_POINTER(ctor_proto)) {
        Value proto_prop_key = create_string("__proto__", 9);
        object_set(new_obj, proto_prop_key, ctor_proto);
    }
    
    // 3. Save old `this`, set new_obj as `this` in global scope
    Value this_key = create_string("this", 4);
    Value old_this = object_get(vm->global_obj, this_key);
    object_set(vm->global_obj, this_key, new_obj);
    
    // 4. Call the constructor (native or bytecode)
    if (actual_ctor->bytecode_offset == 0xffffffff) {
        Value (*native)(VM*, Value, int, Value*) = (Value (*)(VM*, Value, int, Value*))actual_ctor->native_ptr;
        g_current_native_function = actual_ctor;
        Value res = native(vm, new_obj, c, &regs[b + 1]);
        g_current_native_function = NULL;
        
        // Refresh regs pointer!
        regs = vm->registers + frame->reg_base;
        
        // Restore old this and pop GC root
        object_set(vm->global_obj, this_key, old_this);
        // new_obj is still valid (GC root kept it alive); pop the root now
        vm_pop_root(vm);
        if (IS_POINTER(res)) {
            regs[a] = res;
        } else {
            regs[a] = new_obj;
        }
        NEXT();
    }
    
    // Bytecode constructor: push a new frame, mark it with the new_obj so
    // do_return can intercept and place new_obj as the result.
    frame->ip = pc - vm->bytecode;
    
    if (vm->frame_count >= vm->frame_capacity) {
        vm->frame_capacity = vm->frame_capacity == 0 ? 8 : vm->frame_capacity * 2;
        vm->frames = realloc(vm->frames, vm->frame_capacity * sizeof(CallFrame));
        frame = &vm->frames[vm->frame_count - 1];
    }
    
    CallFrame* new_frame = &vm->frames[vm->frame_count++];
    new_frame->func = ctor_val;
    new_frame->prog = actual_ctor->prog;
    new_frame->ip = actual_ctor->bytecode_offset;
    new_frame->reg_base = frame->reg_base + b + 1;
    new_frame->reg_count = actual_ctor->register_count;
    new_frame->env = actual_ctor->env;
    new_frame->coro = frame->coro;
    new_frame->new_target = new_obj; // Store new_obj for do_return to handle
    new_frame->old_this = old_this;  // Store old_this to restore on do_return
    
    uint32_t needed_regs = new_frame->reg_base + new_frame->reg_count;
    if (needed_regs > vm->reg_capacity) {
        uint32_t old_cap = vm->reg_capacity;
        vm->reg_capacity = needed_regs * 2;
        vm->registers = realloc(vm->registers, vm->reg_capacity * sizeof(Value));
        for (uint32_t i = old_cap; i < vm->reg_capacity; i++) {
            vm->registers[i] = VAL_UNDEFINED;
        }
    }
    
    for (uint32_t i = c; i < actual_ctor->register_count; i++) {
        vm->registers[new_frame->reg_base + i] = VAL_UNDEFINED;
    }
    
    frame = new_frame;
    vm_switch_program(vm, frame->prog);
    regs = vm->registers + frame->reg_base;
    pc = vm->bytecode + frame->ip;
    DISPATCH();
}

do_return: {
    uint8_t a = INST_A(*pc);
    Value ret_val = regs[a];
    
    // If this is a constructor frame (new_target is set), check if we need to return new_target
    Value new_target = frame->new_target;
    if (IS_POINTER(new_target)) {
        // The new_obj is in new_target; return it unless the constructor explicitly returned an object
        if (!IS_POINTER(ret_val)) {
            ret_val = new_target;
        }
        vm_pop_root(vm); // Pop the new_obj root we pushed in do_new_call
    }
    
    // Restore global `this` to old_this if it was saved (for both constructors and bound method calls)
    if (frame->old_this != VAL_UNDEFINED) {
        Value this_key = create_string("this", 4);
        object_set(vm->global_obj, this_key, frame->old_this);
    }
    
    vm->frame_count--;
    if (vm->frame_count == stop_frame_count) {
        // Thread execution has finished, return result
        return ret_val;
    }
    
    // Restore parent calling context frame
    frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);
    regs = vm->registers + frame->reg_base;
    
    // Read the original destination register 'a' from the calling instruction
    uint32_t call_inst = vm->bytecode[frame->ip];
    uint8_t dest_reg = INST_A(call_inst);
    regs[dest_reg] = ret_val;
    
    frame->ip++; // Advance past call instruction
    pc = vm->bytecode + frame->ip;
    DISPATCH();
}

do_new_object: {
    uint8_t a = INST_A(*pc);
    regs[a] = create_object();
    NEXT();
}

do_new_array: {
    uint8_t a = INST_A(*pc);
    regs[a] = create_array(4);
    NEXT();
}

do_new_function: {
    uint8_t a = INST_A(*pc);
    uint16_t bx = INST_BX(*pc);
    
    CompilerFuncInfo* f_meta = (CompilerFuncInfo*)vm->functions;
    CompilerFuncInfo* target_func = &f_meta[bx];
    
    Value name = create_string("anonymous", 9);
    vm_push_root(vm, name);
    
    Value func_val = create_function(
        frame->prog,
        target_func->bytecode_offset,
        target_func->register_count,
        target_func->param_count,
        target_func->is_async,
        make_pointer(get_pointer(frame->env)),
        name
    );
    vm_pop_root(vm); // name
    
    regs[a] = func_val;
    
    // Update closure env pointer to target the current frame environment
    JSFunction* f_obj = (JSFunction*)get_pointer(regs[a]);
    f_obj->env = frame->env;
    
    // Auto-create .prototype object on the function (needed for ES5 constructor pattern)
    vm_push_root(vm, regs[a]);
    
    Value proto_obj = create_object();
    vm_push_root(vm, proto_obj);
    
    Value proto_key = create_string("prototype", 9);
    vm_push_root(vm, proto_key);
    
    object_set(regs[a], proto_key, proto_obj);
    
    vm_pop_root(vm); // proto_key
    vm_pop_root(vm); // proto_obj
    vm_pop_root(vm); // regs[a]
    
    NEXT();
}

do_new_env: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    // Create new lexical environment pointing to parent environment
    regs[a] = create_environment(frame->env, b);
    frame->env = regs[a]; // Set new environment as active env for the execution frame
    NEXT();
}

do_print: {
    uint8_t a = INST_A(*pc);
    js_console_log(vm, VAL_UNDEFINED, 1, &regs[a]);
    NEXT();
}

do_concat: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    Value sv1 = value_to_string(regs[b]);
    Value sv2 = value_to_string(regs[c]);
    JSString* s1 = (JSString*)get_pointer(sv1);
    JSString* s2 = (JSString*)get_pointer(sv2);
    int total = s1->length + s2->length;
    char* buf = malloc(total + 1);
    memcpy(buf, s1->data, s1->length);
    memcpy(buf + s1->length, s2->data, s2->length);
    buf[total] = '\0';
    regs[a] = create_string(buf, total);
    free(buf);
    NEXT();
}

do_eq_loose: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(values_abstract_equal(regs[b], regs[c]));
    NEXT();
}

do_ne_loose: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    regs[a] = make_boolean(!values_abstract_equal(regs[b], regs[c]));
    NEXT();
}

do_typeof: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    Value v = regs[b];
    const char* type_str;
    if (IS_UNDEFINED(v) || v == VAL_UNINITIALIZED) type_str = "undefined";
    else if (IS_NULL(v))    type_str = "object";
    else if (IS_BOOLEAN(v)) type_str = "boolean";
    else if (IS_INTEGER(v) || IS_DOUBLE(v)) type_str = "number";
    else if (IS_SYMBOL(v))  type_str = "symbol";
    else if (IS_POINTER(v)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(v) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_FUNCTION) type_str = "function";
        else if (h->obj_type == OBJ_STRING) type_str = "string";
        else type_str = "object";
    } else type_str = "undefined";
    regs[a] = create_string(type_str, strlen(type_str));
    NEXT();
}

do_instanceof: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    (void)c;
    // Simplified: check object type against known constructors
    regs[a] = VAL_FALSE;
    if (IS_POINTER(regs[b])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(regs[b]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_OBJECT || h->obj_type == OBJ_ARRAY ||
            h->obj_type == OBJ_ERROR) {
            regs[a] = VAL_TRUE;
        }
    }
    NEXT();
}

do_bitand: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    int32_t v1 = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    int32_t v2 = IS_INTEGER(regs[c]) ? get_integer(regs[c]) : (int32_t)get_double(regs[c]);
    regs[a] = make_integer(v1 & v2);
    NEXT();
}

do_bitor: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    int32_t v1 = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    int32_t v2 = IS_INTEGER(regs[c]) ? get_integer(regs[c]) : (int32_t)get_double(regs[c]);
    regs[a] = make_integer(v1 | v2);
    NEXT();
}

do_bitxor: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    int32_t v1 = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    int32_t v2 = IS_INTEGER(regs[c]) ? get_integer(regs[c]) : (int32_t)get_double(regs[c]);
    regs[a] = make_integer(v1 ^ v2);
    NEXT();
}

do_bitnot: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc);
    int32_t v = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    regs[a] = make_integer(~v);
    NEXT();
}

do_shl: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    int32_t v1 = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    uint32_t v2 = (IS_INTEGER(regs[c]) ? (uint32_t)get_integer(regs[c]) : (uint32_t)get_double(regs[c])) & 31;
    regs[a] = make_integer(v1 << v2);
    NEXT();
}

do_shr: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    int32_t v1 = IS_INTEGER(regs[b]) ? get_integer(regs[b]) : (int32_t)get_double(regs[b]);
    uint32_t v2 = (IS_INTEGER(regs[c]) ? (uint32_t)get_integer(regs[c]) : (uint32_t)get_double(regs[c])) & 31;
    regs[a] = make_integer(v1 >> v2);
    NEXT();
}

do_ushr: {
    uint8_t a = INST_A(*pc), b = INST_B(*pc), c = INST_C(*pc);
    uint32_t v1 = IS_INTEGER(regs[b]) ? (uint32_t)get_integer(regs[b]) : (uint32_t)get_double(regs[b]);
    uint32_t v2 = (IS_INTEGER(regs[c]) ? (uint32_t)get_integer(regs[c]) : (uint32_t)get_double(regs[c])) & 31;
    regs[a] = make_double((double)(v1 >> v2));
    NEXT();
}

do_jump_if_nullish: {
    uint8_t a = INST_A(*pc);
    int16_t sbx = INST_SBX(*pc);
    if (IS_NULL(regs[a]) || IS_UNDEFINED(regs[a])) {
        pc += sbx;
    }
    NEXT();
}

do_throw: {
    uint8_t a = INST_A(*pc);
    vm_throw_error(vm, regs[a]);
    // vm_throw_error longjmps if handler exists, or exits process.
    // If we somehow return, just stop.
    return VAL_UNDEFINED;
}

do_try_begin: {
    int16_t sbx = INST_SBX(*pc);
    uint32_t current_ip = pc - vm->bytecode;
    VMErrorHandler* handler = malloc(sizeof(VMErrorHandler));
    handler->active = 1;
    handler->error = VAL_UNDEFINED;
    handler->saved_frame_count = vm->frame_count;
    handler->saved_reg_count = vm->reg_count;
    handler->catch_ip = current_ip + sbx + 1;
    handler->prev = vm->error_handler;
    vm->error_handler = handler;
    
    if (setjmp(handler->jmp) != 0) {
        // Exception was thrown — restore VM state and jump to catch block
        // IMPORTANT: The local variable `handler` may be clobbered because we are jumping
        // back in time. We MUST fetch the active handler from vm->error_handler!
        VMErrorHandler* h = vm->error_handler;
        vm->frame_count = h->saved_frame_count;
        vm->reg_count = h->saved_reg_count;
        frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);
        regs = vm->registers + frame->reg_base;
        Value caught = h->error;
        // Pop handler from stack
        vm->error_handler = h->prev;
        uint32_t target_ip = h->catch_ip;
        free(h);
        // Store caught value in a temp location (catch_begin reads it)
        vm->registers[vm->reg_count] = caught;  // Temp slot
        // Jump to catch offset
        pc = vm->bytecode + target_ip;
        DISPATCH();
    }
    NEXT();
}

do_try_end: {
    // Normal exit from try block — pop the error handler
    if (vm->error_handler && vm->error_handler->active) {
        VMErrorHandler* h = vm->error_handler;
        vm->error_handler = h->prev;
        free(h);
    }
    NEXT();
}

do_catch_begin: {
    uint8_t a = INST_A(*pc);
    // The caught value was stored in a temp slot by do_try_begin
    regs[a] = vm->registers[vm->reg_count];
    NEXT();
}

do_iter_next: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    uint8_t c = INST_C(*pc);
    Value iter = regs[c];
    
    if (!IS_POINTER(iter)) { regs[b] = VAL_TRUE; NEXT(); }
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(iter) - sizeof(BlockHeader));
    
    if (h->obj_type == OBJ_ARRAY) {
        // Array iterator: use a side register to track index
        // Convention: reg[c+1] holds the current index
        JSArray* arr = (JSArray*)get_pointer(iter);
        int32_t idx = IS_INTEGER(regs[c+1]) ? get_integer(regs[c+1]) : 0;
        if ((uint32_t)idx >= arr->length) {
            regs[b] = VAL_TRUE;
        } else {
            regs[a] = arr->elements[idx];
            regs[b] = VAL_FALSE;
            regs[c+1] = make_integer(idx + 1);
        }
    } else if (h->obj_type == OBJ_ITERATOR_HELPER) {
        JSIteratorHelper* helper = (JSIteratorHelper*)get_pointer(iter);
        if (helper->done) { regs[b] = VAL_TRUE; NEXT(); }
        // Advance iterator helper (inline call to next)
        // Save PC, call next on source, apply transform
        frame->ip = pc - vm->bytecode;
        Value src_next;
        // Get next from source
        Value src = helper->source;
        if (IS_POINTER(src)) {
            BlockHeader* sh = (BlockHeader*)((char*)get_pointer(src) - sizeof(BlockHeader));
            if (sh->obj_type == OBJ_ARRAY) {
                JSArray* arr = (JSArray*)get_pointer(src);
                if ((uint32_t)helper->count >= arr->length) {
                    helper->done = 1;
                    regs[b] = VAL_TRUE;
                    NEXT();
                }
                src_next = arr->elements[helper->count++];
            } else { regs[b] = VAL_TRUE; NEXT(); }
        } else { regs[b] = VAL_TRUE; NEXT(); }
        
        if (helper->helper_type == ITER_MAP) {
            Value mapped = vm_call_function(vm, helper->callback, 1, &src_next);
            frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);
            regs = vm->registers + frame->reg_base;
            regs[a] = mapped;
            regs[b] = VAL_FALSE;
        } else if (helper->helper_type == ITER_FILTER) {
            while (1) {
                Value test = vm_call_function(vm, helper->callback, 1, &src_next);
                frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);
                regs = vm->registers + frame->reg_base;
                if (is_truthy(test)) { regs[a] = src_next; regs[b] = VAL_FALSE; break; }
                // advance source
                if (IS_POINTER(helper->source)) {
                    JSArray* arr = (JSArray*)get_pointer(helper->source);
                    if ((uint32_t)helper->count >= arr->length) { helper->done = 1; regs[b] = VAL_TRUE; break; }
                    src_next = arr->elements[helper->count++];
                } else { regs[b] = VAL_TRUE; break; }
            }
        } else {
            regs[a] = src_next;
            regs[b] = VAL_FALSE;
        }
    } else {
        regs[b] = VAL_TRUE;
    }
    NEXT();
}

do_for_in_next: {
    uint8_t a = INST_A(*pc);  // key register
    uint8_t b = INST_B(*pc);  // done-flag register
    uint8_t c = INST_C(*pc);  // object + index state register
    Value obj = regs[c];
    int32_t idx = IS_INTEGER(regs[c+1]) ? get_integer(regs[c+1]) : 0;
    
    if (!IS_POINTER(obj)) { regs[b] = VAL_TRUE; NEXT(); }
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(obj) - sizeof(BlockHeader));
    if (h->obj_type == OBJ_OBJECT) {
        JSObject* o = (JSObject*)get_pointer(obj);
        if ((uint32_t)idx >= o->count) {
            regs[b] = VAL_TRUE;
        } else {
            regs[a] = o->properties[idx].key;
            regs[b] = VAL_FALSE;
            regs[c+1] = make_integer(idx + 1);
        }
    } else {
        regs[b] = VAL_TRUE;
    }
    NEXT();
}

do_await: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    Value await_val = regs[b];
    
    frame->ip = pc - vm->bytecode + 1; // IP to resume at
    
    VMCoroutine* coro = vm->current_coro;
    if (!coro) {
        vm_throw_error(vm, create_error("SyntaxError", create_string("await is only valid in async functions", 38)));
        return VAL_UNDEFINED;
    }
    
    extern Value coro_resume_helper(VM* vm, Value this_val, int arg_count, Value* args);
    Value resume_func = create_native_function((void*)coro_resume_helper, create_string("resume", 6));
    ((JSFunction*)get_pointer(resume_func))->env = make_pointer(coro);

    extern Value coro_resume_throw_helper(VM* vm, Value this_val, int arg_count, Value* args);
    Value resume_throw_func = create_native_function((void*)coro_resume_throw_helper, create_string("resumeThrow", 11));
    ((JSFunction*)get_pointer(resume_throw_func))->env = make_pointer(coro);
    
    Value then_func = object_get(await_val, create_string("then", 4));
    if (IS_POINTER(then_func)) {
        Value cb_args[2] = { resume_func, resume_throw_func };
        vm_call_function(vm, then_func, 2, cb_args);
    } else {
        vm_enqueue_microtask(vm, resume_func, await_val);
    }
    
    // Ask VM to suspend this coroutine
    vm_coro_suspend(vm, await_val);
    
    // --- SUSPENDS HERE ---
    
    if (vm->current_coro->is_error) {
        vm->current_coro->is_error = false;
        vm_throw_error(vm, vm->current_coro->result);
        return VAL_UNDEFINED; // vm_throw_error longjmps to catch block
    }
    
    regs[a] = vm->current_coro->result;
    pc = vm->bytecode + frame->ip;
    DISPATCH();
}
do_delete_prop:
do_pow:
do_in:
do_void:
do_inc:
do_dec:
do_new_regex:
do_array_push:
do_array_spread:
do_obj_spread:
do_get_iter: {
    uint8_t a = INST_A(*pc);
    uint8_t b = INST_B(*pc);
    regs[a] = regs[b];
    NEXT();
}
do_yield: {
    printf("FATAL: Unimplemented opcode handler hit at ip=%u\n", frame->ip);
    vm_throw_error(vm, create_string("Unimplemented opcode executed", 29));
    return VAL_UNDEFINED;
}

} // end vm_run

bool vm_snapshot_save(VM* vm, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    
    // Save Arena
    arena_snapshot_save(f);
    
    // Save CompiledProgram
    uint32_t cbc_size;
    extern uint8_t* serialize_program(const struct CompiledProgram* prog, uint32_t* out_size);
    uint8_t* cbc_data = serialize_program(vm->current_prog, &cbc_size);
    fwrite(&cbc_size, sizeof(uint32_t), 1, f);
    fwrite(cbc_data, 1, cbc_size, f);
    free(cbc_data);
    
    fclose(f);
    return true;
}

bool vm_snapshot_load(VM* vm, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    
    // Load Arena
    arena_snapshot_load(f);
    
    // Load CompiledProgram
    uint32_t cbc_size;
    fread(&cbc_size, sizeof(uint32_t), 1, f);
    uint8_t* cbc_data = malloc(cbc_size);
    fread(cbc_data, 1, cbc_size, f);
    
    extern struct CompiledProgram* deserialize_program(const uint8_t* data, uint32_t size);
    if (vm->current_prog) {
        extern void free_compiled_program(struct CompiledProgram* prog);
        free_compiled_program(vm->current_prog);
    }
    vm->current_prog = deserialize_program(cbc_data, cbc_size);
    free(cbc_data);
    
    fclose(f);
    return true;
}

// -----------------------------------------------------------------------------
// VM-Level Hot Module Replacement (HMR)
// -----------------------------------------------------------------------------

static Value vm_hmr_reloadModule(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("reloadModule expects a filepath string", 38)));
        return VAL_UNDEFINED;
    }
    
    JSString* path_str = (JSString*)get_pointer(args[0]);
    FILE* f = fopen(path_str->data, "r");
    if (!f) {
        vm_throw_error(vm, create_error("Error", create_string("Cannot open module file for HMR", 31)));
        return VAL_UNDEFINED;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* source = malloc(size + 1);
    fread(source, 1, size, f);
    source[size] = '\0';
    fclose(f);
    
    extern struct CompiledProgram* compile_source(const char* source);
    struct CompiledProgram* new_prog = compile_source(source);
    free(source);
    
    if (!new_prog) {
        vm_throw_error(vm, create_error("SyntaxError", create_string("Failed to compile module during HMR", 35)));
        return VAL_UNDEFINED;
    }
    
    vm_load_program(vm, new_prog);
    vm_run(vm);
    
    return VAL_TRUE;
}

void vm_register_hmr(VM* vm) {
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    object_set(curica_obj, create_string("reloadModule", 12), 
               create_native_function((void*)vm_hmr_reloadModule, create_string("reloadModule", 12)));
}
