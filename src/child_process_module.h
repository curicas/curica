#ifndef CHILD_PROCESS_MODULE_H
#define CHILD_PROCESS_MODULE_H

#include "vm.h"

#include "alloc.h"
Value build_child_process_module(VM* vm);
void child_process_mark_gc_roots(GCTraceFn trace);

#endif
