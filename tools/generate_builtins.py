import re

with open("src/vm.c", "r") as f:
    vm_content = f.read()

# Extract all js_* function names from vm_init
functions = set(re.findall(r'js_[a-zA-Z0-9_]+', vm_content))

# Look for static declarations in vm.c and alloc.c
existing = set()
for file in ["src/vm.c", "src/alloc.c"]:
    with open(file, "r") as f:
        content = f.read()
        existing.update(re.findall(r'Value\s+(js_[a-zA-Z0-9_]+)\s*\(', content))

missing = functions - existing

with open("src/builtins.h", "w") as f:
    f.write("#ifndef BUILTINS_H\n#define BUILTINS_H\n\n#include \"vm.h\"\n\n")
    for fn in sorted(missing):
        f.write(f"Value {fn}(VM* vm, Value this_val, int arg_count, Value* args);\n")
    f.write("\n#endif // BUILTINS_H\n")

with open("src/builtins.c", "w") as f:
    f.write("#include \"builtins.h\"\n#include \"alloc.h\"\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <math.h>\n\n")
    for fn in sorted(missing):
        f.write(f"Value {fn}(VM* vm, Value this_val, int arg_count, Value* args) {{\n")
        f.write("    (void)vm; (void)this_val; (void)arg_count; (void)args;\n")
        f.write("    return VAL_UNDEFINED;\n")
        f.write("}\n\n")
