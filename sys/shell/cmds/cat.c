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
 * @brief       Shell command to dump the whole binary
 *
 * @}
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>

#include "gnrc_xipfs.h"
#include "shell.h"

static void
usage(char *cmd)
{
    fprintf(stderr, "%s: name\n", cmd);
}

static int
_main(int argc, char **argv)
{
    file_t *file;
    size_t i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    if ((file = tinyfs_file_search(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: no such file\n", argv[0], argv[1]);
        return 1;
    }

    for (i = 0; i < file->size; i++) {
        printf("%c", ((char *)file + sizeof(*file))[i]);
    }
    printf("\n");

    return 0;
}

SHELL_COMMAND(cat, "print files on the standard output", _main);
