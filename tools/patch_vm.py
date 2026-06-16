import sys

with open("src/vm.c", "r") as f:
    lines = f.readlines()

def insert_after(target, content):
    for i, line in enumerate(lines):
        if target in line:
            lines.insert(i + 1, content + "\n")
            return
    print(f"Error: Could not find '{target}'")
    sys.exit(1)

def replace_line(target, content):
    for i, line in enumerate(lines):
        if target in line:
            lines[i] = content + "\n"
            return
    print(f"Error: Could not find '{target}'")
    sys.exit(1)

# Add vm_switch_program
switch_prog = """static void vm_switch_program(VM* vm, struct CompiledProgram* prog) {
    if (prog && vm->current_prog != prog) {
        vm->current_prog = prog;
        vm->const_pool = prog->const_pool;
        vm->const_pool_size = prog->const_pool_size;
        vm->bytecode = prog->bytecode;
        vm->bytecode_size = prog->bytecode_size;
        vm->functions = prog->functions;
        vm->function_count = prog->function_count;
    }
}
"""
insert_after('Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args) {', switch_prog)

# In vm_call_function
insert_after('CallFrame* new_frame = &vm->frames[vm->frame_count++];', '    new_frame->prog = f->prog;\n    vm_switch_program(vm, f->prog);')

# In OP_CALL coro branch
insert_after('CallFrame* new_frame = &coro->frames[0];', '        new_frame->prog = f->prog;')

# In OP_CALL normal branch
insert_after('CallFrame* new_frame = &vm->frames[vm->frame_count++];', '        new_frame->prog = f->prog;')
insert_after('frame = new_frame;', '        vm_switch_program(vm, frame->prog);')

# In do_return
insert_after('frame = &vm->frames[vm->frame_count - 1];', '    vm_switch_program(vm, frame->prog);')

# In do_new_function
replace_line('regs[a] = create_function(', '    regs[a] = create_function(frame->prog, target_func->bytecode_offset, target_func->register_count, target_func->param_count, target_func->is_async, make_pointer(get_pointer(frame->env)), name);')
replace_line('target_func->bytecode_offset,', '')
replace_line('target_func->register_count,', '')
replace_line('target_func->param_count,', '')
replace_line('target_func->is_async,', '')
replace_line('make_pointer(get_pointer(frame->env)), // Set current closure lexical environment', '')
replace_line('name', '')

# In OP_AWAIT do_yield
insert_after('frame = &vm->frames[vm->frame_count - 1];', '        vm_switch_program(vm, frame->prog);')

# In OP_AWAIT (resume from yield)
insert_after('frame = &coro->frames[coro->frame_count - 1];', '    vm_switch_program(vm, frame->prog);')

with open("src/vm.c", "w") as f:
    f.writelines(lines)
