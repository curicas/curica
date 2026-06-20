#include "bootstrapper.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

static char* read_entire_file_bs(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t read_bytes = fread(buf, 1, size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

char* bootstrap_environment(VM* vm, const char* json_path) {
    char* json_data = read_entire_file_bs(json_path);
    if (!json_data) {
        // If file doesn't exist, it's not a fatal error for some modes, but we return NULL to indicate it wasn't found.
        return NULL;
    }

    cJSON* root = cJSON_Parse(json_data);
    free(json_data);
    if (!root) {
        fprintf(stderr, "Error: Failed to parse JSON environment file: %s\n", json_path);
        return NULL;
    }

    // 1. Capability-Based Security Matrix
    cJSON* permissions = cJSON_GetObjectItemCaseSensitive(root, "permissions");
    if (permissions) {
        cJSON* allow_read = cJSON_GetObjectItemCaseSensitive(permissions, "allow_read");
        if (cJSON_IsTrue(allow_read)) vm->allow_read = true;
        
        cJSON* allow_write = cJSON_GetObjectItemCaseSensitive(permissions, "allow_write");
        if (cJSON_IsTrue(allow_write)) vm->allow_write = true;
        
        cJSON* allow_net = cJSON_GetObjectItemCaseSensitive(permissions, "allow_net");
        if (cJSON_IsTrue(allow_net)) vm->allow_net = true;
        
        cJSON* allow_run = cJSON_GetObjectItemCaseSensitive(permissions, "allow_run");
        if (cJSON_IsTrue(allow_run)) vm->allow_run = true;
        
        cJSON* allow_ffi = cJSON_GetObjectItemCaseSensitive(permissions, "allow_ffi");
        if (cJSON_IsTrue(allow_ffi)) vm->allow_ffi = true;
    }

    // 2. Env Variables
    cJSON* env = cJSON_GetObjectItemCaseSensitive(root, "env");
    if (env) {
        cJSON* current_env = NULL;
        cJSON_ArrayForEach(current_env, env) {
            if (cJSON_IsString(current_env)) {
                setenv(current_env->string, current_env->valuestring, 1);
            }
        }
    }

    // 3. Package Resolution Stub (Phase 3.2)
    cJSON* packages = cJSON_GetObjectItemCaseSensitive(root, "packages");
    if (packages) {
        cJSON* package = NULL;
        cJSON_ArrayForEach(package, packages) {
            if (cJSON_IsString(package)) {
                // TODO: Implement actual remote checking and compilation fallback here
                // printf("Resolving package: %s\n", package->valuestring);
            }
        }
    }

    // 4. Entrypoint
    cJSON* entrypoint = cJSON_GetObjectItemCaseSensitive(root, "entrypoint");
    char* entry_path = NULL;
    if (cJSON_IsString(entrypoint) && (entrypoint->valuestring != NULL)) {
        entry_path = strdup(entrypoint->valuestring);
    } else {
        entry_path = strdup("src/index.js"); // Default fallback
    }

    cJSON_Delete(root);
    return entry_path;
}
