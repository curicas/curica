import re

with open("src/vm.c", "r") as f:
    text = f.read()

# 1. new_frame->prog = f->prog; in vm_call (around line 156)
text = text.replace("new_frame->env = f->env;", "new_frame->env = f->env;\n    new_frame->prog = f->prog;")

# 2. In vm_run (starts around 1385), replace const uint32_t* pc = vm->bytecode + frame->ip;
text = text.replace("const uint32_t* pc = vm->bytecode + frame->ip;", "uint32_t* pc = frame->prog->bytecode + frame->ip;")

# 3. Inside OP_CALL logic where a new frame is pushed (coro branch)
text = text.replace("new_frame->coro = coro;", "new_frame->coro = coro;\n        new_frame->prog = f->prog;")

# 4. Inside OP_CALL logic where a new frame is pushed (normal branch)
text = text.replace("new_frame->coro = frame->coro;", "new_frame->coro = frame->coro;\n        new_frame->prog = f->prog;")

# 5. frame->ip = pc - vm->bytecode;
text = text.replace("pc - vm->bytecode", "pc - frame->prog->bytecode")

# 6. vm->bytecode + frame->ip
text = text.replace("vm->bytecode + frame->ip", "frame->prog->bytecode + frame->ip")

# 7. vm->bytecode[frame->ip]
text = text.replace("vm->bytecode[frame->ip]", "frame->prog->bytecode[frame->ip]")

# 8. pc = vm->bytecode + target_ip;
text = text.replace("pc = vm->bytecode + target_ip;", "pc = frame->prog->bytecode + target_ip;")

# 9. vm->const_pool[bx]
text = text.replace("vm->const_pool[bx]", "frame->prog->const_pool[bx]")

# 10. vm->functions
text = text.replace("vm->functions", "frame->prog->functions")

# 11. do_new_function create_function signature
text = text.replace("regs[a] = create_function(\n        target_func->bytecode_offset,", "regs[a] = create_function(\n        frame->prog,\n        target_func->bytecode_offset,")

with open("src/vm.c", "w") as f:
    f.write(text)

