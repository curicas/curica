import re

with open("src/vm.c", "r") as f:
    text = f.read()

text = text.replace("new_frame->env = f->env;\n    new_frame->prog = f->prog;", "new_frame->env = f->env;")
text = text.replace("uint32_t* pc = frame->prog->bytecode + frame->ip;", "const uint32_t* pc = vm->bytecode + frame->ip;")
text = text.replace("new_frame->coro = coro;\n        new_frame->prog = f->prog;", "new_frame->coro = coro;")
text = text.replace("new_frame->coro = frame->coro;\n        new_frame->prog = f->prog;", "new_frame->coro = frame->coro;")
text = text.replace("pc - frame->prog->bytecode", "pc - vm->bytecode")
text = text.replace("frame->prog->bytecode + frame->ip", "vm->bytecode + frame->ip")
text = text.replace("frame->prog->bytecode[frame->ip]", "vm->bytecode[frame->ip]")
text = text.replace("pc = frame->prog->bytecode + target_ip;", "pc = vm->bytecode + target_ip;")
text = text.replace("frame->prog->const_pool[bx]", "vm->const_pool[bx]")
text = text.replace("frame->prog->functions", "vm->functions")
text = text.replace("regs[a] = create_function(\n        frame->prog,\n        target_func->bytecode_offset,", "regs[a] = create_function(\n        target_func->bytecode_offset,")

with open("src/vm.c", "w") as f:
    f.write(text)

