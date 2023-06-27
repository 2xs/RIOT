/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     sys_shell_commands
 * @{
 *
 * @file
 * @brief       Shell command to load a chunk of machine code
 *
 * @}
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

#include "gnrc_xipfs.h"
#include "shell.h"

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static uint32_t offset = 0;

static void
usage(char *cmd)
{
    printf("%s: name chunk\n", cmd);
}

static int
count_loading(void)
{
    int counter = 0;
    file_t *file;

    if ((file = tinyfs_get_first_file()) == NULL)
        return 0;

    if (file->status == TINYFS_STATUS_LOADING)
        counter++;

    while ((file = tinyfs_get_next_file(file)) != NULL)
        if (file->status == TINYFS_STATUS_LOADING)
            counter++;

    return counter;
}

static int
valid(char c)
{
    if (c >= 'A' && c <= 'Z')
        return 0;
    if (c >= 'a' && c <= 'z')
        return 0;
    if (c >= '0' && c <= '9')
        return 0;
    if (c == '+' || c == '/' || c == '=')
        return 0;
    return -1;
}

static int
check_chunk(const char *chunk)
{
    size_t i, len;

    len = strlen(chunk);

    if (len == 0)
        return -1;

    if ((len & 3) != 0)
        return -1;

    for (i = 0; i < len; i++)
        if (valid(chunk[i]) != 0)
            return -1;

    return 0;
}

static void
b64decode(file_t *file, const char *chunk, size_t len)
{
    uint32_t i = 0, bytes = 0, r = 3;
    const char *ptr;
    char buf[3];

    while (i < len && offset < file->size) {
        ptr = strchr(b64, chunk[i]);
        bytes |= (ptr - b64) << 18;

        ptr = strchr(b64, chunk[i+1]);
        bytes |= (ptr - b64) << 12;

        ptr = strchr(b64, chunk[i+2]);
        if (ptr != NULL)
            bytes |= (ptr - b64) << 6;
        else
            r--;

        ptr = strchr(b64, chunk[i + 3]);
        if (ptr != NULL)
            bytes |= ptr - b64;
        else
            r--;

        buf[0] = (bytes >> 16) & 0xff;
        buf[1] = (bytes >>  8) & 0xff;
        buf[2] = (bytes      ) & 0xff;

        tinyfs_file_write(file, offset, buf, r);

        offset += r;
        bytes = 0;
        i += 4;
        r = 3;
    }
}

static int
_ldbin(int argc, char **argv)
{
    int loading;
    file_t *file;

    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    if ((file = tinyfs_file_search(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: no such file\n", argv[0], argv[1]);
        return 1;
    }

    if (check_chunk(argv[2]) != 0) {
        fprintf(stderr, "%s: %s: invalid chunk\n", argv[0], argv[1]);
        return 1;
    }

    /* if nbld > 1 then the file system is corrupted... */
    assert((loading = count_loading()) <= 1);

    if (loading == 1 && file->status != TINYFS_STATUS_LOADING) {
        fprintf(stderr, "%s: another file is already being loaded\n",
            argv[0]);
        return 1;
    }

    if (file->status == TINYFS_STATUS_CREATED)
        assert(tinyfs_file_status(file, TINYFS_STATUS_LOADING) == 0);

    b64decode(file, argv[2], strlen(argv[2]));

    if (file->size == offset) {
        assert(tinyfs_file_status(file, TINYFS_STATUS_LOADED) == 0);
        offset = 0;
    }

    return 0;
}

SHELL_COMMAND(ldbin, "load a chunk of machine code", _ldbin);
