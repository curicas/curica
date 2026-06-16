import sys

with open("src/vm.c", "r") as f:
    lines = f.readlines()

def insert_after_line(match, insertion):
    for i in range(len(lines)):
        if match in lines[i]:
            lines.insert(i + 1, insertion + "\n")
            return
    print(f"Error: {match} not found!")
    sys.exit(1)

def insert_before_line(match, insertion):
    for i in range(len(lines)):
        if match in lines[i]:
            lines.insert(i, insertion + "\n")
            return
    print(f"Error: {match} not found!")
    sys.exit(1)

def replace_line_containing(match, replacement):
    for i in range(len(lines)):
        if match in lines[i]:
            lines[i] = replacement + "\n"
            return
    print(f"Error: {match} not found!")
    sys.exit(1)

insert_before_line('Value vm_call_function(VM* vm, Value func_val, int arg_count, Value* args) {', """static void vm_switch_program(VM* vm, struct CompiledProgram* prog) {
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
""")

insert_after_line('CallFrame* new_frame = &vm->frames[vm->frame_count++];', '    new_frame->prog = f->prog;\n    vm_switch_program(vm, f->prog);')
insert_after_line('CallFrame* new_frame = &coro->frames[0];', '        new_frame->prog = f->prog;')

for i in range(len(lines)):
    if 'frame = &vm->frames[vm->frame_count - 1];' in lines[i]:
        lines[i] = lines[i].replace('frame = &vm->frames[vm->frame_count - 1];', 'frame = &vm->frames[vm->frame_count - 1]; vm_switch_program(vm, frame->prog);')
    if 'frame = &coro->frames[coro->frame_count - 1];' in lines[i]:
        lines[i] = lines[i].replace('frame = &coro->frames[coro->frame_count - 1];', 'frame = &coro->frames[coro->frame_count - 1]; vm_switch_program(vm, frame->prog);')
    if 'frame = new_frame;' in lines[i] and 'regs =' in lines[i+1]:
        lines[i] = lines[i] + '        vm_switch_program(vm, frame->prog);\n'

replace_line_containing('regs[a] = create_function(', '    regs[a] = create_function(frame->prog, target_func->bytecode_offset, target_func->register_count, target_func->param_count, target_func->is_async, make_pointer(get_pointer(frame->env)), name);')
replace_line_containing('target_func->bytecode_offset,', '')
replace_line_containing('target_func->register_count,', '')
replace_line_containing('target_func->param_count,', '')
replace_line_containing('target_func->is_async,', '')
replace_line_containing('make_pointer(get_pointer(frame->env)), // Set current closure lexical environment', '')
replace_line_containing('name', '')

with open("src/vm.c", "w") as f:
    f.writelines(lines)

