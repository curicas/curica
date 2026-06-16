import re

with open('src/main.c', 'r') as f:
    code = f.read()

# First remove all EventLoop loop; el_init(&loop);
code = re.sub(r'EventLoop loop;\s*el_init\(&loop\);.*?\n', '', code)
code = re.sub(r'/\* EventLoop was initialized before VM \*/.*?\n', '', code)

# Now, before every "VM vm;\n        vm_init(&vm);", insert EventLoop loop; el_init(&loop);
code = code.replace('VM vm;\n        vm_init(&vm);', 'EventLoop loop;\n        el_init(&loop);\n        VM vm;\n        vm_init(&vm);')

with open('src/main.c', 'w') as f:
    f.write(code)

