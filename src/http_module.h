#ifndef HTTP_MODULE_H
#define HTTP_MODULE_H

#include "value.h"
#include "vm.h"

#include "alloc.h"
Value build_http_module(struct VM* vm);
void http_mark_gc_roots(GCTraceFn trace);

#endif
