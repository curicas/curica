/**
 * @file fs_module.h
 * @brief Synchronous File System Module.
 *
 * Declares the factory that builds the `fs` built-in module object,
 * intercepted by require('fs') before any filesystem resolution occurs.
 */
#ifndef FS_MODULE_H
#define FS_MODULE_H

#include "vm.h"

/**
 * Build and return the exports object for the built-in 'fs' module.
 * Contains synchronous POSIX wrappers (readFileSync, writeFileSync, etc.).
 * All failures are translated to SystemError via create_system_error().
 */
Value build_fs_module(VM* vm);

#endif // FS_MODULE_H
