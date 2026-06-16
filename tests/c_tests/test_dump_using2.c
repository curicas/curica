#include <stdio.h>
#include "bytecode.h"
#include "compiler.h"
#include "alloc.h"

int main() {
    arena_init(16);
    CompiledProgram* prog = compile_source(
        "function Resource(id) { var obj = {}; obj.id = id; obj[Symbol.dispose] = function() { print(id); }; return obj; } function test() { using x = Resource(\"A\"); } test();"
    );
    for(uint32_t i=0; i<prog->bytecode_size; i++) {
        uint32_t inst = prog->bytecode[i];
        printf("%d: OP=%d A=%d B=%d C=%d\n", i, INST_OP(inst), INST_A(inst), INST_B(inst), INST_C(inst));
    }
    return 0;
}
