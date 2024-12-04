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
 * @brief       xipfs path implementation
 *
 * @author      Damien Amara <damien.amara@univ-lille.fr>
 *
 * @}
 */

/*
 * libc includes
 */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>

/*
 * RIOT include
 */
#include "fs/xipfs.h"

/*
 * xipfs includes
 */
#include "include/errno.h"
#include "include/fs.h"
#include "include/path.h"

/*
 * Helper functions
 */

/**
 * @internal
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre path_2 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Compares the two paths path_1 and path_2 passed as
 * parameters, character by character, and returns the index of
 * the first different character encountered
 *
 * @param path_1 A pointer to the first path to compare
 *
 * @param path_2 A pointer to the second path to compare
 *
 * @return Returns the index of the first different character
 * encountered
 */
static size_t compare_paths(const char *path_1,
                            const char *path_2)
{
    size_t i;

    assert(path_1 != NULL);
    assert(path_2 != NULL);

    i = 0;
    while (i < XIPFS_PATH_MAX) {
        if (path_1[i] != path_2[i]) {
            break;
        }
        if (path_1[i] == '\0') {
            break;
        }
        if (path_2[i] == '\0') {
            break;
        }
        i++;
    }

    return i;
}

/**
 * @internal
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre path_2 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Try to identify path_2, using path_1 and the index of
 * the first different character encountered between path_1 and
 * path_2, as an existing file
 *
 * @param path_1 A pointer to the path of an existing file
 *
 * @param path_2 A pointer to the path that needs to be
 * identified
 *
 * @param i The index of the first different character
 * encountered between path_1 and path_2
 *
 * @return Returns one if path_2 is identified as an existing
 * file, using path_1 and the index of the first different
 * character encountered between path_1 and path_2, zero
 * otherwise
 */
static int exists_as_file(const char *path_1,
                          const char *path_2,
                          size_t i)
{
    int c0 = 0;

    assert(path_1 != NULL);
    assert(path_2 != NULL);

    if (i > 0) {
        c0 = ( path_1[i-1] != '/'  ) &
             ( path_1[i-1] != '\0' ) &
             ( path_1[i  ] == '\0' ) &
             ( path_2[i-1] != '/'  ) &
             ( path_2[i-1] != '\0' ) &
             ( path_2[i  ] == '\0' );
    }

    return c0;
}

/**
 * @internal
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre path_2 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Try to identify path_2, using path_1 and the index of
 * the first different character encountered between path_1 and
 * path_2, as an existing empty directory
 *
 * @param path_1 A pointer to the path of an existing file
 *
 * @param path_2 A pointer to the path that needs to be
 * identified
 *
 * @param i The index of the first different character
 * encountered between path_1 and path_2
 *
 * @return Returns one if path_2 is identified as an existing
 * empty directory, using path_1 and the index of the first
 * different character encountered between path_1 and path_2,
 * zero otherwise
 */
static int exists_as_empty_dir(const char *path_1,
                               const char *path_2,
                               size_t i)
{
    int c0 = 0, c1 = 0;

    assert(path_1 != NULL);
    assert(path_2 != NULL);

    if (i > 0) {
        c0 = ( path_1[i-1] == '/'  ) &
             ( path_1[i  ] == '\0' ) &
             ( path_2[i-1] == '/'  ) &
             ( path_2[i  ] == '\0' );
    }
    if (i > 0 && i < XIPFS_PATH_MAX-1) {
        c1 = ( path_1[i-1] != '/'  )  &
             ( path_1[i-1] != '\0' )  &
             ( path_1[i  ] == '/'  )  &
             ( path_1[i+1] == '\0' )  &
             ( path_2[i-1] != '/'  )  &
             ( path_2[i-1] != '\0' )  &
             ( path_2[i  ] == '\0' );
    }

    return c0 | c1;
}

/**
 * @internal
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre path_2 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Try to identify path_2, using path_1 and the index of
 * the first different character encountered between path_1 and
 * path_2, as an existing non-empty directory
 *
 * @param path_1 A pointer to the path of an existing file
 *
 * @param path_2 A pointer to the path that needs to be
 * identified
 *
 * @param i The index of the first different character
 * encountered between path_1 and path_2
 *
 * @return Returns one if path_2 is identified as an existing
 * non-empty directory, using path_1 and the index of the first
 * different character encountered between path_1 and path_2,
 * zero otherwise
 */
static int exists_as_nonempty_dir(const char *path_1,
                                  const char *path_2,
                                  size_t i)
{
    int c0 = 0, c1 = 0;

    assert(path_1 != NULL);
    assert(path_2 != NULL);

    if (i > 0) {
        c0 = ( path_1[i-1] == '/'  ) &
             ( path_1[i  ] != '/'  ) &
             ( path_1[i  ] != '\0' ) &
             ( path_2[i-1] == '/'  ) &
             ( path_2[i  ] == '\0' );
    }
    if (i > 0 && i < XIPFS_PATH_MAX-1) {
        c1 = ( path_1[i-1] != '/'  )  &
             ( path_1[i-1] != '\0' )  &
             ( path_1[i  ] == '/'  )  &
             ( path_1[i+1] != '/'  )  &
             ( path_1[i+1] != '\0' )  &
             ( path_2[i-1] != '/'  )  &
             ( path_2[i-1] != '\0' )  &
             ( path_2[i  ] == '\0' );
    }

    return c0 | c1;
}

/**
 * @internal
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre path_2 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Try to identify path_2, using path_1 and the index of
 * the first different character encountered between path_1 and
 * path_2, as invalid because one of the parent in path_2 is not
 * a directory
 *
 * @param path_1 A pointer to the path of an existing file
 *
 * @param path_2 A pointer to the path that needs to be
 * identified
 *
 * @param i The index of the first different character
 * encountered between path_1 and path_2
 *
 * @return Returns one if path_2 is identified as invalid
 * because one of its parent is not a directory, using path_1
 * and the index of the first different character encountered
 * between path_1 and path_2, zero otherwise
 */
static int invalid_because_not_dirs(const char *path_1,
                                    const char *path_2,
                                    size_t i)
{
    int c0 = 0;

    assert(path_1 != NULL);
    assert(path_2 != NULL);

    if (i > 0 && i < XIPFS_PATH_MAX-1) {
        c0 = ( path_1[i-1] != '/'  ) &
             ( path_1[i-1] != '\0' ) &
             ( path_1[i  ] == '\0' ) &
             ( path_2[i-1] != '/'  ) &
             ( path_2[i-1] != '\0' ) &
             ( path_2[i  ] == '/'  ) &
             ( path_2[i+1] != '/'  ) &
             ( path_2[i+1] != '\0' );
    }

    return c0;
}

/**
 * @internal
 *
 * @warning Must be called after invalid_because_not_dirs
 * function
 *
 * @pre path_1 must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @pre dirname_2 must be a pointer that references a path which
 * is accessible, null-terminated, starts with a slash,
 * normalized, and shorter than XIPFS_PATH_MAX
 *
 * @brief Try to identify path_2, using the dirname of path_2,
 * its length and path_1, as creatable as a file or as an empty
 * directory
 *
 * @param path_1 A pointer to the path of an existing file
 *
 * @param dirname_2 A pointer to the dirname of path_2 that
 * needs to be identified
 *
 * @param dirname_2_len The length of dirname_2
 *
 * @return Returns one if path_2 is identified as creatable as
 * file or as empty directory, using the dirname of path_2, its
 * length and path_1, zero otherwise
 */
static int creatable(const char *path_1,
                     const char *dirname_2,
                     size_t dirname_2_len)
{
    assert(path_1 != NULL);
    assert(dirname_2 != NULL);

    /* Checks whether all components of xipfs_path exist */
    return strncmp(path_1, dirname_2, dirname_2_len) == 0;
}

/**
 * @internal
 *
 * @pre xipath must be a pointer that references an accessible
 * memory region
 *
 * @brief Strips the last component from an xipfs path
 *
 * @param xipath A pointer to a memory region containing an
 * accessible xipfs path structure
 */
static void xipfs_path_dirname(xipfs_path_t *xipath)
{
    size_t i;

    assert(xipath != NULL);

    if (xipath->path[0] == '/'  &&
        xipath->path[1] == '\0') {
        xipath->dirname[0] = '/';
        xipath->dirname[1] = '\0';
        return;
    }
    for (i = 0; i <= xipath->last_slash; i++) {
        xipath->dirname[i] = xipath->path[i];
    }
    xipath->dirname[i] = '\0';
}

/**
 * @internal
 *
 * @pre xipath must be a pointer that references an accessible
 * memory region
 *
 * @brief Strips the directory and suffix from an xipfs path
 *
 * @param xipath A pointer to a memory region containing an
 * accessible xipfs path structure
 */
static void
xipfs_path_basename(xipfs_path_t *xipath)
{
    char *src, *dst;

    assert(xipath != NULL);

    src = xipath->path;
    dst = xipath->basename;
    if (!(*src == '/' && *(src+1) == '\0')) {
        src += xipath->last_slash+1;
        while (*src != '/' && *src != '\0') {
            *dst++ = *src++;
        }
    } else {
        *dst++ = '/';
    }
    *dst = '\0';
}

/**
 * @internal
 *
 * @pre xipath must be a pointer that references an accessible
 * memory region
 *
 * @pre path must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Initializes an xipfs path data structure using the
 * specified path
 *
 * @param xipath A pointer to a memory region containing an
 * accessible xipfs path structure
 *
 * @param path a pointer to a path
 */
static void xipfs_path_init(xipfs_path_t *xipath,
                            const char *path)
{
    size_t len;

    assert(xipath != NULL);
    assert(path != NULL);

    (void)memset(xipath, 0, sizeof(*xipath));

    if (!(path[0] == '/' && path[1] == '\0')) {
        for (len = 0; path[len] != '\0'; len++) {
            if (path[len] == '/' && path[len+1] != '\0') {
                xipath->last_slash = len;
            }
            xipath->path[len] = path[len];
        }
        xipath->path[len] = '\0';
        xipath->len = len;
    } else {
        xipath->path[0] = '/';
        xipath->path[1] = '\0';
        xipath->last_slash = 0;
        xipath->len = 1;
    }
    xipfs_path_basename(xipath);
    xipfs_path_dirname(xipath);
}

/*
 * Extern functions
 */

/**
 * @pre xipfs_mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @pre xipaths must be a pointer that references an accessible
 * memory region
 *
 * @pre path must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Attempts to identify the nature of the paths provided
 * as arguments and saves the results in the xipfs path
 * structures
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 *
 * @param xipaths A pointer to a list of pointers to memory
 * regions containing xipfs path structures
 *
 * @param path A pointer to a list of pointers to memory
 * regions containing paths
 *
 * @param n The number of elements in both xipath and path
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
int xipfs_path_new_n(xipfs_mount_t *xipfs_mp,
                     xipfs_path_t *xipaths,
                     const char **paths,
                     size_t n)
{
    xipfs_file_t *filp;
    size_t i, j;

    for (j = 0; j < n; j++) {
        if (paths[j][0] == '\0') {
            return -1;
        }
        if (paths[j][0] != '/') {
            return -1;
        }
        xipfs_path_init(&xipaths[j], paths[j]);
    }

    xipfs_errno = XIPFS_OK;
    if ((filp = xipfs_fs_head(xipfs_mp)) != NULL) {
        /* one file at least */
        do {
            for (j = 0; j < n; j++) {
                if (strncmp(xipaths[j].path, filp->path,
                        xipaths[j].last_slash) == 0) {
                    xipaths[j].parent++;
                }
                if (xipaths[j].info == XIPFS_PATH_UNDEFINED ||
                    xipaths[j].info == XIPFS_PATH_CREATABLE) {
                    if ((i = compare_paths(filp->path, xipaths[j].path))
                            == XIPFS_PATH_MAX) {
                        return -1;
                    }
                    if (exists_as_file(filp->path, xipaths[j].path, i)) {
                        xipaths[j].info = XIPFS_PATH_EXISTS_AS_FILE;
                        xipaths[j].witness = filp;
                    } else if (exists_as_empty_dir(filp->path,
                                   xipaths[j].path, i)) {
                        if (xipaths[j].path[xipaths[j].len-1] != '/') {
                            if (xipaths[j].len == XIPFS_PATH_MAX-1) {
                                return -ENAMETOOLONG;
                            }
                            xipaths[j].path[xipaths[j].len++] = '/';
                            xipaths[j].path[xipaths[j].len  ] = '\0';
                        }
                        xipaths[j].info = XIPFS_PATH_EXISTS_AS_EMPTY_DIR;
                        xipaths[j].witness = filp;
                    } else if (exists_as_nonempty_dir(filp->path,
                                   xipaths[j].path, i)) {
                        if (xipaths[j].path[xipaths[j].len-1] != '/') {
                            if (xipaths[j].len == XIPFS_PATH_MAX-1) {
                                return -ENAMETOOLONG;
                            }
                            xipaths[j].path[xipaths[j].len++] = '/';
                            xipaths[j].path[xipaths[j].len  ] = '\0';
                        }
                        xipaths[j].info = XIPFS_PATH_EXISTS_AS_NONEMPTY_DIR;
                        xipaths[j].witness = filp;
                    } else if (invalid_because_not_dirs(filp->path,
                                   xipaths[j].path, i)) {
                        xipaths[j].info = XIPFS_PATH_INVALID_BECAUSE_NOT_DIRS;
                        xipaths[j].witness = filp;
                    } else if (creatable(filp->path, xipaths[j].path,
                                   xipaths[j].last_slash+1)) {
                        xipaths[j].info = XIPFS_PATH_CREATABLE;
                        xipaths[j].witness = filp;
                    }
                }
            }
        } while ((filp = xipfs_fs_next(filp)) != NULL);
    }
    else if (xipfs_errno == XIPFS_OK) {
        /*
         * No file in the file system. This means that there is
         * no witness to confirm that a path exists, that a file
         * or directory can be created, or that the path is
         * invalid.
         */
        for (j = 0; j < n; j++) {
            size_t last_slash = (xipaths[j].last_slash > 0) ? xipaths[j].last_slash-1 : 0;
            if (creatable("/", xipaths[j].path, last_slash)) {
                xipaths[j].info = XIPFS_PATH_CREATABLE;
                xipaths[j].witness = NULL;
            }
        }
    } else {
        /*
         * An error occurred in the low-level layers and
         * xipfs_errno was updated accordingly.
         */
        return -1;
    }
    /*
     * If the type of the path is still undefined upon reaching
     * this point. It means that one or more of its components,
     * other than the last one, do not exist.
     */
    for (j = 0; j < n; j++) {
        if (xipaths[j].info == XIPFS_PATH_UNDEFINED) {
            xipaths[j].info = XIPFS_PATH_INVALID_BECAUSE_NOT_FOUND;
            xipaths[j].witness = NULL;
        }
    }

    return 0;
}

/**
 * @pre xipfs_mp must be a pointer that references a memory
 * region containing an xipfs mount point structure which is
 * accessible and valid
 *
 * @pre xipath must be a pointer that references an accessible
 * memory region
 *
 * @pre path must be a pointer that references a path which is
 * accessible, null-terminated, starts with a slash, normalized,
 * and shorter than XIPFS_PATH_MAX
 *
 * @brief Wrapper to the xipfs_path_new_n function
 *
 * @param xipfs_mp A pointer to a memory region containing an
 * xipfs mount point structure
 *
 * @param xipath A pointer to a memory region containing an
 * xipfs path structure
 *
 * @param path A pointer to a memory region containing a path
 *
 * @return Returns zero if the function succeeds or a negative
 * value otherwise
 */
int xipfs_path_new(xipfs_mount_t *xipfs_mp,
                   xipfs_path_t *xipath,
                   const char *path)
{
    return xipfs_path_new_n(xipfs_mp, xipath, &path, 1);
}
