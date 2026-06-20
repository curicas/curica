import re

with open("src/builtins.c", "r") as f:
    bc = f.read()

# The appended block started with: Value js_object_assign
idx = bc.find("Value js_object_assign(VM* vm, Value this_val, int arg_count, Value* args)")
if idx != -1:
    appended_block = bc[idx:]
    bc = bc[:idx]
    
    # We will replace the stubs instead
    bc = re.sub(r'Value js_regexp_constructor\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_regexp_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_UNDEFINED;
    Value src = value_to_string(args[0]);
    Value flags = (arg_count > 1) ? value_to_string(args[1]) : create_string("", 0);
    
    JSRegExp* re = (JSRegExp*)arena_alloc(OBJ_REGEXP, sizeof(JSRegExp));
    re->source = src;
    re->flags = flags;
    re->global = 0; re->ignore_case = 0; re->multiline = 0; re->dot_all = 0;
    
    JSString* flags_str = (JSString*)get_pointer(flags);
    int cflags = REG_EXTENDED;
    for (uint32_t i = 0; i < flags_str->length; i++) {
        char c = flags_str->data[i];
        if (c == 'g') re->global = 1;
        if (c == 'i') { re->ignore_case = 1; cflags |= REG_ICASE; }
        if (c == 'm') { re->multiline = 1; cflags |= REG_NEWLINE; }
        if (c == 's') re->dot_all = 1;
    }
    
    regex_t* compiled = malloc(sizeof(regex_t));
    JSString* src_str = (JSString*)get_pointer(src);
    if (regcomp(compiled, src_str->data, cflags) != 0) {
        free(compiled);
        re->compiled = NULL;
    } else {
        re->compiled = compiled;
    }
    
    return make_pointer(re);
}
""", bc, flags=re.DOTALL)

    bc = re.sub(r'Value js_regexp_test\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_regexp_test(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1) return VAL_FALSE;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_REGEXP) return VAL_FALSE;
    JSRegExp* re = (JSRegExp*)get_pointer(this_val);
    if (!re->compiled) return VAL_FALSE;
    
    Value target = value_to_string(args[0]);
    JSString* t_str = (JSString*)get_pointer(target);
    
    regmatch_t pmatch[1];
    if (regexec((regex_t*)re->compiled, t_str->data, 1, pmatch, 0) == 0) {
        return VAL_TRUE;
    }
    return VAL_FALSE;
}
""", bc, flags=re.DOTALL)

    bc = re.sub(r'Value js_regexp_exec\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_regexp_exec(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1) return VAL_NULL;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_REGEXP) return VAL_NULL;
    JSRegExp* re = (JSRegExp*)get_pointer(this_val);
    if (!re->compiled) return VAL_NULL;
    
    uint32_t t_idx = vm->gc_root_count; vm_push_root(vm, value_to_string(args[0]));
    JSString* t_str = (JSString*)get_pointer(vm->gc_roots[t_idx]);
    
    regmatch_t pmatch[10];
    if (regexec((regex_t*)re->compiled, t_str->data, 10, pmatch, 0) == 0) {
        Value arr_val = create_array(0);
        uint32_t arr_idx = vm->gc_root_count; vm_push_root(vm, arr_val);
        for (int i = 0; i < 10 && pmatch[i].rm_so != -1; i++) {
            t_str = (JSString*)get_pointer(vm->gc_roots[t_idx]);
            Value match_str = create_string(t_str->data + pmatch[i].rm_so, pmatch[i].rm_eo - pmatch[i].rm_so);
            array_push(vm->gc_roots[arr_idx], match_str);
        }
        Value final_res = vm->gc_roots[arr_idx];
        vm_pop_root(vm); vm_pop_root(vm);
        return final_res;
    }
    vm_pop_root(vm);
    return VAL_NULL;
}
""", bc, flags=re.DOTALL)

    # Re-append the object methods since they didn't have stubs
    obj_methods = """
Value js_object_assign(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    uint32_t target_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    for (int i = 1; i < arg_count; i++) {
        if (!IS_POINTER(args[i])) continue;
        BlockHeader* arg_h = (BlockHeader*)((char*)get_pointer(args[i]) - sizeof(BlockHeader));
        if (arg_h->obj_type == OBJ_OBJECT) {
            JSObject* src = (JSObject*)get_pointer(args[i]);
            for (uint32_t p = 0; p < src->count; p++) {
                object_set(vm->gc_roots[target_idx], src->properties[p].key, src->properties[p].value);
            }
        }
    }
    Value res = vm->gc_roots[target_idx];
    vm_pop_root(vm);
    return res;
}

Value js_object_create(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    Value obj = create_object();
    (void)vm; (void)arg_count; (void)args;
    return obj;
}

Value js_object_freeze(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    return arg_count > 0 ? args[0] : VAL_UNDEFINED;
}

Value js_object_entries(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return create_array(0);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_OBJECT) return create_array(0);
    
    JSObject* obj = (JSObject*)get_pointer(args[0]);
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(obj->count));
    for (uint32_t i = 0; i < obj->count; i++) {
        Value pair = create_array(2);
        uint32_t pair_idx = vm->gc_root_count; vm_push_root(vm, pair);
        array_push(pair, obj->properties[i].key);
        array_push(pair, obj->properties[i].value);
        array_push(vm->gc_roots[res_idx], pair);
        vm_pop_root(vm);
        obj = (JSObject*)get_pointer(args[0]);
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm);
    return final_res;
}

Value js_object_values(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return create_array(0);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_OBJECT) return create_array(0);
    
    JSObject* obj = (JSObject*)get_pointer(args[0]);
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(obj->count));
    for (uint32_t i = 0; i < obj->count; i++) {
        array_push(vm->gc_roots[res_idx], obj->properties[i].value);
        obj = (JSObject*)get_pointer(args[0]);
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm);
    return final_res;
}

Value js_object_from_entries(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return create_object();
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_ARRAY) return create_object();
    
    uint32_t arr_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_object());
    
    JSArray* arr = (JSArray*)get_pointer(vm->gc_roots[arr_idx]);
    for (uint32_t i = 0; i < arr->length; i++) {
        arr = (JSArray*)get_pointer(vm->gc_roots[arr_idx]);
        Value el = arr->elements[i];
        if (IS_POINTER(el)) {
            BlockHeader* el_h = (BlockHeader*)((char*)get_pointer(el) - sizeof(BlockHeader));
            if (el_h->obj_type == OBJ_ARRAY) {
                JSArray* pair = (JSArray*)get_pointer(el);
                if (pair->length >= 2) {
                    object_set(vm->gc_roots[res_idx], value_to_string(pair->elements[0]), pair->elements[1]);
                }
            }
        }
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}
"""
    bc += obj_methods

    with open("src/builtins.c", "w") as f:
        f.write(bc)
