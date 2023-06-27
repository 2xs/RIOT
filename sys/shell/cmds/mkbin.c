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
 * @brief       Shell command to allocate the space needed to load a
 *              binary
 *
 * @}
 */

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "gnrc_xipfs.h"
#include "shell.h"

static void
usage(char *cmd)
{
    printf("%s: name size exec\n", cmd);
}

static int
convert(const char *str, uint32_t *val)
{
    char *endptr;
    long l;

    errno = 0;

    l = strtol(str, &endptr, 10);

    if (l == LONG_MIN && errno != 0)
        return -1;

    if (l == LONG_MAX && errno != 0)
        return -1;

    if (endptr == str)
        return -1;

    if ((long unsigned int)l > UINT32_MAX)
        return -1;

    if (l < 0)
        return -1;

    if (*endptr != '\0')
        return -1;

    *val = (uint32_t)l;

    return 0;
}

static int
_mkbin(int argc, char **argv)
{
    uint32_t size, exec;

    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    if (tinyfs_file_search(argv[1]) != NULL) {
        fprintf(stderr, "%s: %s: file name already used\n", argv[0],
            argv[1]);
        return 1;
    }

    if (convert(argv[2], &size) != 0) {
        fprintf(stderr, "%s: %s: invalid size\n", argv[0], argv[1]);
        return 1;
    }

    if (convert(argv[3], &exec) != 0) {
        fprintf(stderr, "%s: %s: invalid rights\n", argv[0], argv[1]);
        return 1;
    }

    if (exec != 0 && exec != 1) {
        fprintf(stderr, "%s: %s: invalid rights\n", argv[0], argv[1]);
        return 1;
    }

    if (tinyfs_create_file(argv[1], size, exec, TINYFS_STATUS_CREATED) == NULL) {
        fprintf(stderr, "%s: %s: unable to create file\n", argv[0],
            argv[1]);
        return 1;
    }

    return 0;
}

SHELL_COMMAND(mkbin, "allocate the space needed to load a binary", _mkbin);
