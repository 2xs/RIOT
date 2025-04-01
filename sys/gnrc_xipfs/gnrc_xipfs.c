/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_gnrc_xipfs
 * @{
 *
 * @file
 * @brief       xipfs driver implementation
 *
 * @}
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "periph/flashpage.h"
#include "gnrc_xipfs.h"

#define FREE_PAGE (0xffffffff)

#define FILESIZE_MAX ((1 << 29) - 1)

#define TINYFS_INITIALIZED 1

#define FLASHPAGE_ALIGNED(x) \
    ((((uintptr_t)x) & (FLASHPAGE_SIZE - 1)) == 0)

#define ROUND(x, y) (((x) + (y) - 1) & ~((y) - 1))

#define TRUNC(x, y) ((x) & ~((y) - 1))

#define SET_BITS(x) ((1 << (x)) - 1)

static void *first_page = NULL;
static void *last_page = NULL;
static int module_init = 0;

/* Internals */

static void
flashpage_write_unaligned(void *target_addr, const void *data, size_t len)
{
    uint32_t byte_shift, address, address32, value32;
    char value;
    size_t i;

    for (i = 0; i < len; i++) {
        address = (uint32_t)target_addr + i;
        value = ((uint8_t *)data)[i];
        byte_shift = address & (uint32_t)(FLASHPAGE_WRITE_BLOCK_ALIGNMENT - 1);
        address32 = address & ~byte_shift;
        value32 = (*(uint32_t*)address32 & ~((uint32_t)FLASHPAGE_ERASE_STATE <<
            (byte_shift << (uint32_t)(FLASHPAGE_WRITE_BLOCK_SIZE - 1))));
        value32 = value32 + ((uint32_t)value << (byte_shift << 3));
        flashpage_write((void *)address32, &value32, FLASHPAGE_WRITE_BLOCK_SIZE);
    }
}

static int
_get_first_free_page_addr(void **ffp)
{
    file_t *p = first_page;

    assert(FLASHPAGE_ALIGNED(p));

    while (p->next != first_page) {
        if (p->next == (void *)FREE_PAGE) {
            *ffp = p;
            return 0;
        }
        assert(FLASHPAGE_ALIGNED(p->next));
        p = p->next;
    }

    return -1;
}

static int
_tinyfs_flash_needed(size_t page)
{
    uint32_t *addr = flashpage_addr(page);
    size_t i;

    for (i = 0; i < (FLASHPAGE_SIZE/4); i++)
        if (addr[i] != FREE_PAGE)
            return 1;

    return 0;
}

static void
_tinyfs_flash_pages(unsigned int page, unsigned int number)
{
    size_t i;

    for (i = 0; i < number; i++)
        if (_tinyfs_flash_needed(page + i))
            flashpage_erase(page + i);
}

static void
_tinyfs_flash_file(file_t *file)
{
    unsigned int page, number;

    page = flashpage_page(file);
    number = ROUND(file->size, FLASHPAGE_SIZE);
    number /= FLASHPAGE_SIZE;

    _tinyfs_flash_pages(page, number);
}

static void
_tinyfs_flash_file_content(file_t *file)
{
    file_t backup;

    (void)memcpy(&backup, file, sizeof(*file));
    backup.status = TINYFS_STATUS_CREATED;

    _tinyfs_flash_file(file);

    flashpage_write(file, &backup, sizeof(backup));
}

static void
_tinyfs_cleanup(void)
{
    file_t *file;

    if ((file = tinyfs_get_first_file()) == NULL)
        return;

    if (file->status == TINYFS_STATUS_LOADING)
        _tinyfs_flash_file_content(file);

    while ((file = tinyfs_get_next_file(file)) != NULL)
        if (file->status == TINYFS_STATUS_LOADING)
            _tinyfs_flash_file_content(file);
}

/* Public API */

extern file_t *
tinyfs_get_first_file(void)
{
    file_t *file = first_page;

    if (module_init != TINYFS_INITIALIZED)
        return NULL;

    if (file->next == (void *)FREE_PAGE)
        return NULL;

    return file;
}

extern file_t *
tinyfs_get_next_file(file_t *file)
{
    file = file->next;

    if (module_init != TINYFS_INITIALIZED)
        return NULL;

    if (file->next == (void *)FREE_PAGE)
        return NULL;

    if (file->next == first_page)
        return NULL;

    return file;
}

extern void
tinyfs_format(void)
{
    unsigned int start, end;

    if (module_init != TINYFS_INITIALIZED)
        return;

    start = flashpage_page(first_page);
    end = flashpage_page(last_page);

    _tinyfs_flash_pages(start, end-start);
}

extern int
tinyfs_init(void *flash_start, void *flash_end)
{
    /* computation of the first and last flash page available */
    first_page = (void *)ROUND((uintptr_t)flash_start, FLASHPAGE_SIZE);
    last_page = (void *)TRUNC((uintptr_t)flash_end, FLASHPAGE_SIZE);
    last_page = (void *)((uintptr_t)last_page - FLASHPAGE_SIZE);

    /* the first and last flash pages do not overlap */
    assert((uintptr_t)first_page < (uintptr_t)last_page);

    /* clean up files that were loading */
    _tinyfs_cleanup();

    /* the module is correctly initialized */
    module_init = TINYFS_INITIALIZED;

    return 0;
}

extern file_t *
tinyfs_create_file(const char *name, uint32_t size, uint32_t exec,
                   enum tinyfs_status_e status)
{
    size_t page_size;
    void *ffp, *next;
    file_t file;

    /* check if the module is initialized */
    if (module_init != TINYFS_INITIALIZED)
        return NULL;

    /* check if the file size is zero */
    if (size == 0)
        return NULL;

    /* check if the file size is too large */
    if (size > FILESIZE_MAX)
        return NULL;

    /* check if rights are valid */
    if (exec != 0 && exec != 1)
        return NULL;

    /* check if no flash page left */
    if (_get_first_free_page_addr(&ffp) != 0)
        return NULL;

    page_size = ROUND(size, FLASHPAGE_SIZE);
    next = (char *)ffp + page_size;

    /* check if no flash page left */
    if (next == last_page)
        next = first_page;

    (void)memset(&file, 0, sizeof(file));
    (void)strncpy(file.name, name, TINYFS_NAME_MAX - 1);
    file.size = size;
    file.status = status;
    file.next = next;
    file.exec = exec;

    flashpage_write(ffp, &file, sizeof(file));

    return (file_t *)ffp;
}

extern int
tinyfs_file_status(file_t *file, enum tinyfs_status_e status)
{
    if (file->status == TINYFS_STATUS_FREE) {
        flashpage_write(&file->status, &status, sizeof(status));
        return 0;
    }

    if (file->status == TINYFS_STATUS_CREATED) {
        if (status == TINYFS_STATUS_FREE)
            return -1;
        flashpage_write(&file->status, &status, sizeof(status));
    }

    if (file->status == TINYFS_STATUS_LOADING) {
        if (status == TINYFS_STATUS_FREE)
            return -1;
        if (status == TINYFS_STATUS_CREATED)
            return -1;
        flashpage_write(&file->status, &status, sizeof(status));
        return 0;
    }

    return -1;
}

extern int
tinyfs_file_write(file_t *file, uint32_t offset, const void *src, size_t n)
{
    void *dst;

    if (n == 0)
        return 0;

    if (file->size < offset)
        return -1;

    if (file->size < (offset + n))
        return -1;

    dst = (char *)file + sizeof(*file) + offset;

    flashpage_write_unaligned(dst, src, n);

    return 0;
}

extern file_t *
tinyfs_file_search(const char *name)
{
    file_t *file;

    if (module_init != TINYFS_INITIALIZED)
        return NULL;

    if ((file = tinyfs_get_first_file()) == NULL)
        return NULL;

    if (strncmp(file->name, name, TINYFS_NAME_MAX) == 0)
        return file;

    while ((file = tinyfs_get_next_file(file)) != NULL)
        if (strncmp(file->name, name, TINYFS_NAME_MAX) == 0)
            return file;

    return NULL;
}

extern int
tinyfs_remove(const char *name)
{
    void *src, *dst, *next;
    size_t pagenum, i;
    unsigned int num;
    uint32_t size;
    file_t file;

    if (module_init != TINYFS_INITIALIZED) {
        return -1;
    }

    /* if no file with name name found */
    if ((dst = tinyfs_file_search(name)) == NULL) {
        return -1;
    }

    /* get next file if any */
    next = tinyfs_get_next_file(dst);

    /* remove the file */
    _tinyfs_flash_file(dst);

    /*
     * Consolidate the file system by moving files
     * after the deleted one.
     */
    while (next != NULL) {
        src = next;
        next = tinyfs_get_next_file(src);

        /* copy and fix up the file structure */
        (void)memcpy(&file, src, sizeof(file));
        size = ROUND(file.size, FLASHPAGE_SIZE);
        file.next = (char *)dst + size;
        flashpage_write(dst, &file, sizeof(file));

        /* copy the remaining of the file's first page */
        flashpage_write((char *)dst + sizeof(file), (char *)src +
            sizeof(file), FLASHPAGE_SIZE - sizeof(file));
        flashpage_erase(flashpage_page(src));
        dst = (char *)dst + FLASHPAGE_SIZE;
        src = (char *)src + FLASHPAGE_SIZE;

        /* first page of the file already copied */
        pagenum = size / FLASHPAGE_SIZE;
        for (i = 1; i < pagenum; i++) {
            num = flashpage_page(src);
            if (_tinyfs_flash_needed(num)) {
                flashpage_write(dst, src, FLASHPAGE_SIZE);
                flashpage_erase(num);
            }
            dst = (char *)dst + FLASHPAGE_SIZE;
            src = (char *)src + FLASHPAGE_SIZE;
        }
    }

    return 0;
}
