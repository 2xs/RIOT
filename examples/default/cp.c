/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "gnrc_xipfs.h"

int _cp_callback(int argc, char **argv)
{
    file_t *file, *new;

    if (argc < 3) {
        fprintf(stderr, "%s SOURCE DEST\n", argv[0]);
        return 1;
    }

    if ((file = tinyfs_file_search(argv[1])) == NULL) {
        fprintf(stderr, "%s: '%s': no such file\n", argv[0], argv[1]);
        return 1;
    }

    if (file->status != TINYFS_STATUS_LOADED) {
        fprintf(stderr, "%s: '%s': file is not loaded\n", argv[0],
            argv[1]);
        return 1;
    }

    if (strncmp(file->name, argv[2], TINYFS_NAME_MAX) == 0) {
        fprintf(stderr, "%s: '%s' and '%s' are the same file\n",
            argv[0], argv[1], argv[2]);
        return 1;
    }

    if ((new = tinyfs_create_file(argv[2], file->size, file->exec, TINYFS_STATUS_LOADED)) == NULL) {
        fprintf(stderr, "%s: '%s': failed to create the new file\n",
            argv[0], argv[2]);
        return 1;
    }

    if (tinyfs_file_write(new, 0, (char *)file + sizeof(*file), file->size) != 0) {
        fprintf(stderr, "%s: '%s': failed to copy file\n",
            argv[0], argv[2]);
        return 1;
    }

    return 0;
}
