import re

with open("src/builtins.c", "r") as f:
    code = f.read()

def replacer(match):
    name = match.group(1)
    # Match standard math functions
    math_funcs = {
        "abs": "fabs", "acos": "acos", "asin": "asin", "atan": "atan", "cbrt": "cbrt",
        "ceil": "ceil", "cos": "cos", "exp": "exp", "floor": "floor", "log": "log",
        "log10": "log10", "log2": "log2", "round": "round", "sin": "sin", "sqrt": "sqrt",
        "tan": "tan", "trunc": "trunc"
    }
    
    math_name = name.replace("js_math_", "")
    if math_name in math_funcs:
        c_func = math_funcs[math_name]
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1) return make_double(0.0/0.0);
    Value v = value_to_number(args[0]);
    double d = IS_DOUBLE(v) ? get_double(v) : (IS_INTEGER(v) ? get_integer(v) : 0.0);
    return make_double({c_func}(d));
}}"""

    if name == "js_math_sign":
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1) return make_double(0.0/0.0);
    Value v = value_to_number(args[0]);
    double d = IS_DOUBLE(v) ? get_double(v) : (IS_INTEGER(v) ? get_integer(v) : 0.0);
    if (isnan(d)) return make_double(0.0/0.0);
    if (d == 0.0) return make_double(0.0);
    return make_double(d > 0.0 ? 1.0 : -1.0);
}}"""

    if name == "js_math_max" or name == "js_math_min":
        is_max = (name == "js_math_max")
        init_val = "-1.0/0.0" if is_max else "1.0/0.0"
        cmp_op = ">" if is_max else "<"
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count == 0) return make_double({init_val});
    double best = {init_val};
    for (int i = 0; i < arg_count; i++) {{
        Value v = value_to_number(args[i]);
        double d = IS_DOUBLE(v) ? get_double(v) : (IS_INTEGER(v) ? get_integer(v) : 0.0);
        if (isnan(d)) return make_double(0.0/0.0);
        if (d {cmp_op} best) best = d;
    }}
    return make_double(best);
}}"""

    if name == "js_math_random":
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val; (void)arg_count; (void)args;
    return make_double((double)rand() / (double)RAND_MAX);
}}"""

    # Error Constructors
    error_types = {
        "js_range_error_constructor": "RangeError",
        "js_reference_error_constructor": "ReferenceError",
        "js_syntax_error_constructor": "SyntaxError",
        "js_type_error_constructor": "TypeError",
        "js_suppressed_error_constructor": "SuppressedError"
    }
    if name in error_types:
        err_type = error_types[name]
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    Value msg = arg_count > 0 ? value_to_string(args[0]) : create_string("", 0);
    return create_error("{err_type}", msg);
}}"""

    # Global NaN/Finite
    if name == "js_global_is_nan" or name == "js_global_is_finite":
        c_func = "isnan" if name == "js_global_is_nan" else "isfinite"
        bool_val = "VAL_TRUE" if c_func == "isnan" else "VAL_TRUE"
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1) return { "VAL_TRUE" if name == "js_global_is_nan" else "VAL_FALSE" };
    Value v = value_to_number(args[0]);
    double d = IS_DOUBLE(v) ? get_double(v) : (IS_INTEGER(v) ? get_integer(v) : 0.0);
    return {c_func}(d) ? VAL_TRUE : VAL_FALSE;
}}"""

    # Number NaN/Finite/Integer
    if name == "js_number_is_nan":
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_DOUBLE(args[0])) return VAL_FALSE;
    return isnan(get_double(args[0])) ? VAL_TRUE : VAL_FALSE;
}}"""
    if name == "js_number_is_finite":
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_FALSE;
    if (IS_INTEGER(args[0])) return VAL_TRUE;
    if (IS_DOUBLE(args[0]) && isfinite(get_double(args[0]))) return VAL_TRUE;
    return VAL_FALSE;
}}"""
    if name == "js_number_is_integer" or name == "js_number_is_safe_integer":
        is_safe = (name == "js_number_is_safe_integer")
        safe_cond = " && d >= -9007199254740991.0 && d <= 9007199254740991.0" if is_safe else ""
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 1) return VAL_FALSE;
    if (IS_INTEGER(args[0])) return VAL_TRUE;
    if (IS_DOUBLE(args[0])) {{
        double d = get_double(args[0]);
        if (isfinite(d) && floor(d) == d{safe_cond}) return VAL_TRUE;
    }}
    return VAL_FALSE;
}}"""

    # String Search
    string_search = ["js_string_includes", "js_string_starts_with", "js_string_ends_with"]
    if name in string_search:
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm;
    if (!IS_POINTER(this_val) || arg_count < 1 || !IS_POINTER(args[0])) return VAL_FALSE;
    BlockHeader* h1 = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    BlockHeader* h2 = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h1->obj_type != OBJ_STRING || h2->obj_type != OBJ_STRING) return VAL_FALSE;
    JSString* s1 = (JSString*)get_pointer(this_val);
    JSString* s2 = (JSString*)get_pointer(args[0]);
    if (s2->length > s1->length) return VAL_FALSE;
    if (s2->length == 0) return VAL_TRUE;
    {"if (strstr(s1->data, s2->data) != NULL) return VAL_TRUE;" if name == "js_string_includes" else ""}
    {"if (strncmp(s1->data, s2->data, s2->length) == 0) return VAL_TRUE;" if name == "js_string_starts_with" else ""}
    {"if (strcmp(s1->data + s1->length - s2->length, s2->data) == 0) return VAL_TRUE;" if name == "js_string_ends_with" else ""}
    return VAL_FALSE;
}}"""

    # String Formatting
    if name == "js_string_to_lower" or name == "js_string_to_upper":
        func = "tolower" if "lower" in name else "toupper"
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    JSString* s = (JSString*)get_pointer(this_val);
    char* buf = malloc(s->length + 1);
    for (uint32_t i = 0; i < s->length; i++) buf[i] = {func}((unsigned char)s->data[i]);
    buf[s->length] = '\\0';
    Value res = create_string(buf, s->length);
    free(buf);
    return res;
}}"""

    if name in ["js_string_trim", "js_string_trim_start", "js_string_trim_end"]:
        do_start = name != "js_string_trim_end"
        do_end = name != "js_string_trim_start"
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)arg_count; (void)args;
    if (!IS_POINTER(this_val)) return VAL_UNDEFINED;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(this_val) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_STRING) return VAL_UNDEFINED;
    JSString* s = (JSString*)get_pointer(this_val);
    const char* start = s->data;
    const char* end = s->data + s->length - 1;
    {"while (*start && (*start == ' ' || *start == '\\n' || *start == '\\r' || *start == '\\t')) start++;" if do_start else ""}
    {"while (end >= start && (*end == ' ' || *end == '\\n' || *end == '\\r' || *end == '\\t')) end--;" if do_end else ""}
    int len = end >= start ? (end - start + 1) : 0;
    return create_string(start, len);
}}"""

    return match.group(0)

new_code = re.sub(r"Value\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{[^{}]*return VAL_UNDEFINED;\s*\}", replacer, code)

with open("src/builtins.c", "w") as f:
    f.write(new_code)
print("Patched builtins.c successfully.")

