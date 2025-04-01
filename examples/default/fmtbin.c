/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */
#include "gnrc_xipfs.h"

int _fmtbin_callback(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    tinyfs_format();

    return 0;
}
