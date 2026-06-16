#include <stdio.h>
#include "bytecode.h"
#include "compiler.h"
#include "alloc.h"

int main() {
    arena_init(16);
    CompiledProgram* prog = compile_source(
        "function Resource(id) { var obj = {}; obj.id = id; obj[Symbol.dispose] = function() { print(id); }; return obj; } function test() { using x = Resource(\"A\"); } test();"
    );
    return 0;
}
