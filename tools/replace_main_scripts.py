import sys

with open('src/main.c', 'r') as f:
    content = f.read()

content = content.replace('scripts_fetch_js', 'src_js_fetch_js')
content = content.replace('scripts_test_runner_js', 'src_js_test_runner_js')

with open('src/main.c', 'w') as f:
    f.write(content)
print("Updated src/main.c")
