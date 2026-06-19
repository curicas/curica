import re

# 1. Update alloc.c for RegExp memory cleanup
with open("src/alloc.c", "r") as f:
    alloc_code = f.read()

alloc_patch = """                if (block->obj_type == OBJ_BUFFER) {
                    JSBuffer* buf = (JSBuffer*)((char*)block + sizeof(BlockHeader));
                    if (!buf->is_external && buf->data && buf->slab_ref == VAL_UNDEFINED) {
                        free(buf->data);
                    }
                }
                if (block->obj_type == OBJ_REGEXP) {
                    JSRegExp* re = (JSRegExp*)((char*)block + sizeof(BlockHeader));
                    if (re->compiled) {
                        #include <regex.h>
                        regfree((regex_t*)re->compiled);
                        free(re->compiled);
                        re->compiled = NULL;
                    }
                }"""

if "OBJ_REGEXP" not in alloc_code[alloc_code.find("OBJ_BUFFER"):]:
    alloc_code = alloc_code.replace("""                if (block->obj_type == OBJ_BUFFER) {
                    JSBuffer* buf = (JSBuffer*)((char*)block + sizeof(BlockHeader));
                    if (!buf->is_external && buf->data && buf->slab_ref == VAL_UNDEFINED) {
                        free(buf->data);
                    }
                }""", alloc_patch)

with open("src/alloc.c", "w") as f:
    f.write(alloc_code)


# 2. Implement the methods in builtins.c
with open("src/builtins.c", "r") as f:
    bc = f.read()

# Make sure we include regex.h
if "<regex.h>" not in bc:
    bc = bc.replace("#include <math.h>", "#include <math.h>\n#include <regex.h>")

# Add Set methods
bc = re.sub(r'Value js_set_for_each\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_for_each(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_SET) return VAL_UNDEFINED;
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t cb_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    JSSet* set = (JSSet*)get_pointer(vm->gc_roots[this_idx]);
    for (uint32_t i = 0; i < set->capacity; i++) {
        set = (JSSet*)get_pointer(vm->gc_roots[this_idx]);
        Value el = set->elements[i];
        if (el == VAL_UNDEFINED) continue;
        uint32_t el_idx = vm->gc_root_count; vm_push_root(vm, el);
        Value cb_args[3] = { vm->gc_roots[el_idx], vm->gc_roots[el_idx], vm->gc_roots[this_idx] };
        vm_call_function(vm, vm->gc_roots[cb_idx], 3, cb_args);
        vm_pop_root(vm);
    }
    vm_pop_root(vm); vm_pop_root(vm);
    return VAL_UNDEFINED;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_set_values\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_values(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_SET) return VAL_UNDEFINED;
    JSSet* set = (JSSet*)get_pointer(this_val);
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_array(set->count));
    uint32_t out = 0;
    for (uint32_t i = 0; i < set->capacity; i++) {
        set = (JSSet*)get_pointer(vm->gc_roots[this_idx]);
        if (set->elements[i] != VAL_UNDEFINED) {
            array_push(vm->gc_roots[res_idx], set->elements[i]);
        }
    }
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_set_is_subset_of\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_is_subset_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_FALSE;
    JSSet* set1 = (JSSet*)get_pointer(this_val);
    extern bool set_has(Value set_val, Value val);
    for (uint32_t i = 0; i < set1->capacity; i++) {
        if (set1->elements[i] != VAL_UNDEFINED) {
            if (!set_has(args[0], set1->elements[i])) return VAL_FALSE;
        }
    }
    return VAL_TRUE;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_set_is_superset_of\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_is_superset_of(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_FALSE;
    JSSet* set2 = (JSSet*)get_pointer(args[0]);
    extern bool set_has(Value set_val, Value val);
    for (uint32_t i = 0; i < set2->capacity; i++) {
        if (set2->elements[i] != VAL_UNDEFINED) {
            if (!set_has(this_val, set2->elements[i])) return VAL_FALSE;
        }
    }
    return VAL_TRUE;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_set_is_disjoint_from\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_is_disjoint_from(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_FALSE;
    JSSet* set1 = (JSSet*)get_pointer(this_val);
    extern bool set_has(Value set_val, Value val);
    for (uint32_t i = 0; i < set1->capacity; i++) {
        if (set1->elements[i] != VAL_UNDEFINED) {
            if (set_has(args[0], set1->elements[i])) return VAL_FALSE;
        }
    }
    return VAL_TRUE;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_set_symmetric_difference\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_set_symmetric_difference(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_SET || h2->obj_type != OBJ_SET) return VAL_UNDEFINED;
    
    uint32_t res_idx = vm->gc_root_count; vm_push_root(vm, create_set());
    uint32_t this_idx = vm->gc_root_count; vm_push_root(vm, this_val);
    uint32_t arg_idx = vm->gc_root_count; vm_push_root(vm, args[0]);
    extern bool set_add(Value set_val, Value val);
    extern bool set_has(Value set_val, Value val);
    
    JSSet* set1 = (JSSet*)get_pointer(vm->gc_roots[this_idx]);
    for (uint32_t i = 0; i < set1->capacity; i++) {
        if (set1->elements[i] != VAL_UNDEFINED) {
            if (!set_has(vm->gc_roots[arg_idx], set1->elements[i])) {
                set_add(vm->gc_roots[res_idx], set1->elements[i]);
            }
        }
    }
    JSSet* set2 = (JSSet*)get_pointer(vm->gc_roots[arg_idx]);
    for (uint32_t i = 0; i < set2->capacity; i++) {
        if (set2->elements[i] != VAL_UNDEFINED) {
            if (!set_has(vm->gc_roots[this_idx], set2->elements[i])) {
                set_add(vm->gc_roots[res_idx], set2->elements[i]);
            }
        }
    }
    
    Value final_res = vm->gc_roots[res_idx];
    vm_pop_root(vm); vm_pop_root(vm); vm_pop_root(vm);
    return final_res;
}
""", bc, flags=re.DOTALL)


# Object methods
bc += """
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
    // Stub: Prototype chaining isn't fully supported via object_set(__proto__) in standard way here,
    // but we return the object for now.
    (void)vm; (void)arg_count; (void)args;
    return obj;
}

Value js_object_freeze(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    // Stub: Curica has no writable/configurable property descriptors.
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
        obj = (JSObject*)get_pointer(args[0]); // Refresh
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

// RegExp Constructor & Methods
Value js_regexp_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
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
    
    Value re_val = make_pointer(re);
    // Should attach to RegExp prototype ideally, handled by interpreter standard logic.
    return re_val;
}

Value js_regexp_test(VM* vm, Value this_val, int arg_count, Value* args) {
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

Value js_regexp_exec(VM* vm, Value this_val, int arg_count, Value* args) {
    if (!IS_POINTER(this_val) || arg_count < 1) return VAL_NULL;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_REGEXP) return VAL_NULL;
    JSRegExp* re = (JSRegExp*)get_pointer(this_val);
    if (!re->compiled) return VAL_NULL;
    
    uint32_t t_idx = vm->gc_root_count; vm_push_root(vm, value_to_string(args[0]));
    JSString* t_str = (JSString*)get_pointer(vm->gc_roots[t_idx]);
    
    // We only capture up to 10 submatches for simplicity
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
"""

# String replacement methods
bc = re.sub(r'Value js_string_at\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    JSString* str = (JSString*)get_pointer(this_val);
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    int32_t idx = get_integer(args[0]);
    if (idx < 0) idx = str->length + idx;
    if (idx < 0 || idx >= (int32_t)str->length) return VAL_UNDEFINED;
    return create_string(str->data + idx, 1);
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_string_char_at\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_char_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val)) return create_string("", 0);
    JSString* str = (JSString*)get_pointer(this_val);
    int32_t idx = (arg_count >= 1 && IS_INTEGER(args[0])) ? get_integer(args[0]) : 0;
    if (idx < 0 || idx >= (int32_t)str->length) return create_string("", 0);
    return create_string(str->data + idx, 1);
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_string_char_code_at\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_char_code_at(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; extern Value js_global_is_nan(VM*, Value, int, Value*); // Stub to return NaN instead of missing
    if (!IS_POINTER(this_val)) return make_integer(-1); // Return NaN ideally
    JSString* str = (JSString*)get_pointer(this_val);
    int32_t idx = (arg_count >= 1 && IS_INTEGER(args[0])) ? get_integer(args[0]) : 0;
    if (idx < 0 || idx >= (int32_t)str->length) return make_integer(-1); // Return NaN ideally
    return make_integer((unsigned char)str->data[idx]);
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_string_concat_method\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_concat_method(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    JSString* str = (JSString*)get_pointer(this_val);
    uint32_t total_len = str->length;
    for (int i = 0; i < arg_count; i++) {
        Value s = value_to_string(args[i]);
        total_len += ((JSString*)get_pointer(s))->length;
    }
    char* buf = malloc(total_len + 1);
    strcpy(buf, str->data);
    for (int i = 0; i < arg_count; i++) {
        Value s = value_to_string(args[i]);
        strcat(buf, ((JSString*)get_pointer(s))->data);
    }
    Value res = create_string(buf, total_len);
    free(buf);
    return res;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_string_replace\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_replace(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 2) return this_val;
    JSString* str = (JSString*)get_pointer(this_val);
    
    // Check if args[0] is regex
    BlockHeader* arg0_h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (arg0_h->obj_type == OBJ_REGEXP) {
        JSRegExp* re = (JSRegExp*)get_pointer(args[0]);
        if (re->compiled) {
            regmatch_t pmatch[1];
            if (regexec((regex_t*)re->compiled, str->data, 1, pmatch, 0) == 0) {
                JSString* rep = (JSString*)get_pointer(value_to_string(args[1]));
                uint32_t new_len = str->length - (pmatch[0].rm_eo - pmatch[0].rm_so) + rep->length;
                char* buf = malloc(new_len + 1);
                memcpy(buf, str->data, pmatch[0].rm_so);
                memcpy(buf + pmatch[0].rm_so, rep->data, rep->length);
                memcpy(buf + pmatch[0].rm_so + rep->length, str->data + pmatch[0].rm_eo, str->length - pmatch[0].rm_eo);
                buf[new_len] = '\\0';
                Value res = create_string(buf, new_len);
                free(buf);
                return res;
            }
            return this_val;
        }
    }
    
    JSString* search = (JSString*)get_pointer(value_to_string(args[0]));
    JSString* rep = (JSString*)get_pointer(value_to_string(args[1]));
    
    char* pos = strstr(str->data, search->data);
    if (!pos) return this_val;
    
    int prefix_len = pos - str->data;
    uint32_t new_len = str->length - search->length + rep->length;
    char* buf = malloc(new_len + 1);
    
    memcpy(buf, str->data, prefix_len);
    memcpy(buf + prefix_len, rep->data, rep->length);
    memcpy(buf + prefix_len + rep->length, pos + search->length, str->length - prefix_len - search->length);
    buf[new_len] = '\\0';
    
    Value res = create_string(buf, new_len);
    free(buf);
    return res;
}
""", bc, flags=re.DOTALL)

bc = re.sub(r'Value js_string_replace_all\(VM\* vm, Value this_val, int arg_count, Value\* args\) \{.*?\n\}\n', """Value js_string_replace_all(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; if (!IS_POINTER(this_val) || arg_count < 2) return this_val;
    JSString* str = (JSString*)get_pointer(this_val);
    
    // Simplified string-based replaceAll
    JSString* search = (JSString*)get_pointer(value_to_string(args[0]));
    JSString* rep = (JSString*)get_pointer(value_to_string(args[1]));
    
    if (search->length == 0) return this_val; // Avoid infinite loops
    
    char* buf = malloc(str->length * 2 + 1); // rough guess, will realloc if needed
    uint32_t buf_cap = str->length * 2 + 1;
    uint32_t out_len = 0;
    
    char* current = str->data;
    char* next;
    
    while ((next = strstr(current, search->data)) != NULL) {
        int chunk_len = next - current;
        if (out_len + chunk_len + rep->length + 1 > buf_cap) {
            buf_cap *= 2;
            buf = realloc(buf, buf_cap);
        }
        memcpy(buf + out_len, current, chunk_len);
        out_len += chunk_len;
        
        memcpy(buf + out_len, rep->data, rep->length);
        out_len += rep->length;
        
        current = next + search->length;
    }
    
    int remaining = strlen(current);
    if (out_len + remaining + 1 > buf_cap) {
        buf = realloc(buf, out_len + remaining + 1);
    }
    strcpy(buf + out_len, current);
    out_len += remaining;
    
    Value res = create_string(buf, out_len);
    free(buf);
    return res;
}
""", bc, flags=re.DOTALL)


with open("src/builtins.c", "w") as f:
    f.write(bc)


# 3. Update vm.c for Object methods and RegExp constructor
with open("src/vm.c", "r") as f:
    vmc = f.read()

# Add to Object
if "js_object_assign" not in vmc:
    vmc = vmc.replace("""    object_set(vm->global_obj, create_string("Object", 6), object_ctor);""", """    object_set(vm->global_obj, create_string("Object", 6), object_ctor);
    extern Value js_object_assign(VM*, Value, int, Value*);
    extern Value js_object_create(VM*, Value, int, Value*);
    extern Value js_object_entries(VM*, Value, int, Value*);
    extern Value js_object_freeze(VM*, Value, int, Value*);
    extern Value js_object_from_entries(VM*, Value, int, Value*);
    extern Value js_object_values(VM*, Value, int, Value*);
    object_set(object_ctor, create_string("assign", 6), create_native_function((void*)js_object_assign, create_string("assign", 6)));
    object_set(object_ctor, create_string("create", 6), create_native_function((void*)js_object_create, create_string("create", 6)));
    object_set(object_ctor, create_string("entries", 7), create_native_function((void*)js_object_entries, create_string("entries", 7)));
    object_set(object_ctor, create_string("freeze", 6), create_native_function((void*)js_object_freeze, create_string("freeze", 6)));
    object_set(object_ctor, create_string("fromEntries", 11), create_native_function((void*)js_object_from_entries, create_string("fromEntries", 11)));
    object_set(object_ctor, create_string("values", 6), create_native_function((void*)js_object_values, create_string("values", 6)));
""")

# RegExp constructor and proto
if "js_regexp_constructor" not in vmc:
    vmc = vmc.replace("""    object_set(vm->global_obj, create_string("JSON", 4), json_obj);""", """    object_set(vm->global_obj, create_string("JSON", 4), json_obj);
    extern Value js_regexp_constructor(VM*, Value, int, Value*);
    extern Value js_regexp_test(VM*, Value, int, Value*);
    extern Value js_regexp_exec(VM*, Value, int, Value*);
    Value regexp_ctor = create_native_function((void*)js_regexp_constructor, create_string("RegExp", 6));
    Value regexp_proto = create_object();
    object_set(regexp_proto, create_string("test", 4), create_native_function((void*)js_regexp_test, create_string("test", 4)));
    object_set(regexp_proto, create_string("exec", 4), create_native_function((void*)js_regexp_exec, create_string("exec", 4)));
    object_set(regexp_ctor, create_string("prototype", 9), regexp_proto);
    object_set(vm->global_obj, create_string("RegExp", 6), regexp_ctor);
""")

with open("src/vm.c", "w") as f:
    f.write(vmc)

