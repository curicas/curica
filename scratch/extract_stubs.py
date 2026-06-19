import re

with open("src/builtins.c", "r") as f:
    content = f.read()

# Match function signatures
func_pattern = re.compile(r"Value\s+([a-zA-Z0-9_]+)\s*\([^)]*\)\s*\{([^}]+)\}")

stubs = []
for match in func_pattern.finditer(content):
    name = match.group(1)
    body = match.group(2)
    # A stub usually just has (void) casts and returns VAL_UNDEFINED
    if "return VAL_UNDEFINED;" in body and body.count(";") <= 5:
        stubs.append(name)

print("Found", len(stubs), "stubs:")
for s in stubs:
    print(s)
