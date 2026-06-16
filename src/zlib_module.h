#ifndef ZLIB_MODULE_H
#define ZLIB_MODULE_H

#include "vm.h"
#include "alloc.h"

/**
 * Builds the native `_zlib` module which provides low-level compression.
 */
Value build_zlib_module(VM* vm);

/**
 * Marks GC roots for pending zlib async operations.
 */
void zlib_mark_gc_roots(GCTraceFn trace);

#endif /* ZLIB_MODULE_H */
