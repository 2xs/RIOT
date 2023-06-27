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
 * @brief       Meta shell command to run a script from the host to
 *              the board.
 *
 * @}
 */

#include "shell.h"

/**
 * This meta shell command is not implemented here. See
 * dist/tools/pyterm/pyterm script for more informations.
 */
static int
_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    return 0;
}

SHELL_COMMAND(run, "run a script from the host to the board", _main);
