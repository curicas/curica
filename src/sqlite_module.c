/**
 * @file sqlite_module.c
 * @brief Native SQLite bindings for the Curica Runtime.
 *
 * Implements component logic for the Curica Environment OS Kernel.
 * Curica is a secure microkernel OS that employs a strict POSIX Virtual File System (VFS)
 * with /bin, /home/user, and pseudo-filesystems (/dev, /proc). It uses JS natively as the
 * systems shell scripting language to pipe I/O and spawn WASM processes, enforcing
 * capability-based security (allow_run, allow_net, allow_read, allow_write, allow_ffi).
 * Furthermore, the kernel freezes environments into Actually Portable Executables (APEs)
 * and features Source Compilation Fallback, Virtual Networking Mocking, and
 * Foreign Sandbox IPC attached.
 */
#include "sqlite_module.h"
#include "builtins.h"
#include "alloc.h"
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static Value js_sqlite_prepare(VM* vm, Value this_val, int arg_count, Value* args);
static Value js_sqlite_exec(VM* vm, Value this_val, int arg_count, Value* args);
static Value js_sqlite_close(VM* vm, Value this_val, int arg_count, Value* args);

static Value js_stmt_run(VM* vm, Value this_val, int arg_count, Value* args);
static Value js_stmt_get(VM* vm, Value this_val, int arg_count, Value* args);
static Value js_stmt_all(VM* vm, Value this_val, int arg_count, Value* args);
static Value js_stmt_finalize(VM* vm, Value this_val, int arg_count, Value* args);

// ── Database ──
static Value js_database_constructor(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        // throw Error
        return VAL_UNDEFINED;
    }
    JSString* filename = (JSString*)get_pointer(args[0]);

    sqlite3* db = NULL;
    int rc = sqlite3_open(filename->data, &db);
    if (rc != SQLITE_OK) {
        printf("SQLite Error: %s\n", sqlite3_errmsg(db));
        Value err = create_system_error(vm, rc, "sqlite3_open", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        vm_throw_error(vm, err);
        return VAL_UNDEFINED;
    }

    // Cache the this_val in the GC roots array explicitly.
    // This protects it from movement when create_string or object_set trigger nursery GC.
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);

    uint32_t db_key_idx = vm->gc_root_count;
    Value db_key = create_string("_db", 3);
    vm_push_root(vm, db_key);

    // Store the raw sqlite3* pointer in a tagged NaN-boxed value using TAG_POINTER.
    // The VM allocator will explicitly ignore tracking this memory as it sits outside the Arena.
    object_set(vm->gc_roots[this_idx], vm->gc_roots[db_key_idx], make_pointer((void*)db));
    vm_pop_root(vm); // db_key

    // Bind prepared methods safely
    uint32_t prep_name_idx = vm->gc_root_count;
    Value prep_name = create_string("prepare", 7);
    vm_push_root(vm, prep_name);
    
    uint32_t prep_fn_idx = vm->gc_root_count;
    Value prep_fn = create_bound_native_function((void*)js_sqlite_prepare, vm->gc_roots[prep_name_idx], vm->gc_roots[this_idx]);
    vm_push_root(vm, prep_fn);
    
    object_set(vm->gc_roots[this_idx], vm->gc_roots[prep_name_idx], vm->gc_roots[prep_fn_idx]);
    vm_pop_root(vm); // prep_fn
    vm_pop_root(vm); // prep_name

    uint32_t exec_name_idx = vm->gc_root_count;
    Value exec_name = create_string("exec", 4);
    vm_push_root(vm, exec_name);
    
    uint32_t exec_fn_idx = vm->gc_root_count;
    Value exec_fn = create_bound_native_function((void*)js_sqlite_exec, vm->gc_roots[exec_name_idx], vm->gc_roots[this_idx]);
    vm_push_root(vm, exec_fn);
    
    object_set(vm->gc_roots[this_idx], vm->gc_roots[exec_name_idx], vm->gc_roots[exec_fn_idx]);
    vm_pop_root(vm); // exec_fn
    vm_pop_root(vm); // exec_name

    uint32_t close_name_idx = vm->gc_root_count;
    Value close_name = create_string("close", 5);
    vm_push_root(vm, close_name);
    
    uint32_t close_fn_idx = vm->gc_root_count;
    Value close_fn = create_bound_native_function((void*)js_sqlite_close, vm->gc_roots[close_name_idx], vm->gc_roots[this_idx]);
    vm_push_root(vm, close_fn);
    
    object_set(vm->gc_roots[this_idx], vm->gc_roots[close_name_idx], vm->gc_roots[close_fn_idx]);
    vm_pop_root(vm); // close_fn
    vm_pop_root(vm); // close_name
    
    Value ret_val = vm->gc_roots[this_idx];
    vm_pop_root(vm); // this_val

    return ret_val;
}

static Value js_sqlite_close(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)arg_count; (void)args;
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t db_key_idx = vm->gc_root_count;
    Value db_key = create_string("_db", 3);
    vm_push_root(vm, db_key);
    
    Value db_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[db_key_idx]);
    
    if (IS_POINTER(db_val)) {
        sqlite3* db = (sqlite3*)get_pointer(db_val);
        if (db) sqlite3_close(db);
        object_set(vm->gc_roots[this_idx], vm->gc_roots[db_key_idx], VAL_UNDEFINED);
    }
    
    vm_pop_root(vm); // db_key
    vm_pop_root(vm); // this_val
    return VAL_UNDEFINED;
}

static Value js_sqlite_exec(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t db_key_idx = vm->gc_root_count;
    Value db_key = create_string("_db", 3);
    vm_push_root(vm, db_key);
    
    Value db_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[db_key_idx]);
    vm_pop_root(vm); // db_key
    
    if (!IS_POINTER(db_val)) {
        vm_pop_root(vm); // this_val
        return VAL_UNDEFINED;
    }
    sqlite3* db = (sqlite3*)get_pointer(db_val);

    JSString* sql = (JSString*)get_pointer(args[0]);
    char* err_msg = NULL;
    int rc = sqlite3_exec(db, sql->data, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        printf("SQLite Exec Error: %s\n", err_msg);
        Value err = create_system_error(vm, rc, "sqlite3_exec", err_msg);
        sqlite3_free(err_msg);
        vm_pop_root(vm); // this_val
        vm_throw_error(vm, err);
        return VAL_UNDEFINED;
    }
    
    Value ret_val = vm->gc_roots[this_idx];
    vm_pop_root(vm); // this_val
    return ret_val;
}

// ── Statement ──
static Value js_sqlite_prepare(VM* vm, Value this_val, int arg_count, Value* args) {
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* sql = (JSString*)get_pointer(args[0]);

    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t db_key_idx = vm->gc_root_count;
    Value db_key = create_string("_db", 3);
    vm_push_root(vm, db_key);
    
    Value db_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[db_key_idx]);
    vm_pop_root(vm); // db_key
    
    if (!IS_POINTER(db_val)) {
        vm_pop_root(vm); // this_val
        return VAL_UNDEFINED;
    }
    sqlite3* db = (sqlite3*)get_pointer(db_val);

    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql->data, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        printf("SQLite Prepare Error: %s\n", sqlite3_errmsg(db));
        Value err = create_system_error(vm, rc, "sqlite3_prepare_v2", sqlite3_errmsg(db));
        vm_pop_root(vm); // this_val
        vm_throw_error(vm, err);
        return VAL_UNDEFINED;
    }

    uint32_t stmt_obj_idx = vm->gc_root_count;
    Value stmt_obj = create_object();
    vm_push_root(vm, stmt_obj);
    
    uint32_t _stmt_key_idx = vm->gc_root_count;
    Value _stmt_key = create_string("_stmt", 5);
    vm_push_root(vm, _stmt_key);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[_stmt_key_idx], make_pointer((void*)stmt));
    vm_pop_root(vm); // _stmt_key

    uint32_t db_prop_key_idx = vm->gc_root_count;
    Value db_prop_key = create_string("db", 2);
    vm_push_root(vm, db_prop_key);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[db_prop_key_idx], vm->gc_roots[this_idx]);
    vm_pop_root(vm); // db_prop_key

    // run
    uint32_t run_name_idx = vm->gc_root_count;
    Value run_name = create_string("run", 3);
    vm_push_root(vm, run_name);
    
    uint32_t run_fn_idx = vm->gc_root_count;
    Value run_fn = create_bound_native_function((void*)js_stmt_run, vm->gc_roots[run_name_idx], vm->gc_roots[stmt_obj_idx]);
    vm_push_root(vm, run_fn);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[run_name_idx], vm->gc_roots[run_fn_idx]);
    vm_pop_root(vm); // run_fn
    vm_pop_root(vm); // run_name

    // get
    uint32_t get_name_idx = vm->gc_root_count;
    Value get_name = create_string("get", 3);
    vm_push_root(vm, get_name);
    
    uint32_t get_fn_idx = vm->gc_root_count;
    Value get_fn = create_bound_native_function((void*)js_stmt_get, vm->gc_roots[get_name_idx], vm->gc_roots[stmt_obj_idx]);
    vm_push_root(vm, get_fn);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[get_name_idx], vm->gc_roots[get_fn_idx]);
    vm_pop_root(vm); // get_fn
    vm_pop_root(vm); // get_name

    // all
    uint32_t all_name_idx = vm->gc_root_count;
    Value all_name = create_string("all", 3);
    vm_push_root(vm, all_name);
    
    uint32_t all_fn_idx = vm->gc_root_count;
    Value all_fn = create_bound_native_function((void*)js_stmt_all, vm->gc_roots[all_name_idx], vm->gc_roots[stmt_obj_idx]);
    vm_push_root(vm, all_fn);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[all_name_idx], vm->gc_roots[all_fn_idx]);
    vm_pop_root(vm); // all_fn
    vm_pop_root(vm); // all_name

    // finalize
    uint32_t fin_name_idx = vm->gc_root_count;
    Value fin_name = create_string("finalize", 8);
    vm_push_root(vm, fin_name);
    
    uint32_t fin_fn_idx = vm->gc_root_count;
    Value fin_fn = create_bound_native_function((void*)js_stmt_finalize, vm->gc_roots[fin_name_idx], vm->gc_roots[stmt_obj_idx]);
    vm_push_root(vm, fin_fn);
    
    object_set(vm->gc_roots[stmt_obj_idx], vm->gc_roots[fin_name_idx], vm->gc_roots[fin_fn_idx]);
    vm_pop_root(vm); // fin_fn
    vm_pop_root(vm); // fin_name

    Value ret_val = vm->gc_roots[stmt_obj_idx];
    vm_pop_root(vm); // stmt_obj
    vm_pop_root(vm); // this_val
    return ret_val;
}

static void bind_args(sqlite3_stmt* stmt, int arg_count, Value* args) {
    sqlite3_clear_bindings(stmt);
    for (int i = 0; i < arg_count; i++) {
        if (IS_UNDEFINED(args[i]) || IS_NULL(args[i])) {
            sqlite3_bind_null(stmt, i + 1);
        } else if (IS_DOUBLE(args[i])) {
            sqlite3_bind_double(stmt, i + 1, get_double(args[i]));
        } else if (IS_BOOLEAN(args[i])) {
            sqlite3_bind_int(stmt, i + 1, get_boolean(args[i]));
        } else if (IS_POINTER(args[i])) {
            // Convert to string using builtins (mocked manually here if simple string)
            JSString* str = (JSString*)get_pointer(args[i]);
            sqlite3_bind_text(stmt, i + 1, str->data, -1, SQLITE_TRANSIENT);
        }
    }
}

static Value extract_row(VM* vm, sqlite3_stmt* stmt) {
    int cols = sqlite3_column_count(stmt);
    uint32_t obj_idx = vm->gc_root_count;
    Value obj = create_object();
    vm_push_root(vm, obj);
    
    for (int i = 0; i < cols; i++) {
        const char* name = sqlite3_column_name(stmt, i);
        int type = sqlite3_column_type(stmt, i);
        
        uint32_t key_idx = vm->gc_root_count;
        Value key = create_string(name, strlen(name));
        vm_push_root(vm, key);
        
        uint32_t val_idx = vm->gc_root_count;
        Value val = VAL_UNDEFINED;
        switch (type) {
            case SQLITE_INTEGER:
                val = make_double((double)sqlite3_column_int64(stmt, i));
                break;
            case SQLITE_FLOAT:
                val = make_double(sqlite3_column_double(stmt, i));
                break;
            case SQLITE_TEXT: {
                const char* txt = (const char*)sqlite3_column_text(stmt, i);
                val = create_string(txt, strlen(txt));
                break;
            }
            case SQLITE_NULL:
                val = VAL_NULL;
                break;
            case SQLITE_BLOB:
                // Skip blobs for simple implementation
                val = VAL_NULL;
                break;
        }
        vm_push_root(vm, val);
        
        object_set(vm->gc_roots[obj_idx], vm->gc_roots[key_idx], vm->gc_roots[val_idx]);
        
        vm_pop_root(vm); // val
        vm_pop_root(vm); // key
    }
    
    Value ret_val = vm->gc_roots[obj_idx];
    vm_pop_root(vm); // obj
    return ret_val;
}

static Value js_stmt_run(VM* vm, Value this_val, int arg_count, Value* args) {
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t stmt_key_idx = vm->gc_root_count;
    Value stmt_key = create_string("_stmt", 5);
    vm_push_root(vm, stmt_key);
    
    Value stmt_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[stmt_key_idx]);
    vm_pop_root(vm); // stmt_key
    
    if (!IS_POINTER(stmt_val)) {
        vm_pop_root(vm);
        return VAL_UNDEFINED;
    }
    sqlite3_stmt* stmt = (sqlite3_stmt*)get_pointer(stmt_val);

    bind_args(stmt, arg_count, args);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    
    // Return an object with lastInsertRowid and changes
    sqlite3* db = sqlite3_db_handle(stmt);
    
    uint32_t res_idx = vm->gc_root_count;
    Value res = create_object();
    vm_push_root(vm, res);
    
    uint32_t k1_idx = vm->gc_root_count;
    Value k1 = create_string("lastInsertRowid", 15);
    vm_push_root(vm, k1);
    object_set(vm->gc_roots[res_idx], vm->gc_roots[k1_idx], make_double((double)sqlite3_last_insert_rowid(db)));
    vm_pop_root(vm); // k1
    
    uint32_t k2_idx = vm->gc_root_count;
    Value k2 = create_string("changes", 7);
    vm_push_root(vm, k2);
    object_set(vm->gc_roots[res_idx], vm->gc_roots[k2_idx], make_double((double)sqlite3_changes(db)));
    vm_pop_root(vm); // k2
    
    Value ret_val = vm->gc_roots[res_idx];
    vm_pop_root(vm); // res
    vm_pop_root(vm); // this_val
    return ret_val;
}

static Value js_stmt_get(VM* vm, Value this_val, int arg_count, Value* args) {
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t stmt_key_idx = vm->gc_root_count;
    Value stmt_key = create_string("_stmt", 5);
    vm_push_root(vm, stmt_key);
    
    Value stmt_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[stmt_key_idx]);
    vm_pop_root(vm); // stmt_key
    
    if (!IS_POINTER(stmt_val)) {
        vm_pop_root(vm);
        return VAL_UNDEFINED;
    }
    sqlite3_stmt* stmt = (sqlite3_stmt*)get_pointer(stmt_val);

    bind_args(stmt, arg_count, args);
    Value row = VAL_UNDEFINED;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        row = extract_row(vm, stmt);
    }
    sqlite3_reset(stmt);
    
    vm_pop_root(vm); // this_val
    return row;
}

static Value js_stmt_all(VM* vm, Value this_val, int arg_count, Value* args) {
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t stmt_key_idx = vm->gc_root_count;
    Value stmt_key = create_string("_stmt", 5);
    vm_push_root(vm, stmt_key);
    
    Value stmt_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[stmt_key_idx]);
    vm_pop_root(vm); // stmt_key
    
    if (!IS_POINTER(stmt_val)) {
        vm_pop_root(vm);
        return VAL_UNDEFINED;
    }
    sqlite3_stmt* stmt = (sqlite3_stmt*)get_pointer(stmt_val);

    bind_args(stmt, arg_count, args);
    
    // Allocate the dynamic array safely via the VM. array_push will automatically
    // handle capacity expansion over the VM arena without manual libc realloc calls,
    // which protects against segment faults during GC sweeps.
    uint32_t arr_idx = vm->gc_root_count;
    Value arr = create_array(8);
    vm_push_root(vm, arr);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        // extract_row triggers GC. Must rely purely on gc_roots array references!
        uint32_t row_idx = vm->gc_root_count;
        Value row = extract_row(vm, stmt);
        vm_push_root(vm, row);
        
        // Push the completed row object onto the array
        array_push(vm->gc_roots[arr_idx], vm->gc_roots[row_idx]);
        vm_pop_root(vm); // row
    }
    sqlite3_reset(stmt);
    
    Value ret_val = vm->gc_roots[arr_idx];
    vm_pop_root(vm); // arr
    vm_pop_root(vm); // this_val
    return ret_val;
}

static Value js_stmt_finalize(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)arg_count; (void)args;
    uint32_t this_idx = vm->gc_root_count;
    vm_push_root(vm, this_val);
    
    uint32_t stmt_key_idx = vm->gc_root_count;
    Value stmt_key = create_string("_stmt", 5);
    vm_push_root(vm, stmt_key);
    
    Value stmt_val = object_get(vm->gc_roots[this_idx], vm->gc_roots[stmt_key_idx]);
    
    if (IS_POINTER(stmt_val)) {
        sqlite3_stmt* stmt = (sqlite3_stmt*)get_pointer(stmt_val);
        if (stmt) sqlite3_finalize(stmt);
        object_set(vm->gc_roots[this_idx], vm->gc_roots[stmt_key_idx], VAL_UNDEFINED);
    }
    
    vm_pop_root(vm); // stmt_key
    vm_pop_root(vm); // this_val
    return VAL_UNDEFINED;
}

Value build_sqlite_constructor(VM* vm) {
    (void)vm;
    Value ctor = create_native_function((void*)js_database_constructor, create_string("Database", 8));
    return ctor;
}
