/// @file mem.c
/// @brief Memory devices.
/// @copyright (c) 2024 This file is distributed under the MIT License.
/// See LICENSE.md for details.

// Include the kernel log levels.
#include "sys/kernel_levels.h"
/// Change the header.
#define __DEBUG_HEADER__ "[MEMDEV  ]"
/// Set the log level.
#define __DEBUG_LEVEL__ GLOBAL_LOGLEVEL

#include "assert.h"
#include "drivers/mem.h"
#include "io/debug.h"
#include "fs/vfs.h"
#include "string.h"
#include "sys/errno.h"
#include "system/syscall.h"

static vfs_sys_operations_t null_sys_operations = {
    .mkdir_f = NULL,
    .rmdir_f = NULL,
    .stat_f  = NULL,
};


static vfs_file_t *null_open(const char *path, int flags, mode_t mode);
static int null_close(vfs_file_t * file);
static ssize_t null_write(vfs_file_t * file, const void *buffer, off_t offset, size_t size);
static ssize_t null_read(vfs_file_t * file, char *buffer, off_t offset, size_t size);
static int null_fstat(vfs_file_t * file, stat_t *stat);

static vfs_file_operations_t null_fs_operations = {
    .open_f     = null_open,
    .unlink_f   = NULL,
    .close_f    = null_close,
    .read_f     = null_read,
    .write_f    = null_write,
    .lseek_f    = NULL,
    .stat_f     = null_fstat,
    .ioctl_f    = NULL,
    .getdents_f = NULL
};


static vfs_file_t *null_device_create(const char* name) {
    // Create the file.
    vfs_file_t *file = kmem_cache_alloc(vfs_file_cache, GFP_KERNEL);
    if (file == NULL) {
        pr_err("Failed to create null device.\n");
        return NULL;
    }

    // Set the device name.
    strncpy(file->name, name, NAME_MAX);
    file->count = 0;
    // Set the operations.
    file->sys_operations = &null_sys_operations;
    file->fs_operations  = &null_fs_operations;
    return file;
}

static vfs_file_t* null_open(const char *path, int flags, mode_t mode) {
    return null_device_create(path);
}

static int null_close(vfs_file_t * file) {
    assert(file && "Received null file.");
    kmem_cache_free(file);
    return 0;
}

static ssize_t null_write(vfs_file_t * file, const void *buffer, off_t offset, size_t size) {
    return size;
}

static ssize_t null_read(vfs_file_t * file, char *buffer, off_t offset, size_t size) {
    return 0;
}

static int null_fstat(vfs_file_t * file, stat_t *stat)
{
    pr_debug("null_fstat(%s, %p)\n", file->name, stat);
    stat->st_dev   = 0;
    stat->st_ino   = 0;
    stat->st_mode  = 0x0666;
    stat->st_uid   = 0;
    stat->st_gid   = 0;
    stat->st_atime = sys_time(NULL);
    stat->st_mtime = sys_time(NULL);
    stat->st_ctime = sys_time(NULL);
    stat->st_size  = 0;
    return 0;
}

int mem_devs_initialize(void)
{
    vfs_file_t *devnull = null_device_create("/dev/null");
    if (!devnull) {
        pr_err("Failed to create devnull");
        return -ENODEV;
    }
    
    if (!vfs_mount("/dev/null", devnull)) {
        pr_err("Failed to mount /dev/null");
        return 1;
    }

    return 0;
}
