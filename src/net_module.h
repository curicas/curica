/**
 * @file net_module.h
 * @brief TCP Networking Built-in Module.
 *
 * Declares the factory for the 'net' built-in module, intercepted by
 * require('net') before any filesystem resolution occurs.
 */
#ifndef NET_MODULE_H
#define NET_MODULE_H

#include "vm.h"

/**
 * Build and return the exports object for the built-in 'net' module.
 * Contains net.createServer() and net.connect().
 */
Value build_net_module(VM* vm);

#endif /* NET_MODULE_H */
