import re

with open("src/vm.c", "r") as f:
    vm_content = f.read()

# Extract all js_* function names from vm_init
functions = set(re.findall(r'js_[a-z0-9_]+', vm_content))

# Exclude existing ones like js_console_log
existing = {"js_console_log", "js_promise_then"}
functions = functions - existing

print("#include \"vm.h\"")
print("#include \"alloc.h\"")
print("#include <stdio.h>")
print("#include <stdlib.h>")
print("#include <string.h>")
print("#include <math.h>\n")

for fn in sorted(functions):
    print(f"Value {fn}(VM* vm, Value this_val, int arg_count, Value* args) {{")
    print("    (void)vm; (void)this_val; (void)arg_count; (void)args;")
    print("    return VAL_UNDEFINED;")
    print("}\n")
