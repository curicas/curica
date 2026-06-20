#pragma once
#include "vm.h"

/**
 * @brief Bootstraps the environment from curica.env.json
 * 
 * Populates VM capabilities (allow_read, allow_net, etc.), environment variables, 
 * and handles early-stage package resolution setup.
 * 
 * @param vm The VM instance to configure.
 * @param json_path The path to the environment JSON file (e.g., "curica.env.json" or "/zip/curica.env.json")
 * @return char* The entrypoint path specified in the JSON, or a default string. Must be free()'d. Returns NULL on hard failure.
 */
char* bootstrap_environment(VM* vm, const char* json_path);
