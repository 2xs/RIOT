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
 * @brief       Shell command to remove files
 *
 * @}
 */

#include <stdio.h>
#include <stdlib.h>

#include "gnrc_xipfs.h"
#include "shell.h"

static void
usage(char *cmd)
{
    printf("%s [FILE]...\n", cmd);
}

static int
_main(int argc, char **argv)
{
    int i;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        if (tinyfs_remove(argv[i]) != 0) {
            fprintf(stderr, "%s: cannot remove '%s': no such file\n",
                argv[0], argv[i]);
        }
    }

    return 0;
}

SHELL_COMMAND(rm, "remove files", _main);
