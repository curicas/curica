#ifndef CURICA_WASI_MODULE_H
#define CURICA_WASI_MODULE_H

#include "vm.h"

typedef struct {
    char** dir_list;
    int dir_count;
    char** env_list;
    int env_count;
    char** arg_list;
    int arg_count;
} WASIConfig;

/**
 * Extracts WASIConfig* if the given value is a WASI import object marker.
 * Returns NULL if the object is not a valid WASI marker.
 */
WASIConfig* wasi_get_config_from_import(VM* vm, Value import_obj);

#endif // CURICA_WASI_MODULE_H
