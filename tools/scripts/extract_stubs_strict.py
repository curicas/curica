import re

with open("src/builtins.c", "r") as f:
    content = f.read()

func_pattern = re.compile(r"Value\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{([^}]+)\}")

stubs = []
for match in func_pattern.finditer(content):
    name = match.group(1)
    body = match.group(2).strip()
    lines = [line.strip() for line in body.split('\n') if line.strip()]
    
    # Check if lines consist ONLY of (void) casts and return VAL_UNDEFINED;
    is_stub = True
    has_return = False
    for line in lines:
        if line.startswith("(void)"):
            continue
        elif line == "return VAL_UNDEFINED;":
            has_return = True
        else:
            is_stub = False
            break
            
    if is_stub and has_return:
        stubs.append(name)

print("Found", len(stubs), "strict stubs:")
for s in stubs:
    print(s)
