/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */
#include <stdio.h>

#include "gnrc_xipfs.h"

#define COLUMN_1_LEN "10"
#define COLUMN_2_LEN "3"
#define COLUMN_3_LEN "10"
#define COLUMN_4_LEN "10"
#define COLUMN_5_LEN "32"

static void
print_file_infos(file_t *file)
{
    switch (file->status) {
    case TINYFS_STATUS_LOADED:
        printf("%-" COLUMN_1_LEN "s", "loaded");
        break;
    case TINYFS_STATUS_LOADING:
        printf("%-" COLUMN_1_LEN "s", "loading");
        break;
    case TINYFS_STATUS_CREATED:
        printf("%-" COLUMN_1_LEN "s", "created");
        break;
    default:
        /* corrupted filesystem */
        printf("%-" COLUMN_1_LEN "s", "???");
    }

    printf(" ");

    switch (file->exec) {
    case 0:
        printf("%-" COLUMN_2_LEN "s", "-");
        break;
    case 1:
        printf("%-" COLUMN_2_LEN "s", "x");
        break;
    default:
        /* corrupted filesystem */
        printf("%-" COLUMN_2_LEN "s", "???");
    }

    printf(" ");

    printf("0x%-" COLUMN_3_LEN "lx", (uint32_t)(file + 1));

    printf(" ");

    printf("%-" COLUMN_4_LEN "ld", file->size);

    printf(" ");

    printf("%-" COLUMN_5_LEN "s", file->name);

    printf("\n");
}

int _ls_callback(int argc, char **argv)
{
    file_t *file;

    (void)argc;
    (void)argv;

    if ((file = tinyfs_get_first_file()) == NULL) {
        return 0;
    }

    print_file_infos(file);

    while ((file = tinyfs_get_next_file(file)) != NULL) {
        print_file_infos(file);
    }

    return 0;
}
