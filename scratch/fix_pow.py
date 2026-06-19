import re

with open("src/builtins.c", "r") as f:
    code = f.read()

def replacer(match):
    name = match.group(1)
    if name in ["js_math_pow", "js_math_atan2", "js_math_hypot"]:
        c_func = name.replace("js_math_", "")
        return f"""Value {name}(VM* vm, Value this_val, int arg_count, Value* args) {{
    (void)vm; (void)this_val;
    if (arg_count < 2) return make_double(0.0/0.0);
    Value v1 = value_to_number(args[0]);
    Value v2 = value_to_number(args[1]);
    double d1 = IS_DOUBLE(v1) ? get_double(v1) : (IS_INTEGER(v1) ? get_integer(v1) : 0.0);
    double d2 = IS_DOUBLE(v2) ? get_double(v2) : (IS_INTEGER(v2) ? get_integer(v2) : 0.0);
    return make_double({c_func}(d1, d2));
}}"""
    return match.group(0)

new_code = re.sub(r"Value\s+(js_math_(?:pow|atan2|hypot))\s*\([^)]*\)\s*\{[^{}]*return make_double\([^{}]*\}", replacer, code)

with open("src/builtins.c", "w") as f:
    f.write(new_code)
print("Patched builtins.c successfully.")

