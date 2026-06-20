/**
 * @file vfs_module.c
 * @brief Virtual File System (VFS) Kernel Router
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
#include "vfs_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

static VFSDisk disks[MAX_DISKS];
static int num_disks = 0;
static bool initialized = false;

// ---------------------------------------------------------------------------
// In-Memory tmpfs (Phase 1.3)
// ---------------------------------------------------------------------------
#define MAX_TMPFS_FILES 256
#define VFS_FD_TMPFS_MASK 0x20000000

typedef struct {
    char path[256];
    uint8_t* data;
    size_t size;
    size_t capacity;
    bool in_use;
} TmpfsFile;

static TmpfsFile tmpfs_files[MAX_TMPFS_FILES];

typedef struct {
    int file_idx;
    size_t offset;
    bool in_use;
} TmpfsFd;

#define MAX_TMPFS_FDS 512
static TmpfsFd tmpfs_fds[MAX_TMPFS_FDS];

static int tmpfs_open(const char* path, int flags) {
    int file_idx = -1;
    for (int i = 0; i < MAX_TMPFS_FILES; i++) {
        if (tmpfs_files[i].in_use && strcmp(tmpfs_files[i].path, path) == 0) {
            file_idx = i;
            break;
        }
    }

    if (file_idx == -1) {
        if ((flags & O_CREAT) == 0) return -1; // File not found
        // Create new
        for (int i = 0; i < MAX_TMPFS_FILES; i++) {
            if (!tmpfs_files[i].in_use) {
                file_idx = i;
                tmpfs_files[i].in_use = true;
                strncpy(tmpfs_files[i].path, path, sizeof(tmpfs_files[i].path) - 1);
                tmpfs_files[i].data = NULL;
                tmpfs_files[i].size = 0;
                tmpfs_files[i].capacity = 0;
                break;
            }
        }
        if (file_idx == -1) return -1; // Out of memory files
    } else if ((flags & O_TRUNC)) {
        tmpfs_files[file_idx].size = 0;
    }

    for (int i = 0; i < MAX_TMPFS_FDS; i++) {
        if (!tmpfs_fds[i].in_use) {
            tmpfs_fds[i].in_use = true;
            tmpfs_fds[i].file_idx = file_idx;
            tmpfs_fds[i].offset = (flags & O_APPEND) ? tmpfs_files[file_idx].size : 0;
            return VFS_FD_VIRTUAL_MASK | VFS_FD_TMPFS_MASK | i;
        }
    }
    return -1;
}

static ssize_t tmpfs_read(int fd_idx, void* buf, size_t count) {
    if (fd_idx < 0 || fd_idx >= MAX_TMPFS_FDS || !tmpfs_fds[fd_idx].in_use) return -1;
    TmpfsFd* tfd = &tmpfs_fds[fd_idx];
    TmpfsFile* file = &tmpfs_files[tfd->file_idx];

    if (tfd->offset >= file->size) return 0; // EOF
    size_t available = file->size - tfd->offset;
    size_t to_read = (count < available) ? count : available;

    memcpy(buf, file->data + tfd->offset, to_read);
    tfd->offset += to_read;
    return to_read;
}

static ssize_t tmpfs_write(int fd_idx, const void* buf, size_t count) {
    if (fd_idx < 0 || fd_idx >= MAX_TMPFS_FDS || !tmpfs_fds[fd_idx].in_use) return -1;
    TmpfsFd* tfd = &tmpfs_fds[fd_idx];
    TmpfsFile* file = &tmpfs_files[tfd->file_idx];

    if (tfd->offset + count > file->capacity) {
        size_t new_cap = file->capacity == 0 ? 1024 : file->capacity * 2;
        while (new_cap < tfd->offset + count) new_cap *= 2;
        uint8_t* new_data = realloc(file->data, new_cap);
        if (!new_data) return -1; // OOM
        file->data = new_data;
        file->capacity = new_cap;
    }

    memcpy(file->data + tfd->offset, buf, count);
    tfd->offset += count;
    if (tfd->offset > file->size) {
        file->size = tfd->offset;
    }
    return count;
}

static off_t tmpfs_lseek(int fd_idx, off_t offset, int whence) {
    if (fd_idx < 0 || fd_idx >= MAX_TMPFS_FDS || !tmpfs_fds[fd_idx].in_use) return -1;
    TmpfsFd* tfd = &tmpfs_fds[fd_idx];
    TmpfsFile* file = &tmpfs_files[tfd->file_idx];

    if (whence == SEEK_SET) {
        tfd->offset = offset;
    } else if (whence == SEEK_CUR) {
        tfd->offset += offset;
    } else if (whence == SEEK_END) {
        tfd->offset = file->size + offset;
    } else {
        return -1;
    }
    return tfd->offset;
}

static int tmpfs_close(int fd_idx) {
    if (fd_idx < 0 || fd_idx >= MAX_TMPFS_FDS || !tmpfs_fds[fd_idx].in_use) return -1;
    tmpfs_fds[fd_idx].in_use = false;
    return 0;
}

static void create_dir_recursive(const char *dir) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", dir);
    len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

void vfs_init(void) {
    if (initialized) return;
    initialized = true;

    // Try to load /.curica_vfs.txt from the zip payload
    FILE* f = fopen("/zip/.curica_vfs.txt", "r");
    if (!f) return; // No VFS configured

    char line[256];
    while (fgets(line, sizeof(line), f) && num_disks < MAX_DISKS) {
        line[strcspn(line, "\r\n")] = 0; // Strip newline
        if (strlen(line) == 0) continue;

        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            const char* name = line;
            const char* mode = colon + 1;

            strncpy(disks[num_disks].name, name, MAX_DISK_NAME - 1);
            disks[num_disks].name[MAX_DISK_NAME - 1] = '\0';

            if (strcmp(mode, "rw") == 0) {
                disks[num_disks].mode = DISK_WRITABLE;
                // Create local directory for writable disk
                char local_dir[256];
                if (strcmp(name, "packages") == 0) {
                    snprintf(local_dir, sizeof(local_dir), "./packages");
                } else {
                    snprintf(local_dir, sizeof(local_dir), "./.curica_disks/%s", name);
                }
                create_dir_recursive(local_dir);
                
                // Initialize strict POSIX FHS tree in the root writable workspace
                if (strcmp(name, "root") == 0) {
                    const char* fhs_dirs[] = {
                        "/bin", "/usr", "/etc", "/var", "/tmp", "/lib", "/home", "/home/user", "/dev", "/proc", "/sys"
                    };
                    for (size_t d = 0; d < sizeof(fhs_dirs)/sizeof(fhs_dirs[0]); d++) {
                        char fhs_path[512];
                        snprintf(fhs_path, sizeof(fhs_path), "%s%s", local_dir, fhs_dirs[d]);
                        create_dir_recursive(fhs_path);
                    }
                }
            } else {
                disks[num_disks].mode = DISK_READONLY;
            }
            num_disks++;
        }
    }
    fclose(f);
}

bool vfs_is_vfs_path(const char* path) {
    return (strncmp(path, "/disk/", 6) == 0);
}

const char* vfs_resolve_path(const char* original_path, char* resolved_buffer, size_t buffer_size) {
    if (!vfs_is_vfs_path(original_path)) {
        strncpy(resolved_buffer, original_path, buffer_size - 1);
        resolved_buffer[buffer_size - 1] = '\0';
        return resolved_buffer;
    }

    // Path format: /disk/diskname/rest/of/path
    const char* disk_name_start = original_path + 6;
    const char* slash = strchr(disk_name_start, '/');
    
    char disk_name[MAX_DISK_NAME] = {0};
    const char* rest_of_path = "";

    if (slash) {
        size_t name_len = slash - disk_name_start;
        if (name_len >= MAX_DISK_NAME) name_len = MAX_DISK_NAME - 1;
        strncpy(disk_name, disk_name_start, name_len);
        rest_of_path = slash; // Includes the leading slash
    } else {
        strncpy(disk_name, disk_name_start, MAX_DISK_NAME - 1);
    }

    // Find the disk
    VFSDisk* disk = NULL;
    for (int i = 0; i < num_disks; i++) {
        if (strcmp(disks[i].name, disk_name) == 0) {
            disk = &disks[i];
            break;
        }
    }

    if (!disk) {
        // Disk not found, fallback to original
        strncpy(resolved_buffer, original_path, buffer_size - 1);
        resolved_buffer[buffer_size - 1] = '\0';
        return resolved_buffer;
    }

    if (disk->mode == DISK_READONLY) {
        snprintf(resolved_buffer, buffer_size, "/zip/%s%s", disk->name, rest_of_path);
    } else {
        if (strcmp(disk->name, "packages") == 0) {
            snprintf(resolved_buffer, buffer_size, "./packages%s", rest_of_path);
        } else {
            snprintf(resolved_buffer, buffer_size, "./.curica_disks/%s%s", disk->name, rest_of_path);
        }
    }

    return resolved_buffer;
}

// ---------------------------------------------------------------------------
// Virtual File System (VFS) Interceptors
// ---------------------------------------------------------------------------

int vfs_open(const char* path, int flags, int mode) {
    char buf[1024];
    const char* p = vfs_resolve_path(path, buf, sizeof(buf));
    if (strcmp(p, "/dev/null") == 0) return VFS_FD_NULL;
    if (strcmp(p, "/dev/zero") == 0) return VFS_FD_ZERO;
    if (strcmp(p, "/dev/stdout") == 0) return VFS_FD_STDOUT;
    if (strcmp(p, "/dev/stderr") == 0) return VFS_FD_STDERR;
    if (strcmp(p, "/dev/dsp") == 0) return VFS_FD_DSP;
    if (strcmp(p, "/dev/random") == 0 || strcmp(p, "/dev/urandom") == 0) return VFS_FD_RANDOM;
    
    // Intercept tmpfs paths dynamically mapping any /tmp or host equivalents mapped to tmpfs
    if (strncmp(path, "/tmp/", 5) == 0 || strncmp(p, "./.curica_disks/root/tmp/", 25) == 0) {
        return tmpfs_open(path, flags);
    }
    
    // Pass-through essential host networking files for DNS and SSL
    if (strcmp(path, "/etc/resolv.conf") == 0 || 
        strcmp(path, "/etc/hosts") == 0 || 
        strcmp(path, "/etc/ssl/certs/ca-certificates.crt") == 0) {
        return open(path, flags, mode);
    }

    return open(p, flags, mode);
}

ssize_t vfs_read(int fd, void* buf, size_t count) {
    if (fd & VFS_FD_VIRTUAL_MASK) {
        if (fd & VFS_FD_TMPFS_MASK) {
            return tmpfs_read(fd & ~VFS_FD_VIRTUAL_MASK & ~VFS_FD_TMPFS_MASK, buf, count);
        }
        if (fd == VFS_FD_NULL) return 0; // EOF
        if (fd == VFS_FD_ZERO) {
            memset(buf, 0, count);
            return count;
        }
        if (fd == VFS_FD_RANDOM) {
            for(size_t i = 0; i < count; i++) {
                ((uint8_t*)buf)[i] = rand() & 0xFF;
            }
            return count;
        }
        if (fd == VFS_FD_DSP || fd == VFS_FD_STDOUT || fd == VFS_FD_STDERR) {
            return 0; // EOF for outputs
        }
        errno = EBADF;
        return -1;
    }
    return read(fd, buf, count);
}

ssize_t vfs_write(int fd, const void* buf, size_t count) {
    if (fd & VFS_FD_VIRTUAL_MASK) {
        if (fd & VFS_FD_TMPFS_MASK) {
            return tmpfs_write(fd & ~VFS_FD_VIRTUAL_MASK & ~VFS_FD_TMPFS_MASK, buf, count);
        }
        if (fd == VFS_FD_NULL || fd == VFS_FD_ZERO || fd == VFS_FD_RANDOM) return count; // discard
        if (fd == VFS_FD_STDOUT) return write(1, buf, count);
        if (fd == VFS_FD_STDERR) return write(2, buf, count);
        if (fd == VFS_FD_DSP) {
            // Future: Write PCM frames to raw audio ring buffer
            return count; // discard for now to avoid bloat
        }
        errno = EBADF;
        return -1;
    }
    return write(fd, buf, count);
}

off_t vfs_lseek(int fd, off_t offset, int whence) {
    if (fd & VFS_FD_VIRTUAL_MASK) {
        if (fd & VFS_FD_TMPFS_MASK) {
            return tmpfs_lseek(fd & ~VFS_FD_VIRTUAL_MASK & ~VFS_FD_TMPFS_MASK, offset, whence);
        }
        if (fd == VFS_FD_NULL || fd == VFS_FD_ZERO) return 0;
        errno = ESPIPE;
        return -1;
    }
    return lseek(fd, offset, whence);
}

int vfs_close(int fd) {
    if (fd & VFS_FD_VIRTUAL_MASK) {
        if (fd & VFS_FD_TMPFS_MASK) {
            return tmpfs_close(fd & ~VFS_FD_VIRTUAL_MASK & ~VFS_FD_TMPFS_MASK);
        }
        return 0;
    }
    return close(fd);
}

static void populate_dev_stat(struct stat* st) {
    memset(st, 0, sizeof(struct stat));
    st->st_mode = S_IFCHR | 0666;
    st->st_nlink = 1;
}

int vfs_stat(const char* path, struct stat* st) {
    if (strncmp(path, "/tmp/", 5) == 0) {
        for (int i = 0; i < MAX_TMPFS_FILES; i++) {
            if (tmpfs_files[i].in_use && strcmp(tmpfs_files[i].path, path) == 0) {
                memset(st, 0, sizeof(struct stat));
                st->st_mode = S_IFREG | 0777;
                st->st_size = tmpfs_files[i].size;
                st->st_nlink = 1;
                return 0;
            }
        }
        // Fallthrough in case it's an actual disk mapping or missing
    }

    char buf[1024];
    const char* p = vfs_resolve_path(path, buf, sizeof(buf));
    if (strcmp(path, "/etc/resolv.conf") == 0 || 
        strcmp(path, "/etc/hosts") == 0 || 
        strcmp(path, "/etc/ssl/certs/ca-certificates.crt") == 0) {
        p = path;
    }
    if (strncmp(p, "/dev/", 5) == 0) { populate_dev_stat(st); return 0; }
    return stat(p, st);
}

int vfs_lstat(const char* path, struct stat* st) {
    char buf[1024];
    const char* p = vfs_resolve_path(path, buf, sizeof(buf));
    if (strcmp(path, "/etc/resolv.conf") == 0 || 
        strcmp(path, "/etc/hosts") == 0 || 
        strcmp(path, "/etc/ssl/certs/ca-certificates.crt") == 0) {
        p = path;
    }
    if (strncmp(p, "/dev/", 5) == 0) { populate_dev_stat(st); return 0; }
    return lstat(p, st);
}

int vfs_fstat(int fd, struct stat* st) {
    if (fd & VFS_FD_VIRTUAL_MASK) { populate_dev_stat(st); return 0; }
    return fstat(fd, st);
}

int vfs_mkdir(const char* path, mode_t mode) {
    char buf[1024];
    return mkdir(vfs_resolve_path(path, buf, sizeof(buf)), mode);
}

int vfs_unlink(const char* path) {
    if (strncmp(path, "/tmp/", 5) == 0) {
        for (int i = 0; i < MAX_TMPFS_FILES; i++) {
            if (tmpfs_files[i].in_use && strcmp(tmpfs_files[i].path, path) == 0) {
                tmpfs_files[i].in_use = false;
                free(tmpfs_files[i].data);
                tmpfs_files[i].data = NULL;
                tmpfs_files[i].size = 0;
                return 0;
            }
        }
        return -1; // ENOENT
    }
    char buf[1024];
    return unlink(vfs_resolve_path(path, buf, sizeof(buf)));
}

int vfs_rename(const char* oldpath, const char* newpath) {
    char buf1[1024];
    char buf2[1024];
    return rename(vfs_resolve_path(oldpath, buf1, sizeof(buf1)), vfs_resolve_path(newpath, buf2, sizeof(buf2)));
}

DIR* vfs_opendir(const char* path) {
    char buf[1024];
    return opendir(vfs_resolve_path(path, buf, sizeof(buf)));
}

struct dirent* vfs_readdir(DIR* dir) {
    return readdir(dir);
}

int vfs_closedir(DIR* dir) {
    return closedir(dir);
}

ssize_t vfs_pread(int fd, void* buf, size_t count, off_t offset) {
    if (fd & VFS_FD_VIRTUAL_MASK) return vfs_read(fd, buf, count);
    return pread(fd, buf, count, offset);
}

ssize_t vfs_pwrite(int fd, const void* buf, size_t count, off_t offset) {
    if (fd & VFS_FD_VIRTUAL_MASK) return vfs_write(fd, buf, count);
    return pwrite(fd, buf, count, offset);
}
