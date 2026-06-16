#ifndef DGRAM_MODULE_H
#define DGRAM_MODULE_H

#include "vm.h"
#include "alloc.h"

/**
 * Builds the native `_dgram` module which provides low-level UDP socket bindings.
 */
Value build_dgram_module(VM* vm);

/**
 * Marks GC roots for active UDP sockets managed by the dgram module.
 */
void dgram_mark_gc_roots(GCTraceFn trace);

#endif /* DGRAM_MODULE_H */
