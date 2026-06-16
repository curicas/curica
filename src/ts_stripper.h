/**
 * @file ts_stripper.h
 * @brief TypeScript Type Stripper Interface
 *
 * Provides a zero-overhead mechanism to strip TypeScript type annotations
 * directly from source code buffers before they are passed to the Curica compiler.
 */
#ifndef TS_STRIPPER_H
#define TS_STRIPPER_H

/**
 * @brief Strips TypeScript interfaces and type aliases from a source string in-place.
 * 
 * Replaces TypeScript-specific constructs (`interface Name { ... }`, `type Name = ...;`)
 * with whitespace to maintain exact line numbers and column offsets for accurate
 * stack traces, without requiring a heavy AST parsing pass.
 *
 * @param source The null-terminated source code string to be stripped in-place.
 */
void strip_typescript_types(char* source);

#endif
