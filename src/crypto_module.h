#ifndef CRYPTO_MODULE_H
#define CRYPTO_MODULE_H

#include "value.h"
#include "vm.h"
#include <stddef.h>
#include <stdint.h>

Value build_crypto_module(VM* vm);

// Internal C API for WebSockets
void crypto_sha1(const uint8_t* data, size_t len, uint8_t hash[20]);
char* crypto_base64_encode(const uint8_t* src, size_t len);

#endif
