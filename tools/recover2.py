import os
import json
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

for log_id in logs:
    log_path = f"{brain_dir}/{log_id}/.system_generated/logs/overview.txt"
    if not os.path.exists(log_path): continue
    with open(log_path, 'r', encoding='utf-8') as f:
        content = f.read()
        # Find view_file or write_to_file outputs containing the file
        if "js_promise_constructor" in content:
            print(f"Found in {log_id}")
            # we can use a simple regex to find the biggest code block
            blocks = re.findall(r'```c\n(.*?)\n```', content, re.DOTALL)
            for b in blocks:
                if 'js_promise_constructor' in b:
                    print(f"Code block size: {len(b)}")
                    with open("recovered_builtins.c", "w") as out:
                        out.write(b)
