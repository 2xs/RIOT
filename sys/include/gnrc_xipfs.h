/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @defgroup    sys_gnrc_xipfs gnrc_xipfs
 * @ingroup     sys
 * @brief       An execute in place file system
 *
 * @{
 *
 * @file        xipfs driver implementation header
 *
 */

#ifndef GNRC_XIPFS_H
#define GNRC_XIPFS_H

/* Add header includes here */

#include <stddef.h>
#include <stdint.h>

#define TINYFS_NAME_MAX 32

#ifdef __cplusplus
extern "C" {
#endif

/* Declare the API of the module */

enum tinyfs_status_e {
    TINYFS_STATUS_LOADED = 0,
    TINYFS_STATUS_LOADING = 1,
    TINYFS_STATUS_CREATED = 3,
    TINYFS_STATUS_FREE = 0xffffffff,
};

typedef struct file_s
{
    char name[TINYFS_NAME_MAX];
    uint32_t size;
    void *next;
    uint32_t status;
    uint32_t exec;
} file_t;

extern file_t *tinyfs_get_first_file(void);

extern file_t *tinyfs_get_next_file(file_t *file);

extern void tinyfs_format(void);

extern int tinyfs_init(void *flash_start, void *flash_end);

extern file_t *tinyfs_create_file(const char *name, uint32_t size, uint32_t exec, enum tinyfs_status_e status);

extern int tinyfs_file_status(file_t *file, enum tinyfs_status_e status);

extern int tinyfs_file_write(file_t *file, uint32_t offset, const void *src, size_t n);

extern file_t *tinyfs_file_search(const char *name);

extern int tinyfs_remove(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* GNRC_XIPFS_H */
/** @} */
