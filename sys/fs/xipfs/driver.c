/*
 * Copyright (C) 2024 Université de Lille
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_fs_xipfs
 * @{
 *
 * @file
 * @brief       xipfs driver implementation
 *
 * @author      Damien Amara <damien.amara@univ-lille.fr>
 *
 * @}
 */

/*
 * The following define is required in order to use strnlen(3)
 * since glibc 2.10. Refer to the SYNOPSIS section of the
 * strnlen(3) manual and the feature_test_macros(7) manual for
 * more information
 */
#define _POSIX_C_SOURCE 200809L

/*
 * libc includes
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

/*
 * RIOT includes
 */
/*
 * If ENABLE_DEBUG is defined as non-zero in an implementation
 * file, all calls to DEBUG will work like printf(3),
 * outputting the specified information to stdout.
 */
#define ENABLE_DEBUG 0
#include "debug.h"
#include "fs/xipfs.h"
#include "mutex.h"
#include "periph/flashpage.h"
#include "vfs.h"

/*
 * xipfs includes
 */
#include "include/buffer.h"
#include "include/errno.h"
#include "include/file.h"
#include "include/flash.h"
#include "include/fs.h"
#include "include/path.h"

/*
 * The eXecute In Place File System only makes sense if the
 * non-volatile memory of the target MCU is addressable
 */
#ifndef MODULE_PERIPH_FLASHPAGE_IN_ADDRESS_SPACE
#error "sys/fs/xipfs: the target MCU has no addressable NVM"
#endif /* !MODULE_PERIPH_FLASHPAGE_IN_ADDRESS_SPACE */

/*
 * Macro definitions
 */

/**
 * @internal
 *
 * @def MAX
 *
 * @brief Returns the maximum between x and y
 *
 * @param x The first variable to be compared
 *
 * @param y The second variable to be compared
 */
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

/**
 * @internal
 *
 * @def UNUSED
 *
 * @brief The most versatile macro for disabling warnings
 * related to unused variables
 *
 * @param x The unused variable name
 */
#define UNUSED(x) ((void)(x))

/*
 * Internal structure
 */

/**
 * @internal
 *
 * @brief xipfs internal representation of a directory entry
 */
typedef struct xipfs_dirent_s {
    /**
     * A pointer to the current file being searched within the
     * open directory
     */
    xipfs_file_t *filp;
    /**
     * The directory path to open for listing its contents
     */
    char dirname[XIPFS_PATH_MAX];
} xipfs_dirent_t;

/*
 * Global variables
 */

/**
 * @internal
 *
 * @brief xipfs global lock
 */
static mutex_t xipfs_mutex = MUTEX_INIT;

/**
 * @internal
 *
 * @brief An internal table that references the pointers to the
 * data structures of open files in the VFS abstraction layer
 */
static vfs_file_t *vfs_open_files[VFS_MAX_OPEN_FILES];

/*
 * Helper functions
 */

/**
 * @internal
 *
 * @pre base must be a pointer that references an accessible
 * memory region
 *
 * @pre path must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Copy the base name component of path, into the memory
 * region pointed to by base
 *
 * @param base A pointer to a memory region that respects the
 * preconditions for storing the base name component
 *
 * @param path A pointer to a path that respects the
 * preconditions
 */
static void basename(char *base, const char *path)
{
    const char *ptr, *start, *end;
    size_t len, i;

    assert(base != NULL);
    assert(path != NULL);
    assert(path[0] == '/');

    if (path[1] == '\0') {
        base[0] = '/';
        base[1] = '\0';
        return;
    }

    len = strnlen(path, XIPFS_PATH_MAX);
    assert(len < XIPFS_PATH_MAX);
    ptr = path + len - 1;

    if (ptr[0] == '/') {
        /* skip the trailing slash if the
         * path ends with one */
        ptr--;
    }
    end = ptr;

    while (ptr > path && *ptr != '/') {
        /* skip all characters that are not
         * slashes */
        ptr--;
    }
    /* skip the slash */
    start = ptr + 1;

    for (i = 0; start + i <= end; i++) {
        /* copy the characters of the base
         * name component until end */
        base[i] = start[i];
    }
    base[i] = '\0';
}

/**
 * @internal
 *
 * @pre dir must be a pointer that references an accessible
 * memory region
 *
 * @pre path must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Copy the directory name component of path, including
 * the final slash, into the memory region pointed to by dir
 *
 * @param dir A pointer to a memory region that respects the
 * preconditions for storing the directory name component
 *
 * @param path A pointer to a path that respects the
 * preconditions
 */
static void dirname(char *dir, const char *path)
{
    const char *end;
    size_t len, i;

    assert(dir != NULL);
    assert(path != NULL);
    assert(path[0] == '/');

    if (path[1] == '\0') {
        dir[0] = '/';
        dir[1] = '\0';
        return;
    }

    len = strnlen(path, XIPFS_PATH_MAX);
    assert(len < XIPFS_PATH_MAX);
    end = path + len - 1;

    if (*end == '/') {
        /* skip the trailing slash if the
         * path ends with one */
        end--;
    }

    while (end > path && *end != '/') {
        /* skip all characters that are not
         * slashes */
        end--;
    }

    if (end != path) {
        for (i = 0; path + i <= end; i++) {
            /* copy the characters of the directory
             * name component until end */
            dir[i] = path[i];
        }
        dir[i] = '\0';
    } else {
        /* no slashes found, except for the root */
        dir[0] = '/';
        dir[1] = '\0';
    }
}

/**
 * @internal
 *
 * @warning This function provides a workaround for xipfs-
 * specific functions that need to retrieve the xipfs mount
 * point structure directly, bypassing the VFS layer, as these
 * functions are not available in the VFS abstraction
 *
 * @brief Retrieves the xipfs mount point structure from a
 * specified path within the mount point
 *
 * @param path A path within the mount point for retrieving the
 * corresponding xipfs mount point structure
 *
 * @param mp A pointer to an accessible memory region for
 * storing the xipfs mount point structure
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
static int get_xipfs_mp(const char *path, xipfs_mount_t *xipfs_mp)
{
    char dir[XIPFS_PATH_MAX];
    size_t count, len;
    int fd, ret;

    if (path[0] != '/') {
        return -EINVAL;
    }
    len = strnlen(path, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    dirname(dir, path);

    if (len + strlen(".xipfs_infos") + 1 > XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    (void)strcat(dir, ".xipfs_infos");

    if ((ret = vfs_open(dir, O_RDONLY, 0)) < 0) {
        /* not a xipfs mount point */
        return ret;
    }
    fd = ret;

    count = 0;
    while (count < sizeof(*xipfs_mp)) {
        ret = vfs_read(fd, xipfs_mp, sizeof(*xipfs_mp));
        if (ret < 0) {
            /* error */
            return ret;
        }
        if (ret == 0) {
            /* EOF */
            break;
        }
        count += ret;
    }
    assert(count == sizeof(*xipfs_mp));

    (void)vfs_close(fd);

    return 0;
}

/**
 * @internal
 *
 * @pre full_path must be a pointer that references a path which
 * is accessible, null-terminated, starts with a slash,
 * normalized, and shorter than XIPFS_PATH_MAX
 *
 * @brief Returns a pointer to the first character of the
 * relative path derived from the absolute path retrieved from
 * the vfs_mp mount point structure
 *
 * @param vfs_mp A pointer to a memory region containing an
 * accessible VFS mount point structure
 *
 * @param full_path A pointer to a path that respects the
 * preconditions
 */
static const char *get_rel_path(vfs_mount_t *vfs_mp,
                                const char *full_path)
{
    const char *p1, *p2;

    assert(vfs_mp != NULL);
    assert(full_path != NULL);

    p1 = vfs_mp->mount_point;
    p2 = full_path;

    while (*p1 != '\0') {
        if (*p1++ != *p2++) {
            return NULL;
        }
    }

    return p2;
}

/**
 * @internal
 *
 * @pre dirp must be a pointer that references an accessible
 * memory region
 *
 * @pre dirp->mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @pre filp must be a pointer that references an accessible
 * memory region
 *
 * @pre n must be less than XIPFS_PATH_MAX
 *
 * @brief Verify whether the path from index n of the file
 * specified by filp in the open directory structure specified
 * by dirp has already been displayed
 *
 * @param dirp A pointer to a memory region containing an
 * accessible open directory structure
 *
 * @param dirp A pointer to a memory region containing an
 * accessible xipfs file structure
 *
 * @param n The The index of the last character in the path
 * prefix
 *
 * @return Returns zero if the path is already displayed, or a
 * negative value otherwise
 */
static int already_display(vfs_DIR *dirp, xipfs_file_t *filp,
                           size_t n)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_file_t *curp;
    size_t i;

    assert(dirp != NULL);
    assert(filp != NULL);
    assert(n < XIPFS_PATH_MAX);

    xipfs_mp = (xipfs_mount_t *)dirp->mp;
    if ((curp = xipfs_fs_head(xipfs_mp)) != NULL) {
        /* one file at least */
        do {
            if (curp == filp) {
                /* not already display */
                break;
            }
            i = 0;
            while (i < n) {
                /* compare curp and filp prefix */
                if (curp->path[i] != filp->path[i]) {
                    break;
                }
                i++;
            }
            /* skip files with wrong prefix */
            if (i == n) {
                /* curp and filp share a prefix */
                while (i < XIPFS_PATH_MAX    &&
                       curp->path[i] == '\0' &&
                       filp->path[i] == '\0' &&
                       curp->path[i] == '/'  &&
                       filp->path[i] == '/'  &&
                       curp->path[i] != filp->path[i]) {
                    /* compare curp and filp until a slash, null
                     * character, or differing character */
                    i++;
                }
                if (i == XIPFS_PATH_MAX) {
                    /* path too long */
                    return -1;
                }
                if (curp->path[i] == filp->path[i]) {
                    /* already display */
                    return 0;
                }
            }
        } while ((curp = xipfs_fs_next(curp)) != NULL);
    }

    /* not already display */
    return -1;
}

/**
 * @internal
 *
 * @brief Checks if the xipfs mount point structure passed as
 * an argument is valid
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 *
 * @return Returns zero if the xipfs mount point structure
 * passed as an argument is valid or a negative value otherwise
 */
static int xipfs_mp_check(xipfs_mount_t *xipfs_mp)
{
    if (xipfs_mp == NULL) {
        return -EFAULT;
    }
    if (xipfs_mp->magic != XIPFS_MAGIC) {
        return -EINVAL;
    }
    if (xipfs_mp->nbpage == 0) {
        return -EINVAL;
    }
    if (xipfs_mp->nbpage > FLASHPAGE_NUMOF) {
        return -EFAULT;
    }

    return 0;
}

/**
 * @internal
 *
 * @pre vfs_filp must be a pointer that references an accessible
 * memory region
 *
 * @brief Keeps track of a newly opened VFS file descriptor
 * structure
 *
 * @param vfs_filp A pointer to a memory region containing the
 * VFS file descriptor structure to keep track
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
static int vfs_open_files_track(vfs_file_t *vfs_filp)
{
    size_t i;

    assert(vfs_filp != NULL);

    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i] == NULL) {
            /* empty entry found */
            vfs_open_files[i] = vfs_filp;
            return 0;
        }
    }

    /* no more entries */
    return -1;
}

/**
 * @internal
 *
 * @pre vfs_filp must be a pointer that references an accessible
 * memory region
 *
 * @brief Stop keeping track of an open VFS file descriptor
 * structure
 *
 * @param vfs_filp A pointer to a memory region containing the
 * VFS file descriptor structure to stop keeping track
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
static int vfs_open_files_untrack(vfs_file_t *vfs_filp)
{
    int closed = 0;
    size_t i;

    assert(vfs_filp != NULL);

    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i] == vfs_filp) {
            if (closed == 1) {
                /* fd tracked twice */
                return -1;
            }
            vfs_open_files[i] = NULL;
            closed = 1;
        }
    }
    if (closed == 0) {
        /* fd not open */
        return -1;
    }

    return 0;
}

/**
 * @internal
 *
 * @pre xipfs_mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @brief Stop keeping track of all open VFS file descriptor
 * structures for the mount point specified as an argument
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 */
static void vfs_open_files_untrack_all(xipfs_mount_t *xipfs_mp)
{
    uintptr_t curr, start, end;
    size_t i;

    assert(xipfs_mp != NULL);

    start = (uintptr_t)xipfs_mp->vfs.private_data;
    end = start + xipfs_mp->nbpage * FLASHPAGE_SIZE;
    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        curr = (uintptr_t)vfs_open_files[i]->private_data.ptr;
        if (curr != (uintptr_t)xipfs_infos_file) {
            if (curr >= start && curr < end) {
                vfs_open_files[i] = NULL;
            }
        }
    }
}

/**
 * @internal
 *
 * @pre vfs_filp must be a pointer that references an accessible
 * memory region
 *
 * @brief Check whether an open VFS file descriptor structure
 * is tracked
 *
 * @param vfs_filp A pointer to a memory region containing the
 * VFS file descriptor structure to check
 *
 * @return Returns zero if the VFS file descriptor structure is
 * tracked or a negative value otherwise
 */
static int vfs_open_files_is_tracked(vfs_file_t *vfs_filp)
{
    int tracked = 0;
    size_t i;

    assert(vfs_filp != NULL);

    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (vfs_open_files[i] == vfs_filp) {
            if (tracked == 1) {
                /* fd tracked twice */
                return -1;
            }
            tracked = 1;
        }
    }

    return (tracked == 1) ? 0 : -1;
}

/**
 * @internal
 *
 * @pre xipfs_mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @pre removed must be a pointer that references an accessible
 * memory region
 *
 * @brief Update the tracked VFS open file descriptor structures
 * by modifying the internal address of the xipfs file,
 * following the removal of a file at the mount point, with both
 * elements provided as arguments
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 *
 * @param removed A pointer to a memory region containing the
 * removed xipfs file structure
 *
 * @param reserved The reserved size of the removed xipfs file
 * structure
 */
static void vfs_open_files_update(xipfs_mount_t *xipfs_mp,
                                  xipfs_file_t *removed,
                                  size_t reserved)
{
    uintptr_t curr, start, end;
    size_t i;

    assert(xipfs_mp != NULL);
    assert(removed != NULL);

    start = (uintptr_t)xipfs_mp->vfs.private_data;
    end = start + xipfs_mp->nbpage * FLASHPAGE_SIZE;

    for (i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        curr = (uintptr_t)vfs_open_files[i]->private_data.ptr;
        if (curr != (uintptr_t)xipfs_infos_file) {
            if (curr >= start && curr < end) {
                if (curr > (uintptr_t)removed) {
                    /* update entry with actual file address */
                    vfs_open_files[i]->private_data.ptr = (char *)
                        vfs_open_files[i]->private_data.ptr - reserved;
                } else if (curr == (uintptr_t)removed) {
                    /* entry no longer valid */
                    vfs_open_files[i] = NULL;
                }
            }
        }
    }
}

/**
 * @internal
 *
 * @pre xipfs_mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @pre xipfs_filp must be a pointer that references an
 * accessible memory region
 *
 * @brief Remove a file by flushing the read/write buffer,
 * consolidating the file system, and updating the internal
 * xipfs file addresses of all open VFS file descriptor
 * structures
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 *
 * @param xipfs_filp A pointer to a memory region containing
 * the accessible xipfs file structure of the xipfs file to
 * remove
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
static int sync_remove_file(xipfs_mount_t *xipfs_mp,
                            xipfs_file_t *xipfs_filp)
{
    size_t reserved;

    assert(xipfs_mp != NULL);
    assert(xipfs_filp != NULL);

    if (xipfs_buffer_flush() < 0) {
        return -1;
    }
    reserved = xipfs_filp->reserved;
    if (xipfs_fs_remove(xipfs_filp) < 0) {
        return -1;
    }
    vfs_open_files_update(xipfs_mp, xipfs_filp, reserved);

    return 0;
}

/*
 * Operations on open files
 */

static int xipfs_close0(vfs_file_t *vfs_filp)
{
    off_t size;

    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }

    if (vfs_filp->private_data.ptr != xipfs_infos_file) {
        if ((size = xipfs_file_get_size(vfs_filp)) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
        if (size < vfs_filp->pos) {
            /* synchronise file size */
            if (xipfs_file_set_size(vfs_filp, vfs_filp->pos) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
        }
    }

    if (vfs_open_files_untrack(vfs_filp) < 0) {
        return -EIO;
    }

    return 0;
}

static int xipfs_close(vfs_file_t *vfs_filp)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_close0(vfs_filp);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_fstat0(vfs_file_t *vfs_filp, struct stat *buf)
{
    off_t size, reserved;

    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }
    if (vfs_filp->private_data.ptr == xipfs_infos_file) {
        /* cannot fstat(2) a virtual file */
        return -EBADF;
    }

    if ((size = xipfs_file_get_size(vfs_filp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    if ((reserved = xipfs_file_get_reserved(vfs_filp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    (void)memset(buf, 0, sizeof(*buf));
    buf->st_dev = (dev_t)(uintptr_t)vfs_filp->mp;
    buf->st_ino = (ino_t)(uintptr_t)vfs_filp->private_data.ptr;
    buf->st_mode = S_IFREG;
    buf->st_nlink = 1;
    buf->st_uid = vfs_filp->pid;
    buf->st_size = MAX(size, vfs_filp->pos);
    buf->st_blksize = FLASHPAGE_SIZE;
    buf->st_blocks = reserved / FLASHPAGE_SIZE;

    return 0;
}

static int xipfs_fstat(vfs_file_t *vfs_filp, struct stat *buf)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_fstat0(vfs_filp, buf);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static off_t xipfs_lseek0(vfs_file_t *vfs_filp, off_t off, int whence)
{
    off_t max_pos, new_pos, size;

    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }

    if (vfs_filp->private_data.ptr == xipfs_infos_file) {
        max_pos = sizeof(xipfs_mount_t);
        size = (off_t)sizeof(xipfs_mount_t);
    } else {
        if ((max_pos = xipfs_file_get_max_pos(vfs_filp)) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
        if ((size = xipfs_file_get_size(vfs_filp)) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
    }

    switch (whence) {
        case SEEK_SET:
            new_pos = off;
            break;
        case SEEK_CUR:
            new_pos = vfs_filp->pos + off;
            break;
        case SEEK_END:
            new_pos = MAX(vfs_filp->pos, size) + off;
            break;
        default:
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > max_pos) {
        return -EINVAL;
    }

    if (vfs_filp->pos > size && new_pos < vfs_filp->pos) {
        /* synchronise file size */
        if (xipfs_file_set_size(vfs_filp, vfs_filp->pos) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
    }

    vfs_filp->pos = new_pos;

    return new_pos;
}

static off_t xipfs_lseek(vfs_file_t *vfs_filp, off_t off, int whence)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_lseek0(vfs_filp, off, whence);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_open0(vfs_file_t *vfs_filp, const char *name,
                       int flags, mode_t mode)
{
    char buf[XIPFS_PATH_MAX];
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    xipfs_file_t *filp;
    size_t len;
    int ret;

    /* mode bits are ignored */
    UNUSED(mode);

    if (vfs_filp == NULL) {
        return -EFAULT;
    }
    xipfs_mp = (xipfs_mount_t *)vfs_filp->mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (name == NULL) {
        return -EFAULT;
    }
    /* only these flags are supported */
    if (!((flags & O_CREAT)  == O_CREAT  ||
          (flags & O_EXCL)   == O_EXCL   ||
          (flags & O_WRONLY) == O_WRONLY ||
          (flags & O_RDONLY) == O_RDONLY ||
          (flags & O_RDWR)   == O_RDWR   ||
          (flags & O_APPEND) == O_APPEND))
    {
        return -EINVAL;
    }
    len = strnlen(name, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    /* virtual file handling */
    basename(buf, name);
    if (strncmp(buf, ".xipfs_infos", XIPFS_PATH_MAX) == 0) {
        if ((flags & O_CREAT) == O_CREAT &&
            (flags & O_EXCL) == O_EXCL) {
            return -EEXIST;
        }
        if ((flags & O_WRONLY) == O_WRONLY ||
            (flags & O_APPEND) == O_APPEND ||
            (flags & O_RDWR) == O_RDWR) {
            return -EACCES;
        }
        vfs_filp->private_data.ptr = xipfs_infos_file;
        if (vfs_open_files_track(vfs_filp) < 0) {
            return -ENFILE;
        }
        return 0;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, name) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
        if ((flags & O_CREAT) == O_CREAT &&
            (flags & O_EXCL) == O_EXCL) {
            return -EEXIST;
        }
        filp = xipath.witness;
        break;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -EISDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
        return -ENOENT;
    case XIPFS_PATH_CREATABLE:
        if ((flags & O_CREAT) != O_CREAT) {
            return -ENOENT;
        }
        if (xipath.path[xipath.len-1] == '/') {
            return -EISDIR;
        }
        if (xipath.witness != NULL && !(xipath.dirname[0] == '/' &&
                xipath.dirname[1] == '\0')) {
            if (strcmp(xipath.witness->path, xipath.dirname) == 0) {
                if (sync_remove_file(xipfs_mp, xipath.witness) < 0) {
                    return -EIO;
                }
            }
        }
        if ((filp = xipfs_fs_new_file(xipfs_mp, name, 0, 0)) == NULL) {
            /* file creation failed */
            if (xipfs_errno == XIPFS_ENOSPACE ||
                xipfs_errno == XIPFS_EFULL) {
                return -EDQUOT;
            }
            return -EIO;
        }
        break;
    default:
        return -EIO;
    }
    vfs_filp->private_data.ptr = filp;

    if ((flags & O_APPEND) == O_APPEND) {
        if ((vfs_filp->pos = xipfs_file_get_size(vfs_filp)) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
    } else {
        vfs_filp->pos = 0;
    }

    if (vfs_open_files_track(vfs_filp) < 0) {
        return -ENFILE;
    }

    return 0;
}

static int xipfs_open(vfs_file_t *vfs_filp, const char *name,
                      int flags, mode_t mode)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_open0(vfs_filp, name, flags, mode);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static ssize_t xipfs_read0(vfs_file_t *vfs_filp, void *dest,
                           size_t nbytes)
{
    off_t size;
    size_t i;

    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }
    if (dest == NULL) {
        return -EFAULT;
    }

    if (vfs_filp->private_data.ptr == xipfs_infos_file) {
        i = 0;
        while (i < nbytes && i < sizeof(xipfs_mount_t)) {
            ((char *)dest)[i] = ((char *)vfs_filp->mp)[i];
            i++;
        }
        return i;
    }

    if ((size = xipfs_file_get_size(vfs_filp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    i = 0;
    while (i < nbytes && vfs_filp->pos < size) {
        if (xipfs_file_read_8(vfs_filp, &((char *)dest)[i]) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
        vfs_filp->pos++;
        i++;
    }

    return i;
}

static ssize_t xipfs_read(vfs_file_t *vfs_filp, void *dest,
                          size_t nbytes)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_read0(vfs_filp, dest, nbytes);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static ssize_t xipfs_write0(vfs_file_t *vfs_filp, const void *src,
                            size_t nbytes)
{
    off_t max_pos;
    size_t i;

    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }
    if (src == NULL) {
        return -EFAULT;
    }

    if (vfs_filp->private_data.ptr == xipfs_infos_file) {
        /* cannot write(2) this virtual file */
        return -EBADF;
    }

    if ((max_pos = xipfs_file_get_max_pos(vfs_filp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    i = 0;
    while (i < nbytes && vfs_filp->pos < max_pos) {
        if (xipfs_file_write_8(vfs_filp, ((char *)src)[i]) < 0) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
        vfs_filp->pos++;
        i++;
    }

    return i;
}

static ssize_t xipfs_write(vfs_file_t *vfs_filp, const void *src,
                           size_t nbytes)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_write0(vfs_filp, src, nbytes);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_fsync0(vfs_file_t *vfs_filp)
{
    if (vfs_filp == NULL) {
        return -EBADF;
    }
    if (vfs_open_files_is_tracked(vfs_filp) < 0) {
        return -EBADF;
    }

    if (xipfs_file_set_size(vfs_filp, vfs_filp->pos) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    return 0;
}

static int xipfs_fsync(vfs_file_t *vfs_filp)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_fsync0(vfs_filp);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

/*
 * Operations on open directories
 */

static int xipfs_opendir0(vfs_DIR *dirp, const char *dirname)
{
    xipfs_dirent_t *direntp;
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    xipfs_file_t *headp;
    size_t len;
    int ret;

    if (dirp == NULL) {
        return -EFAULT;
    }
    xipfs_mp = (xipfs_mount_t *)dirp->mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (dirname == NULL) {
        return -EFAULT;
    }
    if (dirname[0] == '\0') {
        return -ENOENT;
    }
    len = strnlen(dirname, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    direntp = (xipfs_dirent_t *)&dirp->private_data.ptr;

    xipfs_errno = XIPFS_OK;
    if ((headp = xipfs_fs_head(xipfs_mp)) == NULL) {
        if (xipfs_errno != XIPFS_OK) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
        /* this file system is empty, not an error */
        direntp->dirname[0] = '/';
        direntp->dirname[1] = '\0';
        direntp->filp = NULL;
        return 0;
    }

    if (dirname[0] == '/' && dirname[1] == '\0') {
        /* the root of the file system is always present */
        direntp->dirname[0] = '/';
        direntp->dirname[1] = '\0';
        direntp->filp = headp;
        return 0;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, dirname) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        break;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    /* it is safe to use strcpy(3) here */
    (void)strcpy(direntp->dirname, dirname);
    direntp->filp = headp;

    len = xipath.len;
    if (direntp->dirname[len-1] != '/') {
        if (len+1 == XIPFS_PATH_MAX) {
            return -ENAMETOOLONG;
        }
        /* ensure dirname ends with a slash to indicate
         * it's a directory */
        direntp->dirname[len] = '/';
        direntp->dirname[len+1] = '\0';
    }

    return 0;
}

static int xipfs_opendir(vfs_DIR *dirp, const char *dirname)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_opendir0(dirp, dirname);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_readdir0(vfs_DIR *dirp, vfs_dirent_t *entry)
{
    xipfs_dirent_t *direntp;
    xipfs_mount_t *xipfs_mp;
    size_t i, j;
    int ret;

    if (dirp == NULL) {
        return -EFAULT;
    }
    xipfs_mp = (xipfs_mount_t *)dirp->mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    } 
    if (entry == NULL) {
        return -EFAULT;
    }

    direntp = (xipfs_dirent_t *)&dirp->private_data.ptr;
    xipfs_errno = XIPFS_OK;
    while (direntp->filp != NULL) {
        i = 0;
        while (i < XIPFS_PATH_MAX) {
            if (direntp->filp->path[i] != direntp->dirname[i]) {
                break;
            }
            if (direntp->dirname[i] == '\0') {
                break;
            }
            if (direntp->filp->path[i] == '\0') {
                break;
            }
            i++;
        }
        if (i == XIPFS_PATH_MAX) {
            return -ENAMETOOLONG;
        }
        if (direntp->dirname[i] == '\0') {
            if (direntp->filp->path[i] == '\0') {
                /* empty directory */
                return 0;
            }
            if (direntp->filp->path[i] == '/') {
                if (i == XIPFS_PATH_MAX-1) {
                    return -ENAMETOOLONG;
                }
                if (direntp->filp->path[i+1] == '\0') {
                    /* empty directory */
                    return 0;
                }
            }
            if (direntp->filp->path[i] == '/') {
                /* skip first slash */
                i++;
            }
            if (already_display(dirp, direntp->filp, i) < 0) {
                j = i;
                while (j < XIPFS_PATH_MAX) {
                    if (direntp->filp->path[j] == '\0') {
                        entry->d_name[j-i] = '\0';
                        break;
                    }
                    if (direntp->filp->path[j] == '/') {
                        entry->d_name[j-i] = '/';
                        entry->d_name[j-i+1] = '\0';
                        break;
                    }
                    entry->d_name[j-i] = direntp->filp->path[j];
                    j++;
                }
                if (j == XIPFS_PATH_MAX) {
                    return -ENAMETOOLONG;
                }
                /* d_ino is not supported */
                entry->d_ino = 0;
                /* set the next file to the structure */
                if ((direntp->filp = xipfs_fs_next(direntp->filp)) == NULL) {
                    if (xipfs_errno != XIPFS_OK) {
                        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                        return -EIO;
                    }
                }
                /* entry was updated */
                return 1;
            }
        }
        direntp->filp = xipfs_fs_next(direntp->filp);
    }
    if (xipfs_errno != XIPFS_OK) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }
    /* end of the directory */
    return 0;
}

static int xipfs_readdir(vfs_DIR *dirp, vfs_dirent_t *entry)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_readdir0(dirp, entry);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_closedir0(vfs_DIR *dirp)
{
    (void)memset(dirp, 0, sizeof(*dirp));

    return 0;
}

static int xipfs_closedir(vfs_DIR *dirp)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_closedir0(dirp);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

/*
 * Operations on mounted file systems
 */

static int xipfs_format0(vfs_mount_t *vfs_mp)
{
    xipfs_mount_t *xipfs_mp;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (xipfs_fs_format(xipfs_mp) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }
    vfs_open_files_untrack_all(xipfs_mp);

    return 0;
}

static int xipfs_format(vfs_mount_t *vfs_mp)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_format0(vfs_mp);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_mount0(vfs_mount_t *vfs_mp)
{
    xipfs_mount_t *xipfs_mp;
    uint32_t *start;
    void *end;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    /* check file system integrity using last file pointer */
    xipfs_errno = XIPFS_OK;
    if (xipfs_fs_tail(xipfs_mp) == NULL) {
        if (xipfs_errno != XIPFS_OK) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
    }
    /* ensure pages after the last file are erased */
    if ((start = (uint32_t *)xipfs_fs_tail_next(xipfs_mp)) == NULL) {
        if (xipfs_errno != XIPFS_OK) {
            DEBUG("%s\n", xipfs_strerror(xipfs_errno));
            return -EIO;
        }
    }
    end = (void *)((uintptr_t)xipfs_mp->vfs.private_data +
        xipfs_mp->nbpage * FLASHPAGE_SIZE);
    while ((uintptr_t)start < (uintptr_t)end) {
        if (*start++ != XIPFS_FLASH_ERASE_STATE) {
            return -EIO;
        }
    }

    return 0;
}

static int xipfs_mount(vfs_mount_t *vfs_mp)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_mount0(vfs_mp);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_umount(vfs_mount_t *vfs_mp)
{
    UNUSED(vfs_mp);

    /* nothing to do */

    return 0;
}

static int xipfs_unlink0(vfs_mount_t *vfs_mp, const char *name)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    size_t len;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (name == NULL) {
        return -EFAULT;
    }
    if (name[0] == '\0') {
        return -ENOENT;
    }
    if (name[0] == '/' && name[1] == '\0') {
        return -EISDIR;
    }
    len = strnlen(name, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, name) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
        break;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -EISDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    if (sync_remove_file(xipfs_mp, xipath.witness) < 0) {
        return -EIO;
    }
    if (xipath.parent == 1 && !(xipath.dirname[0] ==
            '/' && xipath.dirname[1] == '\0')) {
        if (xipfs_fs_new_file(xipfs_mp, xipath.dirname,
                FLASHPAGE_SIZE, 0) == NULL) {
            return -EIO;
        }
    }

    return 0;
}

static int xipfs_unlink(vfs_mount_t *vfs_mp, const char *name)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_unlink0(vfs_mp, name);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_mkdir0(vfs_mount_t *vfs_mp, const char *name,
                        mode_t mode)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    int ret;

    /* mode bits are ignored */
    UNUSED(mode);

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (name == NULL) {
        return -EFAULT;
    }
    if (name[0] == '\0') {
        return -ENOENT;
    }
    if (name[0] == '/' && name[1] == '\0') {
        return -EEXIST;
    }
    if (strnlen(name, XIPFS_PATH_MAX) == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, name) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -EEXIST;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
        return -ENOENT;
    case XIPFS_PATH_CREATABLE:
        break;
    default:
        return -EIO;
    }

    if (xipath.path[xipath.len-1] != '/') {
        if (xipath.len == XIPFS_PATH_MAX-1) {
            return -ENAMETOOLONG;
        }
        xipath.path[xipath.len++] = '/';
        xipath.path[xipath.len  ] = '\0';
    }

    if (xipath.witness != NULL) {
        if (strcmp(xipath.witness->path, xipath.dirname) == 0) {
            if (sync_remove_file(xipfs_mp, xipath.witness) < 0) {
                return -EIO;
            }
        }
    }

    if (xipfs_fs_new_file(xipfs_mp, xipath.path,
            FLASHPAGE_SIZE, 0) == NULL) {
        return -EIO;
    }

    return 0;
}

static int xipfs_mkdir(vfs_mount_t *vfs_mp, const char *name,
                       mode_t mode)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_mkdir0(vfs_mp, name, mode);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_rmdir0(vfs_mount_t *vfs_mp, const char *name)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    size_t len;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (name == NULL) {
        return -EFAULT;
    }
    if (name[0] == '\0') {
        return -ENOENT;
    }
    if (name[0] == '/' && name[1] == '\0') {
        return -EBUSY;
    }
    len = strnlen(name, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    if (name[len-1] == '.') {
        return -EINVAL;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, name) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
        break;
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -ENOTEMPTY;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    if (sync_remove_file(xipfs_mp, xipath.witness) < 0) {
        return -EIO;
    }
    if (xipath.parent == 1 && !(xipath.dirname[0] ==
            '/' && xipath.dirname[1] == '\0')) {
        if (xipfs_fs_new_file(xipfs_mp, xipath.dirname,
                FLASHPAGE_SIZE, 0) == NULL) {
            return -EIO;
        }
    }

    return 0;
}

static int xipfs_rmdir(vfs_mount_t *vfs_mp, const char *name)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_rmdir0(vfs_mp, name);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_rename0(vfs_mount_t *vfs_mp, const char *from_path,
                         const char *to_path)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipaths[2];
    const char *paths[2];
    size_t renamed;
    ssize_t ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (from_path == NULL) {
        return -EFAULT;
    }
    if (to_path == NULL) {
        return -EFAULT;
    }
    if (from_path[0] == '\0') {
        return -ENOENT;
    }
    if (to_path[0] == '\0') {
        return -ENOENT;
    }
    ret = strnlen(from_path, XIPFS_PATH_MAX);
    if (ret == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    ret = strnlen(to_path, XIPFS_PATH_MAX);
    if (ret == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    paths[0] = from_path;
    paths[1] = to_path;
    if (xipfs_path_new_n(xipfs_mp, xipaths, paths, 2) < 0) {
        return -EIO;
    }

    switch (xipaths[0].info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
    {
        switch (xipaths[1].info) {
        case XIPFS_PATH_EXISTS_AS_FILE:
        {
            if (xipaths[0].witness == xipaths[1].witness) {
                return 0;
            }
            if (xipfs_file_rename(xipaths[0].witness,
                    xipaths[1].path) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = 1;
            break;
        }
        case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
        case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
            return -EISDIR;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
            return -ENOTDIR;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
            return -ENOENT;
        case XIPFS_PATH_CREATABLE:
        {
            if (xipaths[1].path[xipaths[1].len-1] == '/') {
                return -ENOTDIR;
            }
            if (xipfs_file_rename(xipaths[0].witness,
                    xipaths[1].path) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = 1;
            break;
        }
        default:
            return -EIO;
        }
        break;
    }
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    {
        switch (xipaths[1].info) {
        case XIPFS_PATH_EXISTS_AS_FILE:
            return -ENOTDIR;
        case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
        {
            if (xipaths[0].witness == xipaths[1].witness) {
                return 0;
            }
            if (xipfs_file_rename(xipaths[0].witness,
                    xipaths[1].path) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = 1;
            break;
        }
        case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
            return -ENOTEMPTY;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
            return -ENOTDIR;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
            return -ENOENT;
        case XIPFS_PATH_CREATABLE:
        {
            /* from is an empty directory */
            if (xipaths[1].path[xipaths[1].len-1] != '/') {
                if (xipaths[1].len == XIPFS_PATH_MAX-1) {
                    return -ENAMETOOLONG;
                }
                xipaths[1].path[xipaths[1].len++] = '/';
                xipaths[1].path[xipaths[1].len  ] = '\0';
            }
            /* check whether an attempt was made to make a
             * directory a subdirectory of itself */
            if (strncmp(xipaths[0].path, xipaths[1].path,
                    xipaths[0].len) == 0) {
                return -EINVAL;
            }
            if (xipfs_file_rename(xipaths[0].witness,
                    xipaths[1].path) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = 1;
            break;
        }
        default:
            return -EIO;
        }
        break;
    }
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
    {
        switch (xipaths[1].info) {
        case XIPFS_PATH_EXISTS_AS_FILE:
            return -ENOTDIR;
        case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
        {
            /* check whether an attempt was made to make a
             * directory a subdirectory of itself */
            if (strncmp(xipaths[0].path, xipaths[1].path,
                    xipaths[0].len) == 0) {
                return -EINVAL;
            }
            if ((ret = xipfs_fs_rename_all(xipfs_mp, xipaths[0].path,
                    xipaths[1].path)) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = (size_t)ret;
            break;
        }   
        case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
            return -ENOTEMPTY;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
            return -ENOTDIR;
        case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
            return -ENOENT;
        case XIPFS_PATH_CREATABLE:
        {
            /* from is an nonempty directory */
            if (xipaths[1].path[xipaths[1].len-1] != '/') {
                if (xipaths[1].len == XIPFS_PATH_MAX-1) {
                    return -ENAMETOOLONG;
                }
                xipaths[1].path[xipaths[1].len++] = '/';
                xipaths[1].path[xipaths[1].len  ] = '\0';
            }
            /* check whether an attempt was made to make a
             * directory a subdirectory of itself */
            if (strncmp(xipaths[0].path, xipaths[1].path,
                    xipaths[0].len) == 0) {
                return -EINVAL;
            }
            if ((ret = xipfs_fs_rename_all(xipfs_mp, xipaths[0].path,
                    xipaths[1].path)) < 0) {
                DEBUG("%s\n", xipfs_strerror(xipfs_errno));
                return -EIO;
            }
            renamed = (size_t)ret;
            break;
        }
        default:
            return -EIO;
        }
        break;
    }
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    if (xipaths[0].parent == renamed && !(xipaths[0].dirname[0] ==
            '/' && xipaths[0].dirname[1] == '\0')) {
        if (strcmp(xipaths[0].dirname, xipaths[1].dirname) != 0) {
            if (xipfs_fs_new_file(xipfs_mp, xipaths[0].dirname,
                    FLASHPAGE_SIZE, 0) == NULL) {
                return -EIO;
            }
        }
    }

    if (xipaths[1].witness != NULL) {
        if (strcmp(xipaths[1].witness->path, xipaths[1].dirname) == 0) {
            if (sync_remove_file(xipfs_mp, xipaths[1].witness) < 0) {
                return -EIO;
            }
        }
    }

    return 0;
}

static int xipfs_rename(vfs_mount_t *vfs_mp, const char *from_path,
                        const char *to_path)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_rename0(vfs_mp, from_path, to_path);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_stat0(vfs_mount_t *vfs_mp, const char *path,
                       struct stat *buf)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    mode_t st_mode;
    size_t len;
    off_t size;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (path == NULL) {
        return -EFAULT;
    }
    if (buf == NULL) {
        return -EFAULT;
    }
    if (path[0] == '\0') {
        return -ENOENT;
    }
    len = strnlen(path, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, path) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
        st_mode = S_IFREG;
        break;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        st_mode = S_IFDIR;
        break;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    if (strncmp(xipath.witness->path, xipath.path, len) != 0) {
        return -ENOENT;
    }

    if ((size = xipfs_file_get_size_(xipath.witness)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    (void)memset(buf, 0, sizeof(*buf));
    buf->st_dev = (dev_t)(uintptr_t)vfs_mp;
    buf->st_ino = (ino_t)(uintptr_t)xipath.witness;
    buf->st_mode = st_mode;
    buf->st_nlink = 1;
    buf->st_size = size;
    buf->st_blksize = FLASHPAGE_SIZE;
    buf->st_blocks = xipath.witness->reserved / FLASHPAGE_SIZE;

    return 0;
}

static int xipfs_stat(vfs_mount_t *vfs_mp, const char *path,
                      struct stat *buf)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_stat0(vfs_mp, path, buf);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

static int xipfs_statvfs0(vfs_mount_t *vfs_mp, const char *path,
                          struct statvfs *buf)
{
    unsigned free_pages, page_number;
    xipfs_mount_t *xipfs_mp;
    int ret;

    UNUSED(path);

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (buf == NULL) {
        return -EFAULT;
    }

    if ((ret = xipfs_fs_get_page_number(xipfs_mp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }
    page_number = (unsigned)ret;

    if ((ret = xipfs_fs_free_pages(xipfs_mp)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }
    free_pages = (unsigned)ret;

    (void)memset(buf, 0, sizeof(*buf));
    buf->f_bsize = FLASHPAGE_SIZE;
    buf->f_frsize = FLASHPAGE_SIZE;
    buf->f_blocks = page_number;
    buf->f_bfree = free_pages;
    buf->f_bavail = free_pages;
    buf->f_flag = ST_NOSUID;
    buf->f_namemax = XIPFS_PATH_MAX;

    return 0;
}

static int xipfs_statvfs(vfs_mount_t *vfs_mp, const char *path,
                         struct statvfs *buf)
{
    int ret;

    mutex_lock(&xipfs_mutex);
    ret = xipfs_statvfs0(vfs_mp, path, buf);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

/*
 * xipfs-specific functions
 */

int xipfs_new_file0(vfs_mount_t *vfs_mp, const char *path,
                    uint32_t size, uint32_t exec)
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    size_t len;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (path == NULL) {
        return -EFAULT;
    }
    if (path[0] == '\0') {
        return -ENOENT;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return -EISDIR;
    }
    len = strnlen(path, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    if (exec != 0 && exec != 1) {
        return -EINVAL;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, path) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
        return -EEXIST;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -EISDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
        return -ENOENT;
    case XIPFS_PATH_CREATABLE:
        break;
    default:
        return -EIO;
    }

    if (xipath.path[xipath.len-1] == '/') {
        return -EISDIR;
    }
    if (xipath.witness != NULL && !(xipath.dirname[0] ==
            '/' && xipath.dirname[1] == '\0')) {
        if (strcmp(xipath.witness->path, xipath.dirname) == 0) {
            if (sync_remove_file(xipfs_mp, xipath.witness) < 0) {
                return -EIO;
            }
        }
    }
    if (xipfs_fs_new_file(xipfs_mp, path, size, exec) == NULL) {
        /* file creation failed */
        if (xipfs_errno == XIPFS_ENOSPACE ||
            xipfs_errno == XIPFS_EFULL) {
            return -EDQUOT;
        }
        return -EIO;
    }

    return 0;
}

int xipfs_new_file(const char *full_path, uint32_t size, uint32_t exec)
{
    xipfs_mount_t xipfs_mp;
    vfs_mount_t *vfs_mp;
    const char *path;
    int ret;

    if (full_path == NULL) {
        return -EFAULT;
    }

    if ((ret = get_xipfs_mp(full_path, &xipfs_mp)) < 0) {
        return ret;
    }
    vfs_mp = (vfs_mount_t *)&xipfs_mp;

    if ((path = get_rel_path(vfs_mp, full_path)) == NULL) {
        return -EIO;
    }

    mutex_lock(&xipfs_mutex);
    ret = xipfs_new_file0(vfs_mp, path, size, exec);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

int xipfs_execv0(vfs_mount_t *vfs_mp, const char *path,
                 char *const argv[])
{
    xipfs_mount_t *xipfs_mp;
    xipfs_path_t xipath;
    size_t len;
    int ret;

    xipfs_mp = (xipfs_mount_t *)vfs_mp;
    if ((ret = xipfs_mp_check(xipfs_mp)) < 0) {
        return ret;
    }
    if (path == NULL) {
        return -EFAULT;
    }
    if (path[0] == '\0') {
        return -ENOENT;
    }
    if (path[0] == '/' && path[1] == '\0') {
        return -EISDIR;
    }
    len = strnlen(path, XIPFS_PATH_MAX);
    if (len == XIPFS_PATH_MAX) {
        return -ENAMETOOLONG;
    }
    if (argv == NULL) {
        return -EFAULT;
    }

    if (xipfs_path_new(xipfs_mp, &xipath, path) < 0) {
        return -EIO;
    }
    switch (xipath.info) {
    case XIPFS_PATH_EXISTS_AS_FILE:
        break;
    case XIPFS_PATH_EXISTS_AS_EMPTY_DIR:
    case XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR:
        return -EISDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS:
        return -ENOTDIR;
    case XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND:
    case XIPFS_PATH_CREATABLE:
        return -ENOENT;
    default:
        return -EIO;
    }

    switch (xipath.witness->exec) {
    case 0:
        return -EACCES;
    case 1:
        break;
    default:
        return -EINVAL;
    }

    if ((ret = xipfs_file_exec(xipath.witness, argv)) < 0) {
        DEBUG("%s\n", xipfs_strerror(xipfs_errno));
        return -EIO;
    }

    return ret;
}

int xipfs_execv(const char *full_path, char *const argv[])
{
    xipfs_mount_t xipfs_mp;
    vfs_mount_t *vfs_mp;
    const char *path;
    int ret;

    if (full_path == NULL) {
        return -EFAULT;
    }

    if ((ret = get_xipfs_mp(full_path, &xipfs_mp)) < 0) {
        return ret;
    }
    vfs_mp = (vfs_mount_t *)&xipfs_mp;

    if ((path = get_rel_path(vfs_mp, full_path)) == NULL) {
        return -EIO;
    }

    mutex_lock(&xipfs_mutex);
    ret = xipfs_execv0(vfs_mp, path, argv);
    mutex_unlock(&xipfs_mutex);

    return ret;
}

/*
 * File system driver structures
 */

static const vfs_file_ops_t xipfs_file_ops = {
    .close = xipfs_close,
    .fstat = xipfs_fstat,
    .lseek = xipfs_lseek,
    .open = xipfs_open,
    .read = xipfs_read,
    .write = xipfs_write,
    .fsync = xipfs_fsync,
};

static const vfs_dir_ops_t xipfs_dir_ops = {
    .opendir = xipfs_opendir,
    .readdir = xipfs_readdir,
    .closedir = xipfs_closedir,
};

static const vfs_file_system_ops_t xipfs_fs_ops = {
    .format = xipfs_format,
    .mount = xipfs_mount,
    .umount = xipfs_umount,
    .unlink = xipfs_unlink,
    .mkdir = xipfs_mkdir,
    .rmdir = xipfs_rmdir,
    .rename = xipfs_rename,
    .stat = xipfs_stat,
    .statvfs = xipfs_statvfs,
};

const vfs_file_system_t xipfs_file_system = {
    .fs_op = &xipfs_fs_ops,
    .f_op = &xipfs_file_ops,
    .d_op = &xipfs_dir_ops,
};
