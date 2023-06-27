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

static int
count_files(void)
{
    file_t *file;
    int i = 1;

    if ((file = tinyfs_get_first_file()) == NULL) {
        return 0;
    }

    while ((file = tinyfs_get_next_file(file)) != NULL) {
        i++;
    }

    return i;
}

static int
_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return count_files();
}

SHELL_COMMAND(df, "report file system flash space usage", _main);
