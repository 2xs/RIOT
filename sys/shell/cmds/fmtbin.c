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
 * @brief       Shell command to format the file system
 *
 * @}
 */

#include "gnrc_xipfs.h"
#include "shell.h"

static int
_fmtbin(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tinyfs_format();

    return 0;
}

SHELL_COMMAND(fmtbin, "format the file system", _fmtbin);
