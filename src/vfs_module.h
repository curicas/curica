#ifndef VFS_MODULE_H
#define VFS_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define MAX_DISKS 16
#define MAX_DISK_NAME 64

// Virtual File Descriptors for Pseudo-Devices
#define VFS_FD_VIRTUAL_MASK 0x40000000
#define VFS_FD_NULL    (VFS_FD_VIRTUAL_MASK | 1)
#define VFS_FD_ZERO    (VFS_FD_VIRTUAL_MASK | 2)
#define VFS_FD_STDOUT  (VFS_FD_VIRTUAL_MASK | 3)
#define VFS_FD_STDERR  (VFS_FD_VIRTUAL_MASK | 4)
#define VFS_FD_DSP     (VFS_FD_VIRTUAL_MASK | 5)
#define VFS_FD_RANDOM  (VFS_FD_VIRTUAL_MASK | 6)
#define VFS_FD_STDIN   (VFS_FD_VIRTUAL_MASK | 7)
#define VFS_FD_TTY     (VFS_FD_VIRTUAL_MASK | 8)

typedef enum {
    DISK_READONLY,
    DISK_WRITABLE
} DiskMode;

typedef struct {
    char name[MAX_DISK_NAME];
    DiskMode mode;
} VFSDisk;

void vfs_init(void);
int vfs_mount_overlay(const char* host_path, const char* vfs_path);

// FHS Virtual System Interceptors
int vfs_open(const char* path, int flags, int mode);
ssize_t vfs_read(int fd, void* buf, size_t count);
ssize_t vfs_write(int fd, const void* buf, size_t count);
off_t vfs_lseek(int fd, off_t offset, int whence);
int vfs_close(int fd);

int vfs_stat(const char* path, struct stat* st);
int vfs_lstat(const char* path, struct stat* st);
int vfs_fstat(int fd, struct stat* st);

int vfs_mkdir(const char* path, mode_t mode);
int vfs_unlink(const char* path);
int vfs_rename(const char* oldpath, const char* newpath);

DIR* vfs_opendir(const char* path);
struct dirent* vfs_readdir(DIR* dir);
int vfs_closedir(DIR* dir);

ssize_t vfs_pread(int fd, void* buf, size_t count, off_t offset);
ssize_t vfs_pwrite(int fd, const void* buf, size_t count, off_t offset);

const char* vfs_resolve_path(const char* original_path, char* resolved_buffer, size_t buffer_size);
bool vfs_is_vfs_path(const char* path);

#endif // VFS_MODULE_H
