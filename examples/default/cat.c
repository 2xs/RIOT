/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */
#include <stdio.h>

#include "gnrc_xipfs.h"

int _cat_callback(int argc, char **argv)
{
    file_t *file;
    size_t i;

    if (argc < 2) {
        fprintf(stderr, "%s: name\n", argv[0]);
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
