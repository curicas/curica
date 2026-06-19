import re

with open("src/builtins.c", "r") as f:
    code = f.read()

# Fix js_throw_error
code = code.replace(
    "return js_throw_error(vm, this_val, 0, NULL);",
    "{ vm_throw_error(vm, create_string(\"Reduce of empty array with no initial value\", 43)); return VAL_UNDEFINED; }"
)

# Fix uint32_t start/end comparisons
code = code.replace(
    "uint32_t start = 0; uint32_t end = arr->length;",
    "int32_t start = 0; int32_t end = arr->length;"
)

code = code.replace(
    "uint32_t start = 0;\n    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);\n    for (uint32_t i = start; i < arr->length; i++) {",
    "int32_t start = 0;\n    if (arg_count >= 2 && IS_INTEGER(args[1])) start = get_integer(args[1]);\n    if (start < 0) start = arr->length + start;\n    if (start < 0) start = 0;\n    for (uint32_t i = start; i < arr->length; i++) {"
)

with open("src/builtins.c", "w") as f:
    f.write(code)
