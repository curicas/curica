/**
 * @file sqlite_module.h
 * @brief Header definitions for the Native SQLite integration.
 * 
 * Declares the initialization function `build_sqlite_constructor` which 
 * injects the synchronous SQLite Database class into the global JS environment.
 */
#ifndef SQLITE_MODULE_H
#define SQLITE_MODULE_H

#include "value.h"
#include "vm.h"

Value build_sqlite_constructor(VM* vm);

#endif
