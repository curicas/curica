import os
import re

logs = [
    "33895116-e03a-4738-a704-43e00d679b24",
    "f5029c9d-9edd-4cd0-b062-c146d8777fc8",
    "7f4b0180-b12a-4cc8-8740-8270185239b3",
    "88c83a16-bfdc-4b53-90b3-9c949979c9c6",
    "7eab5e2b-e011-47e6-83e5-c83416712647",
    "602dfa06-d167-474e-9d37-a636c118e81a"
]

brain_dir = os.path.expanduser("~/.gemini/antigravity/brain")

best_content = {}

ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

for log_id in logs:
    log_path = f"{brain_dir}/{log_id}/.system_generated/logs/overview.txt"
    if not os.path.exists(log_path): continue
    with open(log_path, 'r', encoding='utf-8') as f:
        content = f.read()
        content = ansi_escape.sub('', content)
        
    parts = content.split("File Path: `file:///run/media/user/development/JS/Curica%20Runtime/src/builtins.c`")
    for part in parts[1:]:
        lines = part.split("\n")
        for line in lines:
            # Format is: "1: #include \"builtins.h\""
            m = re.match(r'^\s*(\d+):\s(.*)', line)
            if m:
                ln = int(m.group(1))
                best_content[ln] = m.group(2)
                
with open("recovered.c", "w") as f:
    for i in range(1, max(best_content.keys()) + 1 if best_content else 1):
        f.write(best_content.get(i, f"// MISSING LINE {i}") + "\n")

print(f"Recovered {len(best_content)} lines.")
