import os

# Update builtins.h
with open("src/builtins.h", "r") as f:
    bh = f.read()
if "js_global_encode_uri_component" not in bh:
    bh = bh.replace(
        "Value js_global_is_nan(VM* vm, Value this_val, int arg_count, Value* args);",
        "Value js_global_is_nan(VM* vm, Value this_val, int arg_count, Value* args);\nValue js_global_encode_uri_component(VM* vm, Value this_val, int arg_count, Value* args);\nValue js_global_decode_uri_component(VM* vm, Value this_val, int arg_count, Value* args);"
    )
    with open("src/builtins.h", "w") as f:
        f.write(bh)

# Update vm.c
with open("src/vm.c", "r") as f:
    vmc = f.read()
if "encodeURIComponent" not in vmc:
    vmc = vmc.replace(
        'object_set(vm->global_obj, create_string("isNaN", 5), create_native_function((void*)js_global_is_nan, create_string("isNaN", 5)));',
        'object_set(vm->global_obj, create_string("isNaN", 5), create_native_function((void*)js_global_is_nan, create_string("isNaN", 5)));\n    object_set(vm->global_obj, create_string("encodeURIComponent", 18), create_native_function((void*)js_global_encode_uri_component, create_string("encodeURIComponent", 18)));\n    object_set(vm->global_obj, create_string("decodeURIComponent", 18), create_native_function((void*)js_global_decode_uri_component, create_string("decodeURIComponent", 18)));'
    )
    with open("src/vm.c", "w") as f:
        f.write(vmc)

# Update builtins.c
with open("src/builtins.c", "r") as f:
    bc = f.read()
if "js_global_encode_uri_component" not in bc:
    impl = """
Value js_global_encode_uri_component(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    Value str_val = arg_count > 0 ? value_to_string(args[0]) : create_string("undefined", 9);
    JSString* str = (JSString*)get_pointer(str_val);
    uint32_t out_cap = str->length * 3 + 1;
    char* out = malloc(out_cap);
    uint32_t out_len = 0;
    
    for (uint32_t i = 0; i < str->length; i++) {
        unsigned char c = (unsigned char)str->data[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '!' || c == '~' || c == '*' || c == '\\'' || c == '(' || c == ')') {
            out[out_len++] = c;
        } else {
            sprintf(out + out_len, "%%%02X", c);
            out_len += 3;
        }
    }
    Value res = create_string(out, out_len);
    free(out);
    return res;
}

Value js_global_decode_uri_component(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    Value str_val = arg_count > 0 ? value_to_string(args[0]) : create_string("undefined", 9);
    JSString* str = (JSString*)get_pointer(str_val);
    char* out = malloc(str->length + 1);
    uint32_t out_len = 0;
    
    for (uint32_t i = 0; i < str->length; i++) {
        if (str->data[i] == '%') {
            if (i + 2 < str->length) {
                char hex[3] = { str->data[i+1], str->data[i+2], 0 };
                char* endptr;
                long val = strtol(hex, &endptr, 16);
                if (*endptr == 0) {
                    out[out_len++] = (char)val;
                    i += 2;
                } else {
                    out[out_len++] = str->data[i];
                }
            } else {
                out[out_len++] = str->data[i];
            }
        } else {
            out[out_len++] = str->data[i];
        }
    }
    Value res = create_string(out, out_len);
    free(out);
    return res;
}
"""
    bc += impl
    with open("src/builtins.c", "w") as f:
        f.write(bc)

