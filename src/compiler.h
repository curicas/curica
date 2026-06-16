/**
 * @file compiler.h
 * @brief Compiler and AST definitions.
 * 
 * Defines the CompiledProgram, CompilerFuncInfo, and serialization boundaries for AOT.
 */
#ifndef COMPILER_H
#define COMPILER_H

#include <stdint.h>
#include "value.h"

// Metadata for a compiled function
typedef struct {
    uint32_t bytecode_offset; // Starting index in the flattened instruction array
    uint32_t register_count;  // Total registers needed for execution frame
    uint32_t param_count;     // Parameter count expected
    bool is_async;            // True if declared as async function
} CompilerFuncInfo;

// Representation of the compiled output
typedef struct CompiledProgram {
    Value* const_pool;
    uint32_t const_pool_size;
    
    uint32_t* bytecode;
    uint32_t bytecode_size;
    
    uint32_t* ic_table;
    
    CompilerFuncInfo* functions;
    uint32_t function_count;
} CompiledProgram;

// Main compiler API
CompiledProgram* compile_source(const char* source);
void free_compiled_program(CompiledProgram* prog);

// Serializer / Deserializer for AOT/CBC files
uint8_t* serialize_program(const CompiledProgram* prog, uint32_t* out_size);
CompiledProgram* deserialize_program(const uint8_t* data, uint32_t size);

#endif // COMPILER_H
