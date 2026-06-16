/**
 * @file builtins.c
 * @brief Core JavaScript Global Built-ins and Node.js Shims.
 * 
 * Implements standard JS global objects (console, Promise, Error, etc.), 
 * CommonJS require() injection, and Native C-to-JS boundaries.
 */
#include "builtins.h"
#include "alloc.h"
#include "vm.h"
#include "compiler.h"
#include "fs_module.h"
#include "net_module.h"
#include "os_module.h"
#include "crypto_module.h"
#include "ts_stripper.h"
#include "dgram_module.h"
#include "zlib_module.h"
#include "scripts.h"
#include <errno.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <math.h>
#include <libc/dlopen/dlfcn.h>
#include "event_loop.h"
#include "compiler.h"



Value js_array_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_concat_method(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_every(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_fill(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_find(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    
    JSArray* arr = (JSArray*)get_pointer(this_val);
    Value cb = args[0];
    if (!IS_POINTER(cb)) return VAL_UNDEFINED;
    BlockHeader* h_cb = (BlockHeader*)((char*)get_pointer(cb) - sizeof(BlockHeader));
    if (h_cb->obj_type != OBJ_FUNCTION) return VAL_UNDEFINED;
    
    for (uint32_t i = 0; i < arr->length; i++) {
        Value el = arr->elements[i];
        Value cb_args[3] = { el, make_integer(i), this_val };
        Value res = vm_call_function(vm, cb, 3, cb_args);
        if (is_truthy(res)) return el;
    }
    
    return VAL_UNDEFINED;
}

Value js_array_find_index(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_flat(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_flat_map(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_for_each(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_from(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_includes(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_index_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_is_array(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_join(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    
    JSArray* arr = (JSArray*)get_pointer(this_val);
    const char* sep = ",";
    if (arg_count >= 1 && IS_POINTER(args[0])) {
        BlockHeader* h_arg = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
        if (h_arg->obj_type == OBJ_STRING) {
            sep = ((JSString*)get_pointer(args[0]))->data;
        }
    }
    
    int total_len = 0;
    int sep_len = strlen(sep);
    char** strs = malloc(arr->length * sizeof(char*));
    int* lens = malloc(arr->length * sizeof(int));
    
    for (uint32_t i = 0; i < arr->length; i++) {
        Value el = arr->elements[i];
        if (el == VAL_UNDEFINED || el == VAL_NULL || el == VAL_EMPTY) {
            strs[i] = "";
            lens[i] = 0;
        } else {
            Value s_val = value_to_string(el);
            JSString* s = (JSString*)get_pointer(s_val);
            strs[i] = s->data;
            lens[i] = s->length;
        }
        total_len += lens[i];
    }
    
    if (arr->length > 1) {
        total_len += sep_len * (arr->length - 1);
    }
    
    char* out = malloc(total_len + 1);
    char* ptr = out;
    for (uint32_t i = 0; i < arr->length; i++) {
        memcpy(ptr, strs[i], lens[i]);
        ptr += lens[i];
        if (i < arr->length - 1) {
            memcpy(ptr, sep, sep_len);
            ptr += sep_len;
        }
    }
    *ptr = '\0';
    
    free(strs);
    free(lens);
    
    Value res = create_string(out, total_len);
    free(out);
    return res;
}

Value js_array_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_pop(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_push_method(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    for (int i = 0; i < arg_count; i++) {
        array_push(this_val, args[i]);
    }
    JSArray* arr = (JSArray*)get_pointer(this_val);
    return make_integer(arr->length);
}

Value js_array_reduce(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_reverse(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_slice(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    
    int len = 0;
    if (h_this->obj_type == OBJ_ARRAY) {
        JSArray* arr = (JSArray*)get_pointer(this_val);
        len = arr->length;
    } else if (h_this->obj_type == OBJ_OBJECT) {
        Value len_val = object_get(this_val, create_string("length", 6));
        if (IS_INTEGER(len_val)) len = get_integer(len_val);
        else if (IS_DOUBLE(len_val)) len = (int)get_double(len_val);
    } else {
        return VAL_UNDEFINED;
    }
    
    int start = 0;
    int end = len;
    
    if (arg_count > 0 && IS_INTEGER(args[0])) {
        start = get_integer(args[0]);
        if (start < 0) start = len + start;
        if (start < 0) start = 0;
        if (start > len) start = len;
    }
    
    if (arg_count > 1 && IS_INTEGER(args[1])) {
        end = get_integer(args[1]);
        if (end < 0) end = len + end;
        if (end < 0) end = 0;
        if (end > len) end = len;
    }
    
    if (start > end) start = end;
    
    int new_len = end - start;
    Value new_arr_val = create_array(new_len > 0 ? new_len : 1);
    JSArray* new_arr = (JSArray*)get_pointer(new_arr_val);
    
    for (int i = 0; i < new_len; i++) {
        if (h_this->obj_type == OBJ_ARRAY) {
            new_arr->elements[i] = ((JSArray*)get_pointer(this_val))->elements[start + i];
        } else {
            char idx_buf[16];
            int idx_len = snprintf(idx_buf, sizeof(idx_buf), "%d", start + i);
            new_arr->elements[i] = object_get(this_val, create_string(idx_buf, idx_len));
        }
    }
    new_arr->length = new_len;
    
    return new_arr_val;
}

Value js_array_some(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1) return VAL_FALSE;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_ARRAY) return VAL_FALSE;
    
    JSArray* arr = (JSArray*)get_pointer(this_val);
    Value cb = args[0];
    if (!IS_POINTER(cb)) return VAL_FALSE;
    BlockHeader* h_cb = (BlockHeader*)((char*)get_pointer(cb) - sizeof(BlockHeader));
    if (h_cb->obj_type != OBJ_FUNCTION) return VAL_FALSE;
    
    for (uint32_t i = 0; i < arr->length; i++) {
        Value el = arr->elements[i];
        Value cb_args[3] = { el, make_integer(i), this_val };
        Value res = vm_call_function(vm, cb, 3, cb_args);
        if (is_truthy(res)) return VAL_TRUE;
    }
    
    return VAL_FALSE;
}

Value js_array_sort(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_splice(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_array_to_string(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_global_is_finite(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_global_is_nan(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

static Value parse_json_value(VM* vm, const char** ptr) {
    while (**ptr == ' ' || **ptr == '\n' || **ptr == '\r' || **ptr == '\t') (*ptr)++;
    if (**ptr == '{') {
        (*ptr)++;
        Value obj = create_object();
        while (**ptr) {
            while (**ptr == ' ' || **ptr == '\n' || **ptr == '\r' || **ptr == '\t') (*ptr)++;
            if (**ptr == '}') { (*ptr)++; break; }
            if (**ptr == ',') { (*ptr)++; continue; }
            if (**ptr == '"') {
                (*ptr)++;
                const char* start = *ptr;
                while (**ptr && **ptr != '"') (*ptr)++;
                Value key = create_string(start, *ptr - start);
                if (**ptr == '"') (*ptr)++;
                while (**ptr == ' ' || **ptr == ':') (*ptr)++;
                Value val = parse_json_value(vm, ptr);
                object_set(obj, key, val);
            } else {
                (*ptr)++;
            }
        }
        return obj;
    }
    if (**ptr == '"') {
        (*ptr)++;
        const char* start = *ptr;
        while (**ptr && **ptr != '"') (*ptr)++;
        Value str = create_string(start, *ptr - start);
        if (**ptr == '"') (*ptr)++;
        return str;
    }
    if (**ptr == 't' && strncmp(*ptr, "true", 4) == 0) { *ptr += 4; return VAL_TRUE; }
    if (**ptr == 'f' && strncmp(*ptr, "false", 5) == 0) { *ptr += 5; return VAL_FALSE; }
    if (**ptr == 'n' && strncmp(*ptr, "null", 4) == 0) { *ptr += 4; return VAL_NULL; }
    if ((**ptr >= '0' && **ptr <= '9') || **ptr == '-') {
        char* end;
        double d = strtod(*ptr, &end);
        *ptr = end;
        return make_double(d);
    }
    return VAL_UNDEFINED;
}

Value js_json_parse(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* str = (JSString*)get_pointer(args[0]);
    const char* ptr = str->data;
    return parse_json_value(vm, &ptr);
}

static void json_stringify_value(VM* vm, Value val, char** buf, int* cap, int* len);

static void json_append_str(char** buf, int* cap, int* len, const char* str) {
    int slen = strlen(str);
    while (*len + slen + 1 >= *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    strcpy(*buf + *len, str);
    *len += slen;
}

static void json_stringify_value(VM* vm, Value val, char** buf, int* cap, int* len) {
    (void)vm;
    if (val == VAL_NULL || val == VAL_UNDEFINED) { json_append_str(buf, cap, len, "null"); return; }
    if (val == VAL_TRUE) { json_append_str(buf, cap, len, "true"); return; }
    if (val == VAL_FALSE) { json_append_str(buf, cap, len, "false"); return; }
    if (IS_INTEGER(val)) {
        char nbuf[32];
        snprintf(nbuf, sizeof(nbuf), "%d", get_integer(val));
        json_append_str(buf, cap, len, nbuf);
        return;
    }
    if (IS_DOUBLE(val)) {
        char nbuf[32];
        snprintf(nbuf, sizeof(nbuf), "%g", get_double(val));
        json_append_str(buf, cap, len, nbuf);
        return;
    }
    if (IS_POINTER(val)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(val) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(val);
            json_append_str(buf, cap, len, "\"");
            json_append_str(buf, cap, len, s->data);
            json_append_str(buf, cap, len, "\"");
            return;
        }
        if (h->obj_type == OBJ_OBJECT) {
            JSObject* obj = (JSObject*)get_pointer(val);
            json_append_str(buf, cap, len, "{");
            int first = 1;
            for (uint32_t i=0; i<obj->capacity; i++) {
                if (obj->properties[i].key) {
                    if (!first) json_append_str(buf, cap, len, ",");
                    first = 0;
                    json_append_str(buf, cap, len, "\"");
                    JSString* k = (JSString*)get_pointer(obj->properties[i].key);
                    json_append_str(buf, cap, len, k->data);
                    json_append_str(buf, cap, len, "\":");
                    json_stringify_value(vm, obj->properties[i].value, buf, cap, len);
                }
            }
            json_append_str(buf, cap, len, "}");
            return;
        }
        if (h->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)get_pointer(val);
            json_append_str(buf, cap, len, "[");
            for (uint32_t i = 0; i < arr->length; i++) {
                if (i > 0) json_append_str(buf, cap, len, ",");
                json_stringify_value(vm, arr->elements[i], buf, cap, len);
            }
            json_append_str(buf, cap, len, "]");
            return;
        }
        json_append_str(buf, cap, len, "null");
    }
}

Value js_json_stringify(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;
    int cap = 256;
    int len = 0;
    char* buf = malloc(cap);
    buf[0] = '\0';
    json_stringify_value(vm, args[0], &buf, &cap, &len);
    Value res = create_string(buf, len);
    free(buf);
    return res;
}

Value js_math_abs(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_acos(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_asin(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_atan(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_atan2(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_cbrt(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_ceil(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_clz32(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_cos(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_exp(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_f16round(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_floor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_hypot(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_imul(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_log(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_log10(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_log2(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_max(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_min(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_pow(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_random(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_round(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_sign(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_sin(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_sqrt(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_tan(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_math_trunc(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_number_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_number_is_finite(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_number_is_integer(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_number_is_nan(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_number_is_safe_integer(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_assign(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_create(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_entries(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_freeze(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_from_entries(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_object_keys(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    Value arr_val = create_array(4);
    if (arg_count < 1 || !IS_POINTER(args[0])) return arr_val;
    
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_OBJECT) return arr_val;
    
    JSObject* obj = (JSObject*)get_pointer(args[0]);
    for (uint32_t i = 0; i < obj->capacity; i++) {
        if (obj->properties[i].key) {
            array_push(arr_val, obj->properties[i].key);
        }
    }
    return arr_val;
}

Value js_object_values(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_parse_float(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_parse_int(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1) return make_double((double)0.0 / 0.0); /* NaN */

    Value input = args[0];
    int radix = 10;
    if (arg_count >= 2 && IS_INTEGER(args[1])) radix = get_integer(args[1]);
    else if (arg_count >= 2 && IS_DOUBLE(args[1])) radix = (int)get_double(args[1]);
    if (radix == 0) radix = 10;

    /* Fast path: already an integer */
    if (IS_INTEGER(input)) return input;
    /* Fast path: double */
    if (IS_DOUBLE(input)) {
        double d = get_double(input);
        if (d != d) return input; /* NaN passthrough */
        return make_integer((int32_t)d);
    }

    /* String path */
    if (!IS_POINTER(input)) return make_double((double)0.0 / 0.0);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(input) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return make_double((double)0.0 / 0.0);

    JSString* str = (JSString*)get_pointer(input);
    const char* p = str->data;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;

    /* Skip optional sign */
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }

    /* Handle 0x prefix for hex */
    if (radix == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    else if (radix == 10 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        radix = 16; p += 2;
    }

    long result = 0;
    int has_digit = 0;
    while (*p) {
        int digit;
        if (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;
        if (digit >= radix) break;
        result = result * radix + digit;
        has_digit = 1;
        p++;
    }

    if (!has_digit) return make_double((double)0.0 / 0.0); /* NaN */
    return make_integer((int32_t)(sign * result));
}

Value js_promise_with_resolvers(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_range_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_reference_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_regexp_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_regexp_exec(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_regexp_test(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_s(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_for_each(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_is_disjoint_from(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_is_subset_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_is_superset_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_symmetric_difference(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_set_values(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_char_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_char_code_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_concat_method(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_ends_with(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_from_char_code(VM* vm __attribute__((unused)), Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count == 0) return create_string("", 0);
    char* buf = malloc(arg_count + 1);
    for (int i = 0; i < arg_count; i++) {
        if (IS_INTEGER(args[i])) {
            buf[i] = (char)get_integer(args[i]);
        } else if (IS_DOUBLE(args[i])) {
            buf[i] = (char)get_double(args[i]);
        } else {
            buf[i] = '\0';
        }
    }
    buf[arg_count] = '\0';
    Value ret = create_string(buf, arg_count);
    free(buf);
    return ret;
}

Value js_string_includes(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_index_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return make_integer(-1);
    
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h_arg = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_STRING || h_arg->obj_type != OBJ_STRING) return make_integer(-1);
    
    JSString* str = (JSString*)get_pointer(this_val);
    JSString* search = (JSString*)get_pointer(args[0]);
    
    if (search->length == 0) return make_integer(0);
    if (search->length > str->length) return make_integer(-1);
    
    char* found = strstr(str->data, search->data);
    if (found) {
        return make_integer(found - str->data);
    }
    
    return make_integer(-1);
}

Value js_string_last_index_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_pad_end(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_pad_start(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_repeat(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    
    int count = 0;
    if (arg_count > 0) {
        if (IS_INTEGER(args[0])) count = get_integer(args[0]);
        else if (IS_DOUBLE(args[0])) count = (int)get_double(args[0]);
    }
    
    if (count < 0) count = 0;
    if (count == 0) return create_string("", 0);
    
    JSString* str = (JSString*)get_pointer(this_val);
    if (str->length == 0) return this_val;
    
    size_t total_len = str->length * count;
    char* buf = malloc(total_len + 1);
    for (int i = 0; i < count; i++) {
        memcpy(buf + (i * str->length), str->data, str->length);
    }
    buf[total_len] = '\0';
    
    Value res = create_string(buf, total_len);
    free(buf);
    return res;
}

Value js_string_replace(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_replace_all(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_slice(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    
    JSString* str = (JSString*)get_pointer(this_val);
    int start = 0;
    int end = str->length;
    
    if (arg_count >= 1) {
        if (IS_INTEGER(args[0])) start = get_integer(args[0]);
        else if (IS_DOUBLE(args[0])) start = (int)get_double(args[0]);
    }
    if (arg_count >= 2) {
        if (IS_INTEGER(args[1])) end = get_integer(args[1]);
        else if (IS_DOUBLE(args[1])) end = (int)get_double(args[1]);
    }
    
    if (start < 0) start = str->length + start;
    if (end < 0) end = str->length + end;
    
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > (int)str->length) start = str->length;
    if (end > (int)str->length) end = str->length;
    
    if (start >= end) return create_string("", 0);
    
    return create_string(str->data + start, end - start);
}

Value js_string_split(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h_arg = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_STRING || h_arg->obj_type != OBJ_STRING) return VAL_UNDEFINED;

    JSString* str = (JSString*)get_pointer(this_val);
    JSString* delim = (JSString*)get_pointer(args[0]);

    Value arr_val = create_array(16);
    vm_push_root(vm, arr_val);
    JSArray* arr = (JSArray*)get_pointer(arr_val);

    if (delim->length == 0) {
        // Not supporting empty string split yet
        arr->elements[0] = this_val;
        arr->length = 1;
        vm_pop_root(vm);
        return arr_val;
    }

    uint32_t start = 0;
    uint32_t i = 0;
    uint32_t elem_idx = 0;

    while (i <= str->length) {
        int match = 0;
        if (i <= str->length - delim->length && delim->length > 0) {
            if (memcmp(str->data + i, delim->data, delim->length) == 0) {
                match = 1;
            }
        }
        
        if (match || i == str->length) {
            uint32_t len = i - start;
            Value part = create_string_from_chars(str->data + start, len);
            
            // Re-fetch arr since create_string_from_chars might have triggered GC
            arr = (JSArray*)get_pointer(arr_val);
            
            if (elem_idx >= arr->capacity) {
                uint32_t new_cap = arr->capacity * 2;
                Value* new_elems = arena_alloc(OBJ_ARRAY_DATA, new_cap * sizeof(Value));
                // Re-fetch arr again just in case arena_alloc triggered GC
                arr = (JSArray*)get_pointer(arr_val);
                
                memcpy(new_elems, arr->elements, arr->capacity * sizeof(Value));
                for(uint32_t k = arr->capacity; k < new_cap; k++) new_elems[k] = VAL_EMPTY;
                arr->elements = new_elems;
                arr->capacity = new_cap;
            }
            arr->elements[elem_idx++] = part;
            start = i + delim->length;
            if (match) {
                i += delim->length;
                if (i == str->length) {
                    // trailing delimiter produces empty string
                    if (elem_idx >= arr->capacity) {
                        uint32_t new_cap = arr->capacity * 2;
                        Value* new_elems = arena_alloc(OBJ_ARRAY_DATA, new_cap * sizeof(Value));
                        arr = (JSArray*)get_pointer(arr_val);
                        memcpy(new_elems, arr->elements, arr->capacity * sizeof(Value));
                        for(uint32_t k = arr->capacity; k < new_cap; k++) new_elems[k] = VAL_EMPTY;
                        arr->elements = new_elems;
                        arr->capacity = new_cap;
                    }
                    arr->elements[elem_idx++] = create_string("", 0);
                    break;
                }
            } else {
                break;
            }
        } else {
            i++;
        }
    }
    
    arr->length = elem_idx;
    return arr_val;
}

Value js_string_starts_with(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_substring(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    
    BlockHeader* h_this = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h_this->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    
    JSString* str = (JSString*)get_pointer(this_val);
    if (arg_count == 0) return this_val;
    
    int32_t start = 0;
    if (IS_INTEGER(args[0])) start = get_integer(args[0]);
    else if (IS_DOUBLE(args[0])) start = (int32_t)get_double(args[0]);
    
    int32_t end = str->length;
    if (arg_count > 1) {
        if (IS_INTEGER(args[1])) end = get_integer(args[1]);
        else if (IS_DOUBLE(args[1])) end = (int32_t)get_double(args[1]);
    }
    
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > (int32_t)str->length) start = str->length;
    if (end > (int32_t)str->length) end = str->length;
    
    if (start > end) {
        int32_t temp = start;
        start = end;
        end = temp;
    }
    
    int32_t len = end - start;
    if (len <= 0) return create_string("", 0);
    
    return create_string(str->data + start, len);
}

Value js_string_to_lower(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_to_string(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_to_upper(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_trim(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_trim_end(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_string_trim_start(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_suppressed_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_symbol_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_syntax_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}

Value js_type_error_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return VAL_UNDEFINED;
}


Value js_promise_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    Value promise = create_promise(0 /* PENDING */, VAL_UNDEFINED);
    
    extern Value create_bound_native_function(void* native_ptr, Value name, Value env);
    extern Value js_promise_resolve_func(VM* vm, Value this_val, int arg_count, Value* args);
    extern Value js_promise_reject_func(VM* vm, Value this_val, int arg_count, Value* args);
    
    Value resolve_fn = create_bound_native_function((void*)js_promise_resolve_func, create_string("resolve", 7), promise);
    Value reject_fn = create_bound_native_function((void*)js_promise_reject_func, create_string("reject", 6), promise);
    
    Value cb_args[2] = { resolve_fn, reject_fn };
    vm_call_function(vm, args[0], 2, cb_args);
    
    return promise;
}

Value js_promise_try(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        Value err = create_error("TypeError", create_string("Promise.try requires a function argument", 38));
        return create_promise(2 /* REJECTED */, err);
    }
    
    Value callback = args[0];
    
    VMErrorHandler handler;
    handler.active = 1;
    handler.error = VAL_UNDEFINED;
    handler.saved_frame_count = vm->frame_count;
    handler.saved_reg_count = vm->reg_count;
    handler.catch_ip = 0;
    handler.prev = vm->error_handler;
    vm->error_handler = &handler;
    
    if (setjmp(handler.jmp) != 0) {
        VMErrorHandler* active_h = vm->error_handler;
        Value err = active_h->error;
        vm->error_handler = active_h->prev;
        return create_promise(2 /* REJECTED */, err);
    }
    
    Value res = vm_call_function(vm, callback, 0, NULL);
    
    vm->error_handler = handler.prev;
    
    if (IS_POINTER(res)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(res) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_PROMISE) {
            return res;
        }
    }
    
    return create_promise(1 /* FULFILLED */, res);
}

Value js_promise_then(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_PROMISE) return VAL_UNDEFINED;
    
    JSPromise* prom = (JSPromise*)get_pointer(this_val);
    Value on_fulfilled = arg_count > 0 ? args[0] : VAL_UNDEFINED;
    Value on_rejected  = arg_count > 1 ? args[1] : VAL_UNDEFINED;
    
    if (prom->state == 1) {
        if (IS_POINTER(on_fulfilled)) vm_enqueue_microtask(vm, on_fulfilled, prom->result);
    } else if (prom->state == 2) {
        if (IS_POINTER(on_rejected)) vm_enqueue_microtask(vm, on_rejected, prom->result);
    } else {
        if (IS_NULL(prom->then_callbacks)) {
            prom->then_callbacks = create_array(2);
        }
        Value cb_obj = create_object();
        object_set(cb_obj, create_string("resolve", 7), on_fulfilled);
        object_set(cb_obj, create_string("reject", 6), on_rejected);
        
        JSArray* arr = (JSArray*)get_pointer(prom->then_callbacks);
        if (arr->length >= arr->capacity) {
            uint32_t new_cap = arr->capacity * 2;
            Value* new_elems = arena_alloc(OBJ_ARRAY_DATA, new_cap * sizeof(Value));
            memcpy(new_elems, arr->elements, arr->length * sizeof(Value));
            arr->capacity = new_cap;
            arr->elements = new_elems;
        }
        arr->elements[arr->length++] = cb_obj;
    }
    return this_val;
}

Value js_promise_catch(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count == 0) return js_promise_then(vm, this_val, 0, NULL);
    Value then_args[2] = { VAL_UNDEFINED, args[0] };
    return js_promise_then(vm, this_val, 2, then_args);
}

Value js_promise_resolve_func(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_PROMISE) return VAL_UNDEFINED;
    
    JSPromise* prom = (JSPromise*)get_pointer(this_val);
    if (prom->state != 0) return VAL_UNDEFINED;
    
    prom->state = 1;
    prom->result = arg_count > 0 ? args[0] : VAL_UNDEFINED;
    
    if (!IS_NULL(prom->then_callbacks)) {
        JSArray* arr = (JSArray*)get_pointer(prom->then_callbacks);
        Value resolve_key = create_string("resolve", 7);
        for (uint32_t i = 0; i < arr->length; i++) {
            Value cb_obj = arr->elements[i];
            Value on_fulfilled = object_get(cb_obj, resolve_key);
            if (IS_POINTER(on_fulfilled)) {
                vm_enqueue_microtask(vm, on_fulfilled, prom->result);
            }
        }
        prom->then_callbacks = VAL_NULL;
    }
    return VAL_UNDEFINED;
}

Value js_promise_reject_func(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_PROMISE) return VAL_UNDEFINED;
    
    JSPromise* prom = (JSPromise*)get_pointer(this_val);
    if (prom->state != 0) return VAL_UNDEFINED;
    
    prom->state = 2;
    prom->result = arg_count > 0 ? args[0] : VAL_UNDEFINED;
    
    if (!IS_NULL(prom->then_callbacks)) {
        JSArray* arr = (JSArray*)get_pointer(prom->then_callbacks);
        Value reject_key = create_string("reject", 6);
        for (uint32_t i = 0; i < arr->length; i++) {
            Value cb_obj = arr->elements[i];
            Value on_rejected = object_get(cb_obj, reject_key);
            if (IS_POINTER(on_rejected)) {
                vm_enqueue_microtask(vm, on_rejected, prom->result);
            }
        }
        prom->then_callbacks = VAL_NULL;
    }
    return VAL_UNDEFINED;
}

static void timeout_cb(void* arg) {
    Value* cb_val_ptr = (Value*)arg;
    Value cb = *cb_val_ptr;
    free(cb_val_ptr);
    
    // Execute the timeout callback
    Value cb_args[0];
    vm_call_function(g_current_vm, cb, 0, cb_args);
    vm_drain_next_tick(g_current_vm);
    vm_drain_microtasks(g_current_vm);
}

Value js_global_set_timeout(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    uint64_t delay = 0;
    if (arg_count >= 2) {
        if (IS_INTEGER(args[1])) delay = get_integer(args[1]);
        else if (IS_DOUBLE(args[1])) delay = (uint64_t)get_double(args[1]);
    }
    
    Value* cb_val = malloc(sizeof(Value));
    *cb_val = args[0];
    
    el_set_timeout(g_event_loop, delay, timeout_cb, cb_val, VAL_UNDEFINED);
    
    return VAL_UNDEFINED;
}

static void immediate_cb(void* arg) {
    Value* cb_val_ptr = (Value*)arg;
    Value cb = *cb_val_ptr;
    free(cb_val_ptr);
    
    Value cb_args[0];
    vm_call_function(g_current_vm, cb, 0, cb_args);
    vm_drain_next_tick(g_current_vm);
    vm_drain_microtasks(g_current_vm);
}

Value js_global_set_immediate(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    Value* cb_val = malloc(sizeof(Value));
    *cb_val = args[0];
    
    CheckHandle* handle = el_set_immediate(g_event_loop, immediate_cb, cb_val, VAL_UNDEFINED);
    
    // Return a mock object or pointer to handle for clearImmediate
    // For now we'll just return a number representation of the pointer
    return make_integer((intptr_t)handle);
}

Value js_global_clear_immediate(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    
    CheckHandle* handle = (CheckHandle*)(intptr_t)get_integer(args[0]);
    el_clear_immediate(g_event_loop, handle);
    return VAL_UNDEFINED;
}

Value js_process_next_tick(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    vm_enqueue_next_tick(vm, args[0], VAL_UNDEFINED);
    return VAL_UNDEFINED;
}

Value js_queue_microtask(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    vm_enqueue_microtask(vm, args[0], VAL_UNDEFINED);
    return VAL_UNDEFINED;
}

// ── Buffer Implementation ──────────────────────────────────────────────────

Value js_buffer_alloc_unsafe(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;
    uint32_t size = 0;
    if (IS_INTEGER(args[0])) size = (uint32_t)get_integer(args[0]);
    else if (IS_DOUBLE(args[0])) size = (uint32_t)get_double(args[0]);
    return create_buffer(size, false);
}

Value js_buffer_alloc(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;
    uint32_t size = 0;
    if (IS_INTEGER(args[0])) size = (uint32_t)get_integer(args[0]);
    else if (IS_DOUBLE(args[0])) size = (uint32_t)get_double(args[0]);
    return create_buffer(size, true);
}

// Buffer.from(string[, encoding]) or Buffer.from(array)
Value js_buffer_from(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;

    void* ptr = get_pointer(args[0]);
    BlockHeader* hdr = (BlockHeader*)((char*)ptr - sizeof(BlockHeader));

    if (hdr->obj_type == OBJ_STRING) {
        JSString* str = (JSString*)ptr;
        // Extract optional encoding argument
        const char* encoding = NULL;
        if (arg_count >= 2 && IS_POINTER(args[1])) {
            BlockHeader* eh = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
            if (eh->obj_type == OBJ_STRING) {
                encoding = ((JSString*)get_pointer(args[1]))->data;
            }
        }
        return create_buffer_from_string(str->data, str->length, encoding);
    }
    if (hdr->obj_type == OBJ_ARRAY) {
        JSArray* arr = (JSArray*)ptr;
        Value buf_val = create_buffer(arr->length, false);
        JSBuffer* buf = (JSBuffer*)get_pointer(buf_val);
        for (uint32_t i = 0; i < arr->length; i++) {
            Value el = arr->elements[i];
            if (IS_INTEGER(el)) buf->data[i] = (uint8_t)get_integer(el);
            else if (IS_DOUBLE(el)) buf->data[i] = (uint8_t)(int)get_double(el);
            else buf->data[i] = 0;
        }
        return buf_val;
    }
    if (hdr->obj_type == OBJ_BUFFER) {
        // Copy from another Buffer
        JSBuffer* src = (JSBuffer*)ptr;
        Value buf_val = create_buffer(src->length, false);
        JSBuffer* dst = (JSBuffer*)get_pointer(buf_val);
        if (src->length > 0) memcpy(dst->data, src->data, src->length);
        return buf_val;
    }
    return VAL_UNDEFINED;
}

// Buffer.isBuffer(obj) -> boolean
Value js_buffer_is_buffer(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return make_boolean(false);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    return make_boolean(h->obj_type == OBJ_BUFFER);
}

// Buffer.concat(list[, totalLength]) -> Buffer
Value js_buffer_concat(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return create_buffer(0, true);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return create_buffer(0, true);
    JSArray* list = (JSArray*)get_pointer(args[0]);

    uint32_t total = 0;
    for (uint32_t i = 0; i < list->length; i++) {
        Value item = list->elements[i];
        if (IS_POINTER(item)) {
            BlockHeader* ih = (BlockHeader*)((char*)get_pointer(item) - sizeof(BlockHeader));
            if (ih->obj_type == OBJ_BUFFER) {
                total += ((JSBuffer*)get_pointer(item))->length;
            }
        }
    }
    if (arg_count >= 2) {
        if (IS_INTEGER(args[1])) total = (uint32_t)get_integer(args[1]);
        else if (IS_DOUBLE(args[1])) total = (uint32_t)get_double(args[1]);
    }

    Value result = create_buffer(total, false);
    JSBuffer* dst = (JSBuffer*)get_pointer(result);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < list->length && offset < total; i++) {
        Value item = list->elements[i];
        if (!IS_POINTER(item)) continue;
        BlockHeader* ih = (BlockHeader*)((char*)get_pointer(item) - sizeof(BlockHeader));
        if (ih->obj_type != OBJ_BUFFER) continue;
        JSBuffer* src = (JSBuffer*)get_pointer(item);
        uint32_t to_copy = src->length;
        if (offset + to_copy > total) to_copy = total - offset;
        memcpy(dst->data + offset, src->data, to_copy);
        offset += to_copy;
    }
    return result;
}

// buf.toString([encoding[, start[, end]]])
Value js_buffer_prototype_to_string(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* hdr = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (hdr->obj_type != OBJ_BUFFER) return VAL_UNDEFINED;
    JSBuffer* buf = (JSBuffer*)get_pointer(this_val);

    uint32_t start = 0, end = buf->length;
    if (arg_count >= 2) {
        if (IS_INTEGER(args[1])) start = (uint32_t)get_integer(args[1]);
        else if (IS_DOUBLE(args[1])) start = (uint32_t)get_double(args[1]);
    }
    if (arg_count >= 3) {
        if (IS_INTEGER(args[2])) end   = (uint32_t)get_integer(args[2]);
        else if (IS_DOUBLE(args[2])) end   = (uint32_t)get_double(args[2]);
    }
    if (start > buf->length) start = buf->length;
    if (end   > buf->length) end   = buf->length;
    if (start > end) start = end;

    // Check encoding
    const char* encoding = NULL;
    if (arg_count >= 1 && IS_POINTER(args[0])) {
        BlockHeader* eh = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
        if (eh->obj_type == OBJ_STRING)
            encoding = ((JSString*)get_pointer(args[0]))->data;
    }

    if (encoding && strcmp(encoding, "hex") == 0) {
        uint32_t len = (end - start) * 2;
        char* hex = (char*)malloc(len + 1);
        for (uint32_t i = start; i < end; i++) {
            snprintf(hex + (i - start) * 2, 3, "%02x", buf->data[i]);
        }
        Value s = create_string(hex, (int)len);
        free(hex);
        return s;
    }
    // Default: UTF-8
    return create_string_from_chars((const char*)(buf->data + start), (int)(end - start));
}

// process.cwd()
Value js_process_cwd(VM* vm, Value this_val, int arg_count, Value* args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        return create_string(cwd, strlen(cwd));
    }
    return create_string("", 0);
}

// process.dlopen(module, filename)
Value js_process_dlopen(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (!(vm)->allow_ffi) {
        vm_throw_error(vm, create_error("PermissionError", create_string("Requires --allow-ffi access", 27)));
        return VAL_UNDEFINED;
    }
    if (arg_count < 2) return VAL_UNDEFINED;
    
    Value module_obj = args[0];
    Value filename_val = args[1];
    
    if (!IS_POINTER(filename_val)) return VAL_UNDEFINED;
    
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(filename_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    
    JSString* filename_str = (JSString*)get_pointer(filename_val);
    
    char resolved_path[1024];
    extern const char* vfs_resolve_path(const char* original_path, char* resolved_buffer, size_t buffer_size);
    vfs_resolve_path(filename_str->data, resolved_path, sizeof(resolved_path));

    const char* target_path = resolved_path;
    char temp_path[1024];

    if (strncmp(resolved_path, "/zip/", 5) == 0) {
        mkdir("/tmp/curica_addons", 0755);
        const char* basename = strrchr(resolved_path, '/');
        basename = basename ? basename + 1 : resolved_path;
        snprintf(temp_path, sizeof(temp_path), "/tmp/curica_addons/%s", basename);
        
        FILE* in = fopen(resolved_path, "rb");
        if (in) {
            FILE* out = fopen(temp_path, "wb");
            if (out) {
                char buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                    fwrite(buf, 1, n, out);
                }
                fclose(out);
            }
            fclose(in);
            target_path = temp_path;
        }
    }
    
    // Load the shared library
    void* handle = cosmo_dlopen(target_path, RTLD_LAZY);
    if (!handle) {
        printf("dlopen failed for %s: %s\n", target_path, cosmo_dlerror());
        return VAL_UNDEFINED;
    }
    
    // Lookup module registration function
    typedef Value (*NapiRegisterFunc)(VM* env, Value exports);
    NapiRegisterFunc reg_func = (NapiRegisterFunc)cosmo_dlsym(handle, "napi_register_module_v1");
    
    if (!reg_func) {
        printf("Symbol napi_register_module_v1 not found in %s\n", filename_str->data);
        return VAL_UNDEFINED;
    }
    
    // Initialize the module exports
    Value exports_key = create_string("exports", 7);
    Value exports_obj = object_get(module_obj, exports_key);
    
    // Invoke N-API entry point
    Value new_exports = reg_func(vm, exports_obj);
    
    return new_exports;
}
static bool is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) return true;
    return false;
}

static bool is_dir(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return true;
    return false;
}

static bool resolve_as_file(const char* path, char* resolved) {
    if (is_file(path)) { strcpy(resolved, path); return true; }
    
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s.js", path);
    if (is_file(tmp)) { strcpy(resolved, tmp); return true; }
    
    snprintf(tmp, sizeof(tmp), "%s.json", path);
    if (is_file(tmp)) { strcpy(resolved, tmp); return true; }
    
    snprintf(tmp, sizeof(tmp), "%s.node", path);
    if (is_file(tmp)) { strcpy(resolved, tmp); return true; }
    return false;
}

static bool resolve_as_directory(const char* path, char* resolved) {
    char pkg_path[1024];
    snprintf(pkg_path, sizeof(pkg_path), "%s/package.json", path);
    if (is_file(pkg_path)) {
        FILE* f = fopen(pkg_path, "r");
        if (f) {
            char buf[4096];
            size_t n = fread(buf, 1, sizeof(buf)-1, f);
            buf[n] = '\0';
            fclose(f);
            
            // extremely naive json parse for "main": "..."
            char* main_str = strstr(buf, "\"main\"");
            if (main_str) {
                main_str = strchr(main_str, ':');
                if (main_str) {
                    main_str = strchr(main_str, '"');
                    if (main_str) {
                        main_str++;
                        char* end = strchr(main_str, '"');
                        if (end) {
                            *end = '\0';
                            char main_path[1024];
                            snprintf(main_path, sizeof(main_path), "%s/%s", path, main_str);
                            if (resolve_as_file(main_path, resolved)) return true;
                            char index_path[1024];
                            snprintf(index_path, sizeof(index_path), "%s/%s/index", path, main_str);
                            if (resolve_as_file(index_path, resolved)) return true;
                        }
                    }
                }
            }
        }
    }
    
    char index_path[1024];
    snprintf(index_path, sizeof(index_path), "%s/index", path);
    if (resolve_as_file(index_path, resolved)) return true;
    
    return false;
}

static bool resolve_module(const char* request, const char* dirname, char* resolved) {
    if (request[0] == '/' || (request[0] == '.' && (request[1] == '/' || (request[1] == '.' && request[2] == '/')))) {
        char target[1024];
        if (request[0] == '/') snprintf(target, sizeof(target), "%s", request);
        else snprintf(target, sizeof(target), "%s/%s", dirname, request);
        
        if (resolve_as_file(target, resolved)) return true;
        if (is_dir(target) && resolve_as_directory(target, resolved)) return true;
        return false;
    }
    
    // Node modules traversal
    char cur_dir[1024];
    snprintf(cur_dir, sizeof(cur_dir), "%s", dirname);
    while (strlen(cur_dir) > 0) {
        if (strcmp(cur_dir + (strlen(cur_dir) > 12 ? strlen(cur_dir) - 12 : 0), "node_modules") != 0) {
            char target[2048];
            snprintf(target, sizeof(target), "%s/node_modules/%s", cur_dir, request);
            if (resolve_as_file(target, resolved)) return true;
            if (is_dir(target) && resolve_as_directory(target, resolved)) return true;
        }
        
        char* last_slash = strrchr(cur_dir, '/');
        if (!last_slash) break;
        if (last_slash == cur_dir) { cur_dir[1] = '\0'; } // root
        else { *last_slash = '\0'; }
        if (strcmp(cur_dir, "/") == 0) break;
    }
    return false;
}

/**
 * @brief CommonJS and ESM Module Resolver (`require` polyfill)
 *
 * Implements Node.js compliant module resolution strategies:
 * - Scans `node_modules`, standard extensions (`.js`, `.json`, `.node`), and core C modules.
 * - Parses and caches active module environments within `vm->module_cache`.
 * - **ESM Interoperability**: For `.mjs` extensions, it transpiles the evaluation context
 *   into an `async function __wrapper__`, forcing a Coroutine instantiation upon invocation.
 *   This safely returns a Promise to the caller (e.g., transpiled `import` statements
 *   using Top-Level Await) holding the populated `exports` cache.
 */
Value js_process_require(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;
    
    if (!IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    
    JSString* dirname_str = NULL;
    if (arg_count >= 2 && IS_POINTER(args[1])) {
        BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h2->obj_type == OBJ_STRING) {
            dirname_str = (JSString*)get_pointer(args[1]);
        }
    }
    
    // Default dirname to "." if not provided (useful for global test runner)
    if (!dirname_str) {
        dirname_str = (JSString*)get_pointer(create_string(".", 1));
    }

    // --- Core built-in module interception ---
    // Check the requested path against known built-in module names before
    // attempting any filesystem resolution. This mirrors Node.js behaviour.
    if (strcmp(path_str->data, "fs") == 0) {
        return build_fs_module(vm);
    }
    if (strcmp(path_str->data, "net") == 0) {
        return build_net_module(vm);
    }
    if (strcmp(path_str->data, "os") == 0) {
        return build_os_module(vm);
    }
    if (strcmp(path_str->data, "crypto") == 0) {
        return build_crypto_module(vm);
    }
    if (strcmp(path_str->data, "_dgram") == 0) {
        return build_dgram_module(vm);
    }
    if (strcmp(path_str->data, "_zlib") == 0) {
        return build_zlib_module(vm);
    }
    
    unsigned char* script_data = NULL;
    unsigned int script_len = 0;

    if (strcmp(path_str->data, "path") == 0) {
        script_data = src_js_path_js;
        script_len = src_js_path_js_len;
    } else if (strcmp(path_str->data, "url") == 0) {
        script_data = src_js_url_js;
        script_len = src_js_url_js_len;
    } else if (strcmp(path_str->data, "http") == 0) {
        script_data = src_js_http_js;
        script_len = src_js_http_js_len;
    } else if (strcmp(path_str->data, "stream") == 0) {
        script_data = src_js_stream_js;
        script_len = src_js_stream_js_len;
    } else if (strcmp(path_str->data, "events") == 0) {
        script_data = src_js_events_js;
        script_len = src_js_events_js_len;
    } else if (strcmp(path_str->data, "child_process") == 0) {
        script_data = src_js_child_process_js;
        script_len = src_js_child_process_js_len;
    } else if (strcmp(path_str->data, "gui") == 0) {
        script_data = src_js_webview_js;
        script_len = src_js_webview_js_len;
    } else if (strcmp(path_str->data, "tty") == 0) {
        script_data = src_js_tty_js;
        script_len = src_js_tty_js_len;
    } else if (strcmp(path_str->data, "readline") == 0) {
        script_data = src_js_readline_js;
        script_len = src_js_readline_js_len;
    } else if (strcmp(path_str->data, "dgram") == 0) {
        script_data = src_js_dgram_js;
        script_len = src_js_dgram_js_len;
    } else if (strcmp(path_str->data, "zlib") == 0) {
        script_data = src_js_zlib_js;
        script_len = src_js_zlib_js_len;
    }

    if (script_data) {
        Value cache_key = create_string(path_str->data, strlen(path_str->data));
        Value cached_module = object_get(vm->module_cache, cache_key);
        if (cached_module != VAL_UNDEFINED) {
            return object_get(cached_module, create_string("exports", 7));
        }

        const char* wrapper_head = "function __wrapper__(exports, _req, module, __filename, __dirname) {\n"
                                   "  const require = function(path) { return _req(path, __dirname); };\n";
        const char* wrapper_tail = "\nreturn module.exports;\n}";
        int src_len = strlen(wrapper_head) + script_len + strlen(wrapper_tail) + 1;
        char* src = malloc(src_len);
        strcpy(src, wrapper_head);
        memcpy(src + strlen(wrapper_head), script_data, script_len);
        src[strlen(wrapper_head) + script_len] = '\0';
        strcat(src, wrapper_tail);

        CompiledProgram* prog = compile_source(src);
        free(src);
        if (!prog || prog->function_count < 2) return VAL_UNDEFINED;

        Value module_obj = create_object();
        vm_push_root(vm, module_obj);
        
        Value exports_obj = create_object();
        vm_push_root(vm, exports_obj);
        
        Value exports_str = create_string("exports", 7);
        vm_push_root(vm, exports_str);
        
        object_set(module_obj, exports_str, exports_obj);
        object_set(vm->module_cache, cache_key, module_obj);

        CompilerFuncInfo* f_meta = (CompilerFuncInfo*)prog->functions;
        Value wrapper_fn = create_function(
            prog,
            f_meta[1].bytecode_offset,
            f_meta[1].register_count,
            f_meta[1].param_count,
            f_meta[1].is_async,
            VAL_NULL,
            create_string("__wrapper__", 11)
        );
        vm_push_root(vm, wrapper_fn);

        Value require_str = create_string("require", 7);
        vm_push_root(vm, require_str);
        Value require_fn = create_native_function((void*)js_process_require, require_str);
        vm_push_root(vm, require_fn);
        
        Value dir_val = create_string(".", 1);
        vm_push_root(vm, dir_val);
        Value file_val = create_string(path_str->data, strlen(path_str->data));
        vm_push_root(vm, file_val);
        
        Value args_val[5] = { exports_obj, require_fn, module_obj, file_val, dir_val };
        Value final_exports = vm_call_function(vm, wrapper_fn, 5, args_val);
        vm_push_root(vm, final_exports);
        
        object_set(module_obj, exports_str, final_exports);
        
        for (int i = 0; i < 9; i++) {
            vm_pop_root(vm);
        }
        
        return final_exports;
    }

    char resolved_path[1024];
    if (!resolve_module(path_str->data, dirname_str->data, resolved_path)) {
        Value err = create_system_error(vm, ENOENT, "open", path_str->data);
        vm_throw_error(vm, err);
        return VAL_UNDEFINED;
    }
    
    Value cache_key = create_string(resolved_path, strlen(resolved_path));
    Value cached_module = object_get(vm->module_cache, cache_key);
    if (cached_module != VAL_UNDEFINED) {
        Value exports_key = create_string("exports", 7);
        return object_get(cached_module, exports_key);
    }
    
    int path_len = strlen(resolved_path);
    if (path_len > 5 && strcmp(resolved_path + path_len - 5, ".node") == 0) {
        Value module_obj = create_object();
        Value exports_obj = create_object();
        Value exports_key = create_string("exports", 7);
        object_set(module_obj, exports_key, exports_obj);
        
        object_set(vm->module_cache, cache_key, module_obj);
        
        Value dlopen_args[2] = { module_obj, cache_key };
        extern Value js_process_dlopen(VM* vm, Value this_val, int arg_count, Value* args);
        js_process_dlopen(vm, VAL_UNDEFINED, 2, dlopen_args);
        
        return object_get(module_obj, exports_key);
    }
    
    FILE* f = fopen(resolved_path, "rb");
    if (!f) {
        Value err = create_system_error(vm, ENOENT, "open", resolved_path);
        vm_throw_error(vm, err);
        return VAL_UNDEFINED;
    }
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* src = malloc(fsize + 1024);
    
    bool is_esm = false;
    if (path_len > 4 && strcmp(resolved_path + path_len - 4, ".mjs") == 0) {
        is_esm = true;
    } else if (path_len > 3 && strcmp(resolved_path + path_len - 3, ".ts") == 0) {
        is_esm = true; // TS implies ESM usually in modern contexts
    } else if (path_len > 4 && strcmp(resolved_path + path_len - 4, ".mts") == 0) {
        is_esm = true;
    }
    
    const char* wrapper_head;
    if (is_esm) {
        wrapper_head = "async function __wrapper__(exports, _req, module, __filename, __dirname) {\n"
                       "  const require = function(path) { return _req(path, __dirname); };\n";
    } else {
        wrapper_head = "function __wrapper__(exports, _req, module, __filename, __dirname) {\n"
                       "  const require = function(path) { return _req(path, __dirname); };\n";
    }
    
    strcpy(src, wrapper_head);
    long head_len = strlen(wrapper_head);
    fread(src + head_len, 1, fsize, f);
    src[head_len + fsize] = '\0';
    
    const char* wrapper_tail = "\n  return exports;\n}";
    strcat(src, wrapper_tail);
    fclose(f);
    
    // Type strip if it's a TypeScript file
    if ((path_len > 3 && strcmp(resolved_path + path_len - 3, ".ts") == 0) ||
        (path_len > 4 && strcmp(resolved_path + path_len - 4, ".mts") == 0)) {
        strip_typescript_types(src + head_len);
    }
    
    CompiledProgram* prog = compile_source(src);
    free(src);
    if (!prog) {
        printf("Syntax Error in module '%s'\n", resolved_path);
        return VAL_UNDEFINED;
    }
    
    CompilerFuncInfo* f_meta = (CompilerFuncInfo*)prog->functions;
    if (prog->function_count < 2) {
        printf("Module wrapper compilation failed for '%s'\n", resolved_path);
        return VAL_UNDEFINED;
    }
    Value wrapper_name = create_string("__wrapper__", 11);
    Value wrapper_closure = create_function(
        prog,
        f_meta[1].bytecode_offset,
        f_meta[1].register_count,
        f_meta[1].param_count,
        f_meta[1].is_async,
        VAL_NULL,
        wrapper_name
    );
    
    Value module_obj = create_object();
    Value exports_obj = create_object();
    Value exports_key = create_string("exports", 7);
    object_set(module_obj, exports_key, exports_obj);
    
    object_set(vm->module_cache, cache_key, module_obj);
    
    char dirname_buf[1024];
    strcpy(dirname_buf, resolved_path);
    char* last_slash = strrchr(dirname_buf, '/');
    if (last_slash) *last_slash = '\0';
    else strcpy(dirname_buf, ".");
    Value dirname_val = create_string(dirname_buf, strlen(dirname_buf));
    
    Value process_key = create_string("process", 7);
    Value process_obj = object_get(vm->global_obj, process_key);
    Value req_key = create_string("__require", 9);
    Value process_require = object_get(process_obj, req_key);
    
    Value wrapper_args[5] = {
        exports_obj,
        process_require,
        module_obj,
        cache_key,
        dirname_val
    };
    
    extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
    Value ret = vm_call_function(vm, wrapper_closure, 5, wrapper_args);
    
    return ret;
}

Value js_function_call(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) {
        vm_throw_error(vm, create_error("TypeError", create_string("Function.prototype.call called on non-function", 44)));
        return VAL_UNDEFINED;
    }
    JSFunction* f = (JSFunction*)get_pointer(this_val);
    BlockHeader* h = (BlockHeader*)((char*)f - sizeof(BlockHeader));
    if (h->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_error("TypeError", create_string("Function.prototype.call called on non-function", 44)));
        return VAL_UNDEFINED;
    }
    
    Value new_this = (arg_count > 0) ? args[0] : VAL_UNDEFINED;
    
    int pass_count = (arg_count > 1) ? (arg_count - 1) : 0;
    Value* pass_args = (arg_count > 1) ? &args[1] : NULL;
    
    JSFunction* actual_f = f;
    if (f->bytecode_offset != 0xffffffff && f->native_ptr != NULL) {
        actual_f = (JSFunction*)f->native_ptr;
    }
    
    extern Value create_bound_native_function(void* native_ptr, Value name, Value env);
    extern Value create_bound_bytecode_function(JSFunction* target, Value bound_this);
    
    Value bound_func;
    if (actual_f->bytecode_offset == 0xffffffff) {
        bound_func = create_bound_native_function(actual_f->native_ptr, actual_f->name, new_this);
    } else {
        bound_func = create_bound_bytecode_function(actual_f, new_this);
    }
    
    vm_push_root(vm, bound_func);
    
    extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
    Value res = vm_call_function(vm, bound_func, pass_count, pass_args);
    
    vm_pop_root(vm);
    
    return res;
}

Value js_function_apply(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) {
        vm_throw_error(vm, create_error("TypeError", create_string("Function.prototype.apply called on non-function", 45)));
        return VAL_UNDEFINED;
    }
    JSFunction* f = (JSFunction*)get_pointer(this_val);
    BlockHeader* h = (BlockHeader*)((char*)f - sizeof(BlockHeader));
    if (h->obj_type != OBJ_FUNCTION) {
        vm_throw_error(vm, create_error("TypeError", create_string("Function.prototype.apply called on non-function", 45)));
        return VAL_UNDEFINED;
    }
    
    Value new_this = (arg_count > 0) ? args[0] : VAL_UNDEFINED;
    
    int pass_count = 0;
    Value* pass_args = NULL;
    
    if (arg_count > 1 && IS_POINTER(args[1])) {
        void* arr_ptr = get_pointer(args[1]);
        BlockHeader* arr_hdr = (BlockHeader*)((char*)arr_ptr - sizeof(BlockHeader));
        if (arr_hdr->obj_type == OBJ_ARRAY) {
            JSArray* arr = (JSArray*)arr_ptr;
            pass_count = arr->length;
            if (pass_count > 0) {
                pass_args = malloc(pass_count * sizeof(Value));
                for (int i = 0; i < pass_count; i++) {
                    pass_args[i] = arr->elements[i];
                }
            }
        }
    }
    
    JSFunction* actual_f = f;
    if (f->bytecode_offset != 0xffffffff && f->native_ptr != NULL) {
        actual_f = (JSFunction*)f->native_ptr;
    }
    
    extern Value create_bound_native_function(void* native_ptr, Value name, Value env);
    extern Value create_bound_bytecode_function(JSFunction* target, Value bound_this);
    
    Value bound_func;
    if (actual_f->bytecode_offset == 0xffffffff) {
        bound_func = create_bound_native_function(actual_f->native_ptr, actual_f->name, new_this);
    } else {
        bound_func = create_bound_bytecode_function(actual_f, new_this);
    }
    
    vm_push_root(vm, bound_func);
    
    extern Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args);
    Value res = vm_call_function(vm, bound_func, pass_count, pass_args);
    
    vm_pop_root(vm);
    
    if (pass_args) {
        free(pass_args);
    }
    
    return res;
}

