import re

with open("src/vm.c", "r") as f:
    text = f.read()

# Replace vm->bytecode with frame->prog->bytecode inside vm_run
# Wait, we should only do this inside vm_run and maybe vm_call?
# In vm.c:
# 1177: vm->bytecode = bytecode; -> KEEP
# 881: vm->bytecode = NULL; -> KEEP
# So replace only in certain functions, or just global search and replace inside specific ranges.

# Actually it's safer to just patch line by line via replace_file_content
