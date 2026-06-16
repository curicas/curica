import os
import glob
brain_dir = os.path.expanduser("~/.gemini/antigravity/brain")
log_files = glob.glob(f"{brain_dir}/*/.system_generated/logs/overview.txt")
found = False
for log in log_files:
    try:
        with open(log, 'r', encoding='utf-8') as f:
            content = f.read()
            if 'js_json_stringify' in content or 'js_promise_constructor' in content:
                print(f"Found something in {log}")
    except Exception as e:
        print(e)
