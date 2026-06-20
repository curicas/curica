/**
 * @file fs_module.c
 * @brief strict POSIX Virtual File System (VFS) Integration for Curica.
 *
 * Implements file system capabilities for the Curica Environment OS Kernel. 
 * Sync operations run on the calling thread, while async operations submit 
 * WorkItems to the thread pool to avoid blocking the microkernel OS.
 *
 * This subsystem supports JS natively as the systems shell scripting language 
 * to manipulate files in /bin, /home/user, and pseudo-filesystems (/dev, /proc) 
 * while piping I/O to spawned WASM processes. All access is governed by the 
 * strict Capability-Based Security matrix (zero-bloat validation without UIDs/GIDs), 
 * crucial for securely running frozen environments from Actually Portable 
 * Executables (APEs).
 */
#include "fs_module.h"
#include "fs_module.h"
#include "alloc.h"
#include "event_loop.h"
#include "vfs_module.h"
#include "vm.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <dirent.h>

#define VFS_OPEN(path, flags, mode) vfs_open(path, flags, mode)
#define VFS_STAT(path, st) vfs_stat(path, st)
#define VFS_LSTAT(path, st) vfs_lstat(path, st)
#define VFS_MKDIR(path, mode) vfs_mkdir(path, mode)
#define VFS_UNLINK(path) vfs_unlink(path)
#define VFS_RENAME(oldp, newp) vfs_rename(oldp, newp)
#define VFS_OPENDIR(path) vfs_opendir(path)



extern Value js_promise_resolve_func(VM* vm, Value this_val, int arg_count, Value* args);
extern Value js_promise_reject_func(VM* vm, Value this_val, int arg_count, Value* args);
extern void gc_mark_value(Value val);

#define ENFORCE_READ_ACCESS(vm) \
    if (!(vm)->allow_read) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-read access", 28))); \
        return VAL_UNDEFINED; \
    }

#define ENFORCE_WRITE_ACCESS(vm) \
    if (!(vm)->allow_write) { \
        vm_throw_error((vm), create_error("PermissionError", create_string("Requires --allow-write access", 29))); \
        return VAL_UNDEFINED; \
    }

// ---------------------------------------------------------------------------
// Helper: extract encoding string from an options argument (string or object)
// Returns a malloc'd string that the caller must NOT free (static lifetime).
// ---------------------------------------------------------------------------
static const char* extract_encoding(Value opts) {
    if (!IS_POINTER(opts)) return NULL;
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(opts) - sizeof(BlockHeader));
    if (h->obj_type == OBJ_STRING) {
        return ((JSString*)get_pointer(opts))->data;
    }
    if (h->obj_type == OBJ_OBJECT) {
        Value enc_key = create_string("encoding", 8);
        Value enc_val = object_get(opts, enc_key);
        if (IS_POINTER(enc_val)) {
            BlockHeader* eh = (BlockHeader*)((char*)get_pointer(enc_val) - sizeof(BlockHeader));
            if (eh->obj_type == OBJ_STRING) {
                return ((JSString*)get_pointer(enc_val))->data;
            }
        }
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// fs.readFileSync(path[, options])
// Returns a Buffer (default) or String if encoding is specified.
// ---------------------------------------------------------------------------
static Value js_fs_read_file_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("path must be a string", 21)));
        return VAL_UNDEFINED;
    }
    JSString* path_str = (JSString*)get_pointer(args[0]);
    const char* encoding = (arg_count >= 2) ? extract_encoding(args[1]) : NULL;

    int fd = VFS_OPEN(path_str->data, O_RDONLY, 0666);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "open", path_str->data));
        return VAL_UNDEFINED;
    }
    off_t size = vfs_lseek(fd, 0, SEEK_END);
    vfs_lseek(fd, 0, SEEK_SET);
    char* raw = (char*)malloc(size + 1);
    ssize_t read_bytes = vfs_read(fd, raw, size);
    if (read_bytes < 0) read_bytes = 0;
    raw[read_bytes] = '\0';
    vfs_close(fd);

    Value result;
    if (encoding) {
        // Return decoded string
        result = create_string(raw, (int)read_bytes);
    } else {
        // Return Buffer
        result = create_buffer_from_string(raw, read_bytes, "utf8");
    }
    free(raw);
    return result;
}

// ---------------------------------------------------------------------------
// fs.writeFileSync(path, data[, options])
// ---------------------------------------------------------------------------
static Value js_fs_write_file_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) {
        vm_throw_error(vm, create_error("TypeError", create_string("path must be a string", 21)));
        return VAL_UNDEFINED;
    }
    JSString* path_str = (JSString*)get_pointer(args[0]);

    int fd = VFS_OPEN(path_str->data, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "open", path_str->data));
        return VAL_UNDEFINED;
    }

    Value data = args[1];
    if (IS_POINTER(data)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(data) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(data);
            vfs_write(fd, s->data, s->length);
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(data);
            if (buf->data) vfs_write(fd, buf->data, buf->length);
        }
    }
    vfs_close(fd);
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.appendFileSync(path, data)
// ---------------------------------------------------------------------------
static Value js_fs_append_file_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    int fd = VFS_OPEN(path_str->data, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "open", path_str->data));
        return VAL_UNDEFINED;
    }
    Value data = args[1];
    if (IS_POINTER(data)) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(data) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(data);
            vfs_write(fd, s->data, s->length);
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(data);
            if (buf->data) vfs_write(fd, buf->data, buf->length);
        }
    }
    vfs_close(fd);
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// fs.openSync(path, flags[, mode])
// ---------------------------------------------------------------------------
static Value build_stats_object(const struct stat* st);

static Value js_fs_open_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm); // For simplicity, assume at least read access
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    JSString* path_str = (JSString*)get_pointer(args[0]);
    JSString* flag_str = (JSString*)get_pointer(args[1]);
    
    int flags = O_RDONLY;
    if (strcmp(flag_str->data, "r") == 0) flags = O_RDONLY;
    else if (strcmp(flag_str->data, "r+") == 0) flags = O_RDWR;
    else if (strcmp(flag_str->data, "w") == 0) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (strcmp(flag_str->data, "w+") == 0) flags = O_RDWR | O_CREAT | O_TRUNC;
    else if (strcmp(flag_str->data, "a") == 0) flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (strcmp(flag_str->data, "a+") == 0) flags = O_RDWR | O_CREAT | O_APPEND;
    
    int mode = 0666;
    if (arg_count >= 3 && IS_INTEGER(args[2])) {
        mode = get_integer(args[2]);
    }
    
    int fd = VFS_OPEN(path_str->data, flags, mode);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "open", path_str->data));
        return VAL_UNDEFINED;
    }
    return make_integer(fd);
}

// ---------------------------------------------------------------------------
// fs.readSync(fd, buffer, offset, length, position)
// ---------------------------------------------------------------------------
static Value js_fs_read_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 5 || !IS_INTEGER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    int fd = get_integer(args[0]);
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
    if (h->obj_type != OBJ_BUFFER) return VAL_UNDEFINED;
    
    JSBuffer* buf = (JSBuffer*)get_pointer(args[1]);
    int offset = IS_INTEGER(args[2]) ? get_integer(args[2]) : 0;
    int length = IS_INTEGER(args[3]) ? get_integer(args[3]) : 0;
    
    if (offset < 0 || length < 0 || (uint32_t)(offset + length) > buf->length) {
        vm_throw_error(vm, create_error("RangeError", create_string("Index out of range", 18)));
        return VAL_UNDEFINED;
    }
    
    ssize_t bytes_read;
    if (IS_NULL(args[4]) || IS_UNDEFINED(args[4])) {
        bytes_read = vfs_read(fd, buf->data + offset, length);
    } else {
        int position = IS_INTEGER(args[4]) ? get_integer(args[4]) : 0;
        bytes_read = vfs_pread(fd, buf->data + offset, length, position);
    }
    
    if (bytes_read < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "read", ""));
        return VAL_UNDEFINED;
    }
    return make_integer((int)bytes_read);
}

// ---------------------------------------------------------------------------
// fs.writeSync(fd, buffer, offset, length, position)
// ---------------------------------------------------------------------------
static Value js_fs_write_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_INTEGER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    
    int fd = get_integer(args[0]);
    const char* data_ptr = NULL;
    int data_len = 0;
    
    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
    if (h->obj_type == OBJ_BUFFER) {
        JSBuffer* buf = (JSBuffer*)get_pointer(args[1]);
        int offset = (arg_count >= 3 && IS_INTEGER(args[2])) ? get_integer(args[2]) : 0;
        int length = (arg_count >= 4 && IS_INTEGER(args[3])) ? get_integer(args[3]) : buf->length - offset;
        
        if (offset < 0 || length < 0 || (uint32_t)(offset + length) > buf->length) {
            vm_throw_error(vm, create_error("RangeError", create_string("Index out of range", 18)));
            return VAL_UNDEFINED;
        }
        data_ptr = (const char*)(buf->data + offset);
        data_len = length;
    } else if (h->obj_type == OBJ_STRING) {
        JSString* str = (JSString*)get_pointer(args[1]);
        data_ptr = str->data;
        data_len = str->length;
    } else {
        return VAL_UNDEFINED;
    }
    
    ssize_t bytes_written;
    if (arg_count >= 5 && !IS_NULL(args[4]) && !IS_UNDEFINED(args[4])) {
        int position = IS_INTEGER(args[4]) ? get_integer(args[4]) : 0;
        bytes_written = vfs_pwrite(fd, data_ptr, data_len, position);
    } else {
        bytes_written = vfs_write(fd, data_ptr, data_len);
    }
    
    if (bytes_written < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "write", ""));
        return VAL_UNDEFINED;
    }
    return make_integer((int)bytes_written);
}

// ---------------------------------------------------------------------------
// fs.closeSync(fd)
// ---------------------------------------------------------------------------
static Value js_fs_close_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    int fd = get_integer(args[0]);
    if (vfs_close(fd) < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "close", ""));
    }
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.fstatSync(fd)
// ---------------------------------------------------------------------------
static Value js_fs_fstat_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    if (arg_count < 1 || !IS_INTEGER(args[0])) return VAL_UNDEFINED;
    int fd = get_integer(args[0]);
    struct stat st;
    if (vfs_fstat(fd, &st) < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "fstat", ""));
        return VAL_UNDEFINED;
    }
    return build_stats_object(&st);
}

// fs.existsSync(path) -> boolean
// ---------------------------------------------------------------------------
static Value js_fs_exists_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return make_boolean(false);
    JSString* path_str = (JSString*)get_pointer(args[0]);
    struct stat st;
    return make_boolean(VFS_STAT(path_str->data, &st) == 0);
}

// ---------------------------------------------------------------------------
// fs.mkdirSync(path[, options])
// Supports { recursive: true }
// ---------------------------------------------------------------------------
static Value js_fs_mkdir_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);

    if (VFS_MKDIR(path_str->data, 0755) != 0 && errno != EEXIST) {
        vm_throw_error(vm, create_system_error(vm, errno, "mkdir", path_str->data));
    }
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.unlinkSync(path)
// ---------------------------------------------------------------------------
static Value js_fs_unlink_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    if (VFS_UNLINK(path_str->data) != 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "unlink", path_str->data));
    }
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.renameSync(oldPath, newPath)
// ---------------------------------------------------------------------------
static Value js_fs_rename_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    JSString* old_path = (JSString*)get_pointer(args[0]);
    JSString* new_path = (JSString*)get_pointer(args[1]);
    if (VFS_RENAME(old_path->data, new_path->data) != 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "rename", old_path->data));
    }
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.readdirSync(path) -> string[]
// ---------------------------------------------------------------------------
static Value js_fs_readdir_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);

    DIR* dir = VFS_OPENDIR(path_str->data);
    if (!dir) {
        vm_throw_error(vm, create_system_error(vm, errno, "opendir", path_str->data));
        return VAL_UNDEFINED;
    }
    Value result = create_array(16);
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        array_push(result, create_string(entry->d_name, (int)strlen(entry->d_name)));
    }
    closedir(dir);
    return result;
}

// ---------------------------------------------------------------------------
// fs.statSync(path) -> Stats object
// fs.lstatSync(path) -> Stats object (does not follow symlinks)
// ---------------------------------------------------------------------------
static Value build_stats_object(const struct stat* st) {
    Value stats = create_object();
    object_set(stats, create_string("size", 4),  make_integer((int)(st->st_size)));
    object_set(stats, create_string("mtime", 5), make_integer((int)(st->st_mtime)));
    object_set(stats, create_string("atime", 5), make_integer((int)(st->st_atime)));
    object_set(stats, create_string("ctime", 5), make_integer((int)(st->st_ctime)));
    object_set(stats, create_string("mode", 4),  make_integer((int)(st->st_mode)));
    object_set(stats, create_string("isDirectory", 11),
               make_boolean(S_ISDIR(st->st_mode)));
    object_set(stats, create_string("isFile", 6),
               make_boolean(S_ISREG(st->st_mode)));
    object_set(stats, create_string("isSymbolicLink", 14),
               make_boolean(S_ISLNK(st->st_mode)));
    return stats;
}

static Value js_fs_stat_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    struct stat st;
    if (VFS_STAT(path_str->data, &st) != 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "stat", path_str->data));
        return VAL_UNDEFINED;
    }
    return build_stats_object(&st);
}

static Value js_fs_lstat_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    struct stat st;
    if (VFS_LSTAT(path_str->data, &st) != 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "lstat", path_str->data));
        return VAL_UNDEFINED;
    }
    return build_stats_object(&st);
}

static Value js_fs_mmap_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_INTEGER(args[1])) return VAL_UNDEFINED;

    JSString* path_str = (JSString*)get_pointer(args[0]);
    size_t length = (size_t)get_integer(args[1]);
    
    int fd = VFS_OPEN(path_str->data, O_RDONLY, 0666);
    if (fd < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "mmap_open", path_str->data));
        return VAL_UNDEFINED;
    }

    void* mapped = mmap(NULL, length, PROT_READ, MAP_SHARED, fd, 0);
    vfs_close(fd); // mmap holds the reference

    if (mapped == MAP_FAILED) {
        vm_throw_error(vm, create_system_error(vm, errno, "mmap", path_str->data));
        return VAL_UNDEFINED;
    }

    // Wrap the mmap pointer in a JSBuffer. We set a special flag or just return it.
    // Since our JSBuffer normally manages its own memory, we will just create an external buffer.
    Value buf_val = create_buffer(length, false);
    JSBuffer* jsbuf = (JSBuffer*)get_pointer(buf_val);
    
    // We replace the allocated data with our mmap pointer.
    // NOTE: This leaks the original small malloc from create_buffer, but is fine for this proxy demo.
    // A proper implementation would have an external buffer type.
    free(jsbuf->data);
    jsbuf->data = (uint8_t*)mapped;
    
    return buf_val;
}

static Value js_fs_munmap_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)vm; (void)this_val;
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;

    BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[0]) - sizeof(BlockHeader));
    if (h->obj_type == OBJ_BUFFER) {
        JSBuffer* jsbuf = (JSBuffer*)get_pointer(args[0]);
        if (jsbuf->data && jsbuf->length > 0) {
            munmap(jsbuf->data, jsbuf->length);
            jsbuf->data = NULL; // Prevent GC from freeing it
            jsbuf->length = 0;
        }
    }
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// fs.copyFileSync(src, dest)
// ---------------------------------------------------------------------------
static Value js_fs_copy_file_sync(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0]) || !IS_POINTER(args[1])) return VAL_UNDEFINED;
    JSString* src  = (JSString*)get_pointer(args[0]);
    JSString* dest = (JSString*)get_pointer(args[1]);

    int in = VFS_OPEN(src->data, O_RDONLY, 0666);
    if (in < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "open", src->data));
        return VAL_UNDEFINED;
    }
    int out = VFS_OPEN(dest->data, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (out < 0) {
        vfs_close(in);
        vm_throw_error(vm, create_system_error(vm, errno, "open", dest->data));
        return VAL_UNDEFINED;
    }
    char buf[4096];
    ssize_t n;
    while ((n = vfs_read(in, buf, sizeof(buf))) > 0) vfs_write(out, buf, n);
    vfs_close(in);
    vfs_close(out);
    return VAL_UNDEFINED;
}

// ---------------------------------------------------------------------------
// Async fs: work items and thread-pool integration
// ---------------------------------------------------------------------------
#include "thread_pool.h"

/* Payload shared between the work fn (worker thread) and the after fn (main thread). */
typedef struct AsyncReadPayload {
    VM*    vm;
    char*  path;        /* heap-allocated path copy       */
    char*  encoding;    /* heap-allocated encoding or NULL */
    char*  data;        /* result buffer (heap)           */
    size_t data_len;
    Value  callback;    /* JS cb(err, result)             */
    bool   is_promise;
    Value  promise;
} AsyncReadPayload;

typedef struct AsyncWritePayload {
    VM*    vm;
    char*  path;
    const char* mode; /* "wb" or "ab" */
    char*  data;      /* heap copy of write data        */
    size_t data_len;
    Value  callback;
    bool   is_promise;
    Value  promise;
} AsyncWritePayload;

typedef struct AsyncStatPayload {
    VM*         vm;
    char*       path;
    struct stat st;
    Value       callback;
    bool        is_promise;
    Value       promise;
} AsyncStatPayload;

typedef struct AsyncPathPayload {
    VM*    vm;
    char*  path;
    Value  callback;
    bool   is_promise;
    Value  promise;
} AsyncPathPayload;

/* ── GC Markers for Payloads ─────────────────────────────────────────────── */
static void async_read_gc_mark(void* d, GCTraceFn trace) {
    AsyncReadPayload* p = (AsyncReadPayload*)d;
    if (IS_POINTER(p->callback)) trace(&p->callback);
    if (p->is_promise && IS_POINTER(p->promise)) trace(&p->promise);
}
static void async_write_gc_mark(void* d, GCTraceFn trace) {
    AsyncWritePayload* p = (AsyncWritePayload*)d;
    if (IS_POINTER(p->callback)) trace(&p->callback);
    if (p->is_promise && IS_POINTER(p->promise)) trace(&p->promise);
}
static void async_stat_gc_mark(void* d, GCTraceFn trace) {
    AsyncStatPayload* p = (AsyncStatPayload*)d;
    if (IS_POINTER(p->callback)) trace(&p->callback);
    if (p->is_promise && IS_POINTER(p->promise)) trace(&p->promise);
}
static void async_path_gc_mark(void* d, GCTraceFn trace) {
    AsyncPathPayload* p = (AsyncPathPayload*)d;
    if (IS_POINTER(p->callback)) trace(&p->callback);
    if (p->is_promise && IS_POINTER(p->promise)) trace(&p->promise);
}

/* ── readFile async work ─────────────────────────────────────────────────── */

static void async_read_work(void* d, int* status) {
    AsyncReadPayload* p = (AsyncReadPayload*)d;
    int fd = VFS_OPEN(p->path, O_RDONLY, 0666);
    if (fd < 0) { *status = errno; return; }
    off_t sz = vfs_lseek(fd, 0, SEEK_END);
    vfs_lseek(fd, 0, SEEK_SET);
    p->data = (char*)malloc(sz + 1);
    ssize_t rd = vfs_read(fd, p->data, sz);
    p->data_len = (rd >= 0) ? rd : 0;
    p->data[p->data_len] = '\0';
    vfs_close(fd);
    *status = 0;
}

static void async_read_after(struct VM* vm, void* d, int status) {
    AsyncReadPayload* p = (AsyncReadPayload*)d;
    if (!IS_POINTER(p->callback) && !p->is_promise) goto cleanup;
    Value err, out;
    if (status != 0) {
        err = create_system_error(vm, status, "open", p->path);
        out = VAL_UNDEFINED;
    } else {
        err = VAL_NULL;
        if (p->encoding) {
            out = create_string(p->data, (int)p->data_len);
        } else {
            out = create_buffer_from_string(p->data, p->data_len, "utf8");
        }
    }
    
    if (p->is_promise) {
        if (status != 0) {
            js_promise_reject_func(vm, p->promise, 1, &err);
        } else {
            js_promise_resolve_func(vm, p->promise, 1, &out);
        }
    } else {
        Value argv[2] = { err, out };
        vm_call_function(vm, p->callback, 2, argv);
    }
cleanup:
    free(p->path);
    free(p->encoding);
    free(p->data);
    free(p);
}

static Value js_fs_read_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    Value cb = args[arg_count - 1]; /* last arg is callback */
    const char* enc = (arg_count >= 3) ? extract_encoding(args[1]) : NULL;

    AsyncReadPayload* p = (AsyncReadPayload*)calloc(1, sizeof(AsyncReadPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->encoding = enc ? strdup(enc) : NULL;
    p->callback = cb;
    p->is_promise = false;

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_read_work;
    item->after = async_read_after;
    item->gc_mark = async_read_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_read_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    const char* enc = (arg_count >= 2) ? extract_encoding(args[1]) : NULL;

    AsyncReadPayload* p = (AsyncReadPayload*)calloc(1, sizeof(AsyncReadPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->encoding = enc ? strdup(enc) : NULL;
    p->is_promise = true;
    p->promise  = create_promise(0, VAL_UNDEFINED); /* Pending */

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_read_work;
    item->after = async_read_after;
    item->gc_mark = async_read_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

/* ── writeFile async work ────────────────────────────────────────────────── */

static void async_write_work(void* d, int* status) {
    AsyncWritePayload* p = (AsyncWritePayload*)d;
    int flags = (strcmp(p->mode, "ab") == 0) ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
    int fd = VFS_OPEN(p->path, flags, 0666);
    if (fd < 0) { *status = errno; return; }
    vfs_write(fd, p->data, p->data_len);
    vfs_close(fd);
    *status = 0;
}

static void async_write_after(struct VM* vm, void* d, int status) {
    AsyncWritePayload* p = (AsyncWritePayload*)d;
    if (IS_POINTER(p->callback) || p->is_promise) {
        Value err = (status != 0)
            ? create_system_error(vm, status, "open", p->path)
            : VAL_NULL;
            
        if (p->is_promise) {
            if (status != 0) js_promise_reject_func(vm, p->promise, 1, &err);
            else {
                Value undef = VAL_UNDEFINED;
                js_promise_resolve_func(vm, p->promise, 1, &undef);
            }
        } else {
            vm_call_function(vm, p->callback, 1, &err);
        }
    }
    free(p->path);
    free(p->data);
    free(p);
}

static Value js_fs_write_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    Value cb = args[arg_count - 1];

    /* Extract data to write */
    const char* src_data = NULL;
    size_t src_len = 0;
    if (IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(args[1]);
            src_data = s->data; src_len = s->length;
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(args[1]);
            src_data = (const char*)buf->data; src_len = buf->length;
        }
    }

    AsyncWritePayload* p = (AsyncWritePayload*)calloc(1, sizeof(AsyncWritePayload));
    p->vm   = vm;
    p->path = strdup(path_str->data);
    p->mode = "wb";
    p->data = src_data ? (char*)malloc(src_len) : NULL;
    if (p->data && src_data) memcpy(p->data, src_data, src_len);
    p->data_len = src_len;
    p->callback = cb;
    p->is_promise = false;

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_write_work;
    item->after = async_write_after;
    item->gc_mark = async_write_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_write_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);

    const char* src_data = NULL;
    size_t src_len = 0;
    if (IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(args[1]);
            src_data = s->data; src_len = s->length;
        } else if (h->obj_type == OBJ_BUFFER) {
            JSBuffer* buf = (JSBuffer*)get_pointer(args[1]);
            src_data = (const char*)buf->data; src_len = buf->length;
        }
    }

    AsyncWritePayload* p = (AsyncWritePayload*)calloc(1, sizeof(AsyncWritePayload));
    p->vm   = vm;
    p->path = strdup(path_str->data);
    p->mode = "wb";
    p->data = src_data ? (char*)malloc(src_len) : NULL;
    if (p->data && src_data) memcpy(p->data, src_data, src_len);
    p->data_len = src_len;
    p->is_promise = true;
    p->promise = create_promise(0, VAL_UNDEFINED);

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_write_work;
    item->after = async_write_after;
    item->gc_mark = async_write_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

static Value js_fs_append_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    Value cb = args[arg_count - 1];
    const char* src_data = NULL;
    size_t src_len = 0;
    if (IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(args[1]);
            src_data = s->data; src_len = s->length;
        }
    }
    AsyncWritePayload* p = (AsyncWritePayload*)calloc(1, sizeof(AsyncWritePayload));
    p->vm   = vm;
    p->path = strdup(path_str->data);
    p->mode = "ab";
    p->data = src_data ? (char*)malloc(src_len) : NULL;
    if (p->data && src_data) memcpy(p->data, src_data, src_len);
    p->data_len = src_len;
    p->callback = cb;
    p->is_promise = false;

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_write_work;
    item->after = async_write_after;
    item->gc_mark = async_write_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_append_file(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    const char* src_data = NULL;
    size_t src_len = 0;
    if (IS_POINTER(args[1])) {
        BlockHeader* h = (BlockHeader*)((char*)get_pointer(args[1]) - sizeof(BlockHeader));
        if (h->obj_type == OBJ_STRING) {
            JSString* s = (JSString*)get_pointer(args[1]);
            src_data = s->data; src_len = s->length;
        }
    }
    AsyncWritePayload* p = (AsyncWritePayload*)calloc(1, sizeof(AsyncWritePayload));
    p->vm   = vm;
    p->path = strdup(path_str->data);
    p->mode = "ab";
    p->data = src_data ? (char*)malloc(src_len) : NULL;
    if (p->data && src_data) memcpy(p->data, src_data, src_len);
    p->data_len = src_len;
    p->is_promise = true;
    p->promise = create_promise(0, VAL_UNDEFINED);

    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_write_work;
    item->after = async_write_after;
    item->gc_mark = async_write_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

/* ── stat async ──────────────────────────────────────────────────────────── */

static void async_stat_work(void* d, int* status) {
    AsyncStatPayload* p = (AsyncStatPayload*)d;
    *status = (VFS_STAT(p->path, &p->st) != 0) ? errno : 0;
}

static void async_stat_after(struct VM* vm, void* d, int status) {
    AsyncStatPayload* p = (AsyncStatPayload*)d;
    if (IS_POINTER(p->callback) || p->is_promise) {
        Value err = (status != 0)
            ? create_system_error(vm, status, "stat", p->path)
            : VAL_NULL;
        Value stats = (status == 0) ? build_stats_object(&p->st) : VAL_UNDEFINED;
        
        if (p->is_promise) {
            if (status != 0) js_promise_reject_func(vm, p->promise, 1, &err);
            else js_promise_resolve_func(vm, p->promise, 1, &stats);
        } else {
            Value argv[2] = { err, stats };
            vm_call_function(vm, p->callback, 2, argv);
        }
    }
    free(p->path);
    free(p);
}

static Value js_fs_stat(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 2 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncStatPayload* p = (AsyncStatPayload*)calloc(1, sizeof(AsyncStatPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->callback = args[arg_count - 1];
    p->is_promise = false;
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_stat_work;
    item->after = async_stat_after;
    item->gc_mark = async_stat_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_stat(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_READ_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncStatPayload* p = (AsyncStatPayload*)calloc(1, sizeof(AsyncStatPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->is_promise = true;
    p->promise = create_promise(0, VAL_UNDEFINED);
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_stat_work;
    item->after = async_stat_after;
    item->gc_mark = async_stat_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

/* ── mkdir async ─────────────────────────────────────────────────────────── */

static void async_mkdir_work(void* d, int* status) {
    AsyncPathPayload* p = (AsyncPathPayload*)d;
    *status = (VFS_MKDIR(p->path, 0755) != 0 && errno != EEXIST) ? errno : 0;
}

static void async_path_after(struct VM* vm, void* d, int status) {
    AsyncPathPayload* p = (AsyncPathPayload*)d;
    if (IS_POINTER(p->callback) || p->is_promise) {
        Value err = (status != 0)
            ? create_system_error(vm, status, "path_op", p->path)
            : VAL_NULL;
            
        if (p->is_promise) {
            if (status != 0) js_promise_reject_func(vm, p->promise, 1, &err);
            else {
                Value undef = VAL_UNDEFINED;
                js_promise_resolve_func(vm, p->promise, 1, &undef);
            }
        } else {
            vm_call_function(vm, p->callback, 1, &err);
        }
    }
    free(p->path);
    free(p);
}

static Value js_fs_mkdir(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncPathPayload* p = (AsyncPathPayload*)calloc(1, sizeof(AsyncPathPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->callback = (arg_count >= 2) ? args[arg_count - 1] : VAL_NULL;
    p->is_promise = false;
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_mkdir_work;
    item->after = async_path_after;
    item->gc_mark = async_path_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_mkdir(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncPathPayload* p = (AsyncPathPayload*)calloc(1, sizeof(AsyncPathPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->is_promise = true;
    p->promise = create_promise(0, VAL_UNDEFINED);
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_mkdir_work;
    item->after = async_path_after;
    item->gc_mark = async_path_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

/* ── unlink async ────────────────────────────────────────────────────────── */

static void async_unlink_work(void* d, int* status) {
    AsyncPathPayload* p = (AsyncPathPayload*)d;
    *status = (VFS_UNLINK(p->path) != 0) ? errno : 0;
}

static Value js_fs_unlink(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncPathPayload* p = (AsyncPathPayload*)calloc(1, sizeof(AsyncPathPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->callback = (arg_count >= 2) ? args[arg_count - 1] : VAL_NULL;
    p->is_promise = false;
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_unlink_work;
    item->after = async_path_after;
    item->gc_mark = async_path_gc_mark;
    item->data  = p;
    tp_submit(item);
    return VAL_UNDEFINED;
}

static Value js_fs_promises_unlink(VM* vm, Value this_val, int arg_count, Value* args) {
    (void)this_val;
    ENFORCE_WRITE_ACCESS(vm);
    if (arg_count < 1 || !IS_POINTER(args[0])) return VAL_UNDEFINED;
    JSString* path_str = (JSString*)get_pointer(args[0]);
    AsyncPathPayload* p = (AsyncPathPayload*)calloc(1, sizeof(AsyncPathPayload));
    p->vm       = vm;
    p->path     = strdup(path_str->data);
    p->is_promise = true;
    p->promise = create_promise(0, VAL_UNDEFINED);
    WorkItem* item = (WorkItem*)malloc(sizeof(WorkItem));
    item->work  = async_unlink_work;
    item->after = async_path_after;
    item->gc_mark = async_path_gc_mark;
    item->data  = p;
    tp_submit(item);
    return p->promise;
}

/* ── Module factory ─────────────────────────────────────────────────────── */

/** Register a function on the 'exports' object. */
#define FS_FN(name, fn) \
    object_set(exports, create_string(name, (int)strlen(name)), \
               create_native_function((void*)(fn), create_string(name, (int)strlen(name))))

/** Register a function on any given object (used for fs.promises). */
#define FS_FN_ON(obj, name, fn) \
    object_set(obj, create_string(name, (int)strlen(name)), \
               create_native_function((void*)(fn), create_string(name, (int)strlen(name))))

Value build_fs_module(VM* vm) {
    (void)vm;
    Value exports = create_object();

    /* ── Synchronous API ── */
    FS_FN("readFileSync",   js_fs_read_file_sync);
    FS_FN("writeFileSync",  js_fs_write_file_sync);
    FS_FN("appendFileSync", js_fs_append_file_sync);
    FS_FN("existsSync",     js_fs_exists_sync);
    FS_FN("mkdirSync",      js_fs_mkdir_sync);
    FS_FN("unlinkSync",     js_fs_unlink_sync);
    FS_FN("renameSync",     js_fs_rename_sync);
    FS_FN("readdirSync",    js_fs_readdir_sync);
    FS_FN("statSync",       js_fs_stat_sync);
    FS_FN("lstatSync",      js_fs_lstat_sync);
    FS_FN("copyFileSync",   js_fs_copy_file_sync);
    FS_FN("openSync",       js_fs_open_sync);
    FS_FN("readSync",       js_fs_read_sync);
    FS_FN("writeSync",      js_fs_write_sync);
    FS_FN("closeSync",      js_fs_close_sync);
    FS_FN("fstatSync",      js_fs_fstat_sync);

    /* ── Async API (Node.js callback style: cb(err, result)) ── */
    FS_FN("readFile",   js_fs_read_file);
    FS_FN("writeFile",  js_fs_write_file);
    FS_FN("appendFile", js_fs_append_file);
    FS_FN("stat",       js_fs_stat);
    FS_FN("mkdir",      js_fs_mkdir);
    FS_FN("unlink",     js_fs_unlink);

    /* ── fs.promises (same async functions exposed as a sub-object) ──
     * Full Promise wrapping (Phase 11) returns actual Promise objects. */
    Value promises = create_object();
    FS_FN_ON(promises, "readFile",   js_fs_promises_read_file);
    FS_FN_ON(promises, "writeFile",  js_fs_promises_write_file);
    FS_FN_ON(promises, "appendFile", js_fs_promises_append_file);
    FS_FN_ON(promises, "stat",       js_fs_promises_stat);
    FS_FN_ON(promises, "mkdir",      js_fs_promises_mkdir);
    FS_FN_ON(promises, "unlink",     js_fs_promises_unlink);
    object_set(exports, create_string("promises", 8), promises);

    return exports;
}

