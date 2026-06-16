const fs = require('fs');

let content = fs.readFileSync('src/fs_module.c', 'utf8');

// Insert #include <fcntl.h>
content = content.replace('#include <unistd.h>', '#include <unistd.h>\n#include <fcntl.h>');

// Insert VFS_OPEN
content = content.replace(
    '#define VFS_FOPEN(path, mode) ({ char _buf[1024]; fopen(vfs_resolve_path(path, _buf, sizeof(_buf)), mode); })',
    '#define VFS_FOPEN(path, mode) ({ char _buf[1024]; fopen(vfs_resolve_path(path, _buf, sizeof(_buf)), mode); })\n#define VFS_OPEN(path, flags, mode) ({ char _buf[1024]; open(vfs_resolve_path(path, _buf, sizeof(_buf)), flags, mode); })'
);

const functions = `
// ---------------------------------------------------------------------------
// fs.openSync(path, flags[, mode])
// ---------------------------------------------------------------------------
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
        bytes_read = read(fd, buf->data + offset, length);
    } else {
        int position = IS_INTEGER(args[4]) ? get_integer(args[4]) : 0;
        bytes_read = pread(fd, buf->data + offset, length, position);
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
        bytes_written = pwrite(fd, data_ptr, data_len, position);
    } else {
        bytes_written = write(fd, data_ptr, data_len);
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
    if (close(fd) < 0) {
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
    if (fstat(fd, &st) < 0) {
        vm_throw_error(vm, create_system_error(vm, errno, "fstat", ""));
        return VAL_UNDEFINED;
    }
    return build_stats_object(&st);
}

`;

content = content.replace(
    '// fs.existsSync(path) -> boolean',
    functions + '// fs.existsSync(path) -> boolean'
);

fs.writeFileSync('src/fs_module.c', content);
console.log('fs_module.c patched');
