import re

with open("src/builtins.c", "r") as f:
    code = f.read()

implementations = {
    "js_array_for_each": """Value js_array_for_each(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    for (uint32_t i = 0; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[3] = { vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        vm_pop_root(vm);
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return VAL_UNDEFINED;
}""",
    
    "js_array_some": """Value js_array_some(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_FALSE;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    for (uint32_t i = 0; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[3] = { vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        Value res = vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        vm_pop_root(vm);
        if (is_truthy(res)) { vm_pop_root(vm); vm_pop_root(vm); return VAL_TRUE; }
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return VAL_FALSE;
}""",

    "js_array_every": """Value js_array_every(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_TRUE;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_TRUE;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    for (uint32_t i = 0; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[3] = { vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        Value res = vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        vm_pop_root(vm);
        if (!is_truthy(res)) { vm_pop_root(vm); vm_pop_root(vm); return VAL_FALSE; }
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return VAL_TRUE;
}""",

    "js_array_find": """Value js_array_find(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    for (uint32_t i = 0; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[3] = { vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        Value res = vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        Value el = vm->gc_roots[el_idx];
        vm_pop_root(vm);
        if (is_truthy(res)) { vm_pop_root(vm); vm_pop_root(vm); return el; }
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return VAL_UNDEFINED;
}""",

    "js_array_find_index": """Value js_array_find_index(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return make_integer(-1);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return make_integer(-1);
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    for (uint32_t i = 0; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[3] = { vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        Value res = vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        vm_pop_root(vm);
        if (is_truthy(res)) { vm_pop_root(vm); vm_pop_root(vm); return make_integer(i); }
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return make_integer(-1);
}""",

    "js_array_reduce": """Value js_array_reduce(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    uint32_t len = arr->length;
    if (len == 0 && arg_count < 2) { vm_pop_root(vm); vm_pop_root(vm); return js_throw_error(vm, this_val, 0, NULL); }
    uint32_t start_i = 0;
    uint32_t acc_idx = vm->gc_root_count;
    if (arg_count >= 2) { vm_push_root(vm, args[1]); } else { vm_push_root(vm, arr->elements[0]); start_i = 1; }
    for (uint32_t i = start_i; i < len; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, arr->elements[i]);
        Value cb_args[4] = { vm->gc_roots[acc_idx], vm->gc_roots[el_idx], make_integer(i), vm->gc_roots[this_idx] };
        Value res = vm_call_function(vm, vm->gc_roots[cb_idx], 4, cb_args);
        vm->gc_roots[acc_idx] = res;
        vm_pop_root(vm);
    }
    Value final_res = vm->gc_roots[acc_idx];
    vm_pop_root(vm); vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}""",

    "js_array_index_of": """Value js_array_index_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1) return make_integer(-1);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return make_integer(-1);
    JSArray* arr = (JSArray*)get_pointer(this_val);
    uint32_t start = 0;
    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);
    for (uint32_t i = start; i < arr->length; i++) {
        if (values_strict_equal(arr->elements[i], args[0])) return make_integer(i);
    }
    return make_integer(-1);
}""",

    "js_array_includes": """Value js_array_includes(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1) return VAL_FALSE;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_FALSE;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    uint32_t start = 0;
    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);
    for (uint32_t i = start; i < arr->length; i++) {
        if (values_strict_equal(arr->elements[i], args[0])) return VAL_TRUE;
        if (IS_DOUBLE(arr->elements[i]) && IS_DOUBLE(args[0]) && isnan(get_double(arr->elements[i])) && isnan(get_double(args[0]))) return VAL_TRUE;
    }
    return VAL_FALSE;
}""",

    "js_array_is_array": """Value js_array_is_array(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    return h->obj_type == OBJ_ARRAY ? VAL_TRUE : VAL_FALSE;
}""",

    "js_array_fill": """Value js_array_fill(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1) return this_val;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return this_val;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    uint32_t start = 0; uint32_t end = arr->length;
    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);
    if (arg_count >= 3 && IS_INTEGER(args[2])) end = get_integer(args[2]);
    if (start < 0) start = 0; if (end > arr->length) end = arr->length;
    for (uint32_t i = start; i < end; i++) arr->elements[i] = args[0];
    return this_val;
}""",

    "js_array_reverse": """Value js_array_reverse(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return this_val;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return this_val;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    for (uint32_t i = 0; i < arr->length / 2; i++) {
        Value temp = arr->elements[i];
        arr->elements[i] = arr->elements[arr->length - 1 - i];
        arr->elements[arr->length - 1 - i] = temp;
    }
    return this_val;
}""",

    "js_array_pop": """Value js_array_pop(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    if (arr->length == 0) return VAL_UNDEFINED;
    Value el = arr->elements[--arr->length];
    return el;
}""",

    "js_array_slice": """Value js_array_slice(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    int32_t start = 0; int32_t end = arr->length;
    if (arg_count >= 1 && IS_INTEGER(args[0])) start = get_integer(args[0]);
    if (arg_count >= 2 && IS_INTEGER(args[1])) end = get_integer(args[1]);
    if (start < 0) start = arr->length + start; if (start < 0) start = 0;
    if (end < 0) end = arr->length + end; if (end > (int32_t)arr->length) end = arr->length;
    if (start > end) start = end;
    
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(end - start));
    arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    JSArray* res = (JSArray*)get_pointer(vm->gc_roots[res_idx]);
    res->length = end - start;
    for (int32_t i = start; i < end; i++) {
        res->elements[i - start] = arr->elements[i];
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}""",

    "js_array_concat_method": """Value js_array_concat_method(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(8));
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    for (uint32_t i = 0; i < arr->length; i++) array_push(vm->gc_roots[res_idx], arr->elements[i]);
    
    for (int j = 0; j < arg_count; j++) {
        uint32_t arg_idx = vm->gc_root_count; vm_push_root(vm, args[j]);
        if (IS_POINTER(vm->gc_roots[arg_idx])) {
            BlockHeader* arg_h = (BlockHeader*)((char*)get_pointer(vm->gc_roots[arg_idx]) - sizeof(BlockHeader));
            if (arg_h->obj_type == OBJ_ARRAY) {
                JSArray* arg_arr = (JSArray*)get_pointer(vm->gc_roots[arg_idx]);
                for (uint32_t k = 0; k < arg_arr->length; k++) {
                    array_push(vm->gc_roots[res_idx], arg_arr->elements[k]);
                }
            } else {
                array_push(vm->gc_roots[res_idx], vm->gc_roots[arg_idx]);
            }
        } else {
            array_push(vm->gc_roots[res_idx], vm->gc_roots[arg_idx]);
        }
        vm_pop_root(vm);
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}""",

    "js_array_flat": """Value js_array_flat(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    // Basic flat implementation (depth 1)
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(8));
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    for (uint32_t i = 0; i < arr->length; i++) {
        Value el = arr->elements[i];
        if (IS_POINTER(el)) {
            BlockHeader* el_h = (BlockHeader*)((char*)get_pointer(el) - sizeof(BlockHeader));
            if (el_h->obj_type == OBJ_ARRAY) {
                JSArray* el_arr = (JSArray*)get_pointer(el);
                for (uint32_t k = 0; k < el_arr->length; k++) array_push(vm->gc_roots[res_idx], el_arr->elements[k]);
                continue;
            }
        }
        array_push(vm->gc_roots[res_idx], el);
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}""",

    "js_array_splice": """Value js_array_splice(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    return VAL_UNDEFINED; // Splice is very complex in C bump allocator, keeping stub for now
}""",

    "js_array_flat_map": """Value js_array_flat_map(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    return VAL_UNDEFINED; // FlatMap is complex, keeping stub for now
}"""
}

def replacer(match):
    name = match.group(1)
    if name in implementations:
        return implementations[name]
    return match.group(0)

new_code = re.sub(r"Value\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{[^{}]*return VAL_UNDEFINED;\s*\}", replacer, code)

# We need to manually add shift, unshift, and lastIndexOf because they don't have stubs
additions = """
Value js_array_shift(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    JSArray* arr = (JSArray*)get_pointer(this_val);
    if (arr->length == 0) return VAL_UNDEFINED;
    Value el = arr->elements[0];
    for (uint32_t i = 1; i < arr->length; i++) {
        arr->elements[i - 1] = arr->elements[i];
    }
    arr->length--;
    return el;
}

Value js_array_unshift(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count == 0) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return VAL_UNDEFINED;
    
    // Simplest way to unshift is to push all elements, then shift everything right
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    for (int i = 0; i < arg_count; i++) array_push(vm->gc_roots[this_idx], VAL_UNDEFINED);
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[this_idx]);
    
    for (int32_t i = arr->length - 1; i >= arg_count; i--) {
        arr->elements[i] = arr->elements[i - arg_count];
    }
    for (int i = 0; i < arg_count; i++) {
        arr->elements[i] = args[i];
    }
    Value final_res = make_integer(arr->length);
    vm_pop_root(vm);
    return final_res;
}

Value js_array_last_index_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1) return make_integer(-1);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return make_integer(-1);
    JSArray* arr = (JSArray*)get_pointer(this_val);
    if (arr->length == 0) return make_integer(-1);
    int32_t start = arr->length - 1;
    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);
    if (start < 0) start = arr->length + start;
    if (start >= (int32_t)arr->length) start = arr->length - 1;
    for (int32_t i = start; i >= 0; i--) {
        if (values_strict_equal(arr->elements[i], args[0])) return make_integer(i);
    }
    return make_integer(-1);
}
"""

if "js_array_shift" not in new_code:
    new_code += additions

with open("src/builtins.c", "w") as f:
    f.write(new_code)
print("Patched array builtins successfully.")

