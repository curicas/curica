/**
 * @file kv_store.c
 * @brief Persistent Key-Value Store
 * 
 * Exposes Curica.KV using a lightweight append-only log strategy with an in-memory index.
 */

#include "vm.h"
#include "alloc.h"
#include "builtins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// In-memory index node
typedef struct KVNode {
    char* key;
    char* value;
    struct KVNode* next;
} KVNode;

typedef struct {
    char filepath[256];
    FILE* file_handle;
    KVNode* head;
} KVStoreContext;

static void kv_set_mem(KVStoreContext* ctx, const char* key, const char* val) {
    KVNode* curr = ctx->head;
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            if (val) {
                free(curr->value);
                curr->value = strdup(val);
            } else {
                free(curr->value);
                curr->value = NULL;
            }
            return;
        }
        curr = curr->next;
    }
    if (val) {
        KVNode* node = malloc(sizeof(KVNode));
        node->key = strdup(key);
        node->value = strdup(val);
        node->next = ctx->head;
        ctx->head = node;
    }
}

static const char* kv_get_mem(KVStoreContext* ctx, const char* key) {
    KVNode* curr = ctx->head;
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            return curr->value;
        }
        curr = curr->next;
    }
    return NULL;
}

// Curica.KV.open(path)
static Value kv_open(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("open expects a file path", 24)));
        return VAL_UNDEFINED;
    }
    
    JSString* path_str = (JSString*)get_pointer(args[0]);
    KVStoreContext* ctx = arena_alloc(OBJ_OBJECT_DATA, sizeof(KVStoreContext));
    strncpy(ctx->filepath, path_str->data, 255);
    ctx->filepath[255] = '\0';
    ctx->head = NULL;
    
    // Open in append-plus mode (read/write, create if not exists)
    ctx->file_handle = fopen(ctx->filepath, "a+");
    if (!ctx->file_handle) {
        vm_throw_error(vm, create_error("SystemError", create_string("Failed to open KV store file", 28)));
        return VAL_UNDEFINED;
    }
    
    // Load existing data to build the in-memory index
    fseek(ctx->file_handle, 0, SEEK_SET);
    char type;
    while (fread(&type, 1, 1, ctx->file_handle) == 1) {
        if (type == '\n') continue;
        if (type != 'S' && type != 'D') break;
        
        int k_len = 0, v_len = 0;
        if (fscanf(ctx->file_handle, " %d %d ", &k_len, &v_len) != 2) break;
        
        char* key = malloc(k_len + 1);
        if (fread(key, 1, k_len, ctx->file_handle) != (size_t)k_len) { free(key); break; }
        key[k_len] = '\0';
        
        if (type == 'S') {
            char* val = malloc(v_len + 1);
            if (fread(val, 1, v_len, ctx->file_handle) != (size_t)v_len) { free(key); free(val); break; }
            val[v_len] = '\0';
            kv_set_mem(ctx, key, val);
            free(val);
        } else {
            kv_set_mem(ctx, key, NULL);
        }
        free(key);
    }
    
    // Ensure we are at the end for appending
    fseek(ctx->file_handle, 0, SEEK_END);
    
    Value js_kv = create_object();
    object_set(js_kv, create_string("_ctx", 4), make_pointer(ctx));
    
    return js_kv;
}

// Curica.KV.set(kv_instance, key, value)
static Value kv_set(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 3 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    Value ctx_val = object_get(args[0], create_string("_ctx", 4));
    if (!IS_POINTER(ctx_val)) return VAL_UNDEFINED;
    
    KVStoreContext* ctx = (KVStoreContext*)get_pointer(ctx_val);
    JSString* key_str = (JSString*)get_pointer(args[1]);
    
    // Stringify value
    Value json_str_val = js_json_stringify(vm, VAL_UNDEFINED, 1, &args[2]);
    if (!IS_POINTER(json_str_val)) return VAL_UNDEFINED;
    
    JSString* val_str = (JSString*)get_pointer(json_str_val);
    
    fprintf(ctx->file_handle, "S %d %d ", key_str->length, val_str->length);
    fwrite(key_str->data, 1, key_str->length, ctx->file_handle);
    fwrite(val_str->data, 1, val_str->length, ctx->file_handle);
    fprintf(ctx->file_handle, "\n");
    fflush(ctx->file_handle);
    
    kv_set_mem(ctx, key_str->data, val_str->data);
    
    return VAL_TRUE;
}

// Curica.KV.get(kv_instance, key)
static Value kv_get(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    Value ctx_val = object_get(args[0], create_string("_ctx", 4));
    if (!IS_POINTER(ctx_val)) return VAL_UNDEFINED;
    
    KVStoreContext* ctx = (KVStoreContext*)get_pointer(ctx_val);
    JSString* key_str = (JSString*)get_pointer(args[1]);
    
    const char* val_str = kv_get_mem(ctx, key_str->data);
    if (val_str) {
        Value str_val = create_string(val_str, strlen(val_str));
        return js_json_parse(vm, VAL_UNDEFINED, 1, &str_val);
    }
    
    return VAL_NULL;
}

// Curica.KV.delete(kv_instance, key)
static Value kv_delete(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    Value ctx_val = object_get(args[0], create_string("_ctx", 4));
    if (!IS_POINTER(ctx_val)) return VAL_UNDEFINED;
    
    KVStoreContext* ctx = (KVStoreContext*)get_pointer(ctx_val);
    JSString* key_str = (JSString*)get_pointer(args[1]);
    
    fprintf(ctx->file_handle, "D %d 0 ", key_str->length);
    fwrite(key_str->data, 1, key_str->length, ctx->file_handle);
    fprintf(ctx->file_handle, "\n");
    fflush(ctx->file_handle);
    
    kv_set_mem(ctx, key_str->data, NULL);
    
    return VAL_TRUE;
}

// Curica.KV.compact(kv_instance)
static Value kv_compact(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    Value ctx_val = object_get(args[0], create_string("_ctx", 4));
    if (!IS_POINTER(ctx_val)) return VAL_UNDEFINED;
    
    KVStoreContext* ctx = (KVStoreContext*)get_pointer(ctx_val);
    
    if (ctx->file_handle) {
        fclose(ctx->file_handle);
    }
    
    char tmp_filepath[512];
    snprintf(tmp_filepath, sizeof(tmp_filepath), "%s.tmp", ctx->filepath);
    
    FILE* tmp_file = fopen(tmp_filepath, "w");
    if (!tmp_file) {
        vm_throw_error(vm, create_error("SystemError", create_string("Failed to open temp file for compaction", 39)));
        return VAL_UNDEFINED;
    }
    
    KVNode* curr = ctx->head;
    while (curr) {
        if (curr->value) {
            int key_len = strlen(curr->key);
            int val_len = strlen(curr->value);
            fprintf(tmp_file, "S %d %d ", key_len, val_len);
            fwrite(curr->key, 1, key_len, tmp_file);
            fwrite(curr->value, 1, val_len, tmp_file);
            fprintf(tmp_file, "\n");
        }
        curr = curr->next;
    }
    fclose(tmp_file);
    
    rename(tmp_filepath, ctx->filepath);
    
    ctx->file_handle = fopen(ctx->filepath, "a+");
    if (!ctx->file_handle) {
        vm_throw_error(vm, create_error("SystemError", create_string("Failed to reopen KV file", 24)));
        return VAL_UNDEFINED;
    }
    
    return VAL_TRUE;
}

void vm_register_kv_store(VM* vm) {
    Value kv_obj = create_object();
    
    object_set(kv_obj, create_string("open", 4), create_native_function((void*)kv_open, create_string("open", 4)));
    object_set(kv_obj, create_string("set", 3), create_native_function((void*)kv_set, create_string("set", 3)));
    object_set(kv_obj, create_string("get", 3), create_native_function((void*)kv_get, create_string("get", 3)));
    object_set(kv_obj, create_string("delete", 6), create_native_function((void*)kv_delete, create_string("delete", 6)));
    object_set(kv_obj, create_string("compact", 7), create_native_function((void*)kv_compact, create_string("compact", 7)));
    
    Value curica_obj = object_get(vm->global_obj, create_string("Curica", 6));
    if (!IS_POINTER(curica_obj)) {
        curica_obj = create_object();
        object_set(vm->global_obj, create_string("Curica", 6), curica_obj);
    }
    
    object_set(curica_obj, create_string("KV", 2), kv_obj);
}
