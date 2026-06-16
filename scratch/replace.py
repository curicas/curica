import os

files_to_update = [
    'src/repl.c',
    'src/wasm_module.c',
    'src/child_process_module.c'
]

for file in files_to_update:
    with open(file, 'r') as f:
        content = f.read()
    
    # Replace 'sysroot' with 'vfs'
    content = content.replace('sysroot', 'vfs')
    
    with open(file, 'w') as f:
        f.write(content)
    print(f"Updated {file}")
