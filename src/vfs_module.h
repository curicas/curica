#ifndef VFS_MODULE_H
#define VFS_MODULE_H

#include <stdbool.h>
#include <stddef.h>

#define MAX_DISKS 16
#define MAX_DISK_NAME 64

typedef enum {
    DISK_READONLY,
    DISK_WRITABLE
} DiskMode;

typedef struct {
    char name[MAX_DISK_NAME];
    DiskMode mode;
} VFSDisk;

void vfs_init(void);
const char* vfs_resolve_path(const char* original_path, char* resolved_buffer, size_t buffer_size);
bool vfs_is_vfs_path(const char* path);

#endif // VFS_MODULE_H
