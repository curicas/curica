/**
 * @file vfs_module.c
 * @brief Virtual File System (VFS) Kernel Router
 * 
 * Intercepts POSIX file system access originating from WASM and JS user-space
 * processes. Dynamically routes standard file operations enforcing a strict 
 * POSIX FHS compliance (e.g. /bin, /usr, /home/user).
 * 
 * Handles reading from the frozen internal zip payload (APE), 
 * dynamic host overlays mounted via `--attach`, and manages
 * in-memory volatile tmpfs ramdisks.
 */
#include "vfs_module.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static VFSDisk disks[MAX_DISKS];
static int num_disks = 0;
static bool initialized = false;

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
                snprintf(local_dir, sizeof(local_dir), "./.curica_disks/%s", name);
                create_dir_recursive(local_dir);
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
        snprintf(resolved_buffer, buffer_size, "./.curica_disks/%s%s", disk->name, rest_of_path);
    }

    return resolved_buffer;
}
