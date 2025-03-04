/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <ctype.h>

#include "gnrc_xipfs.h"

#define FST_COL_END 8
#define SND_COL_END 16

static void
dump(void *addr, size_t size)
{
    volatile unsigned char *ptr, *eaddr;
    unsigned char byte;
    unsigned int i, j;

    ptr = addr;
    eaddr = (void *)(uintptr_t)ptr + size;
    while (ptr < eaddr) {
        printf("%" PRIxPTR "  ", (uintptr_t)ptr);

        i = 0;
        while (i < FST_COL_END && ptr + i < eaddr)
            printf("%02x ", ptr[i++]);

        printf("  ");

        while (i < SND_COL_END && ptr + i < eaddr)
            printf("%02x ", ptr[i++]);

        j = i;

        while (i < SND_COL_END) {
            printf("   ");
            i++;
        }

        printf(" |");

        i = 0;
        while (i < j) {
            byte = ptr[i];
            if (isprint(byte) != 0)
                printf("%c", byte);
            else
                printf(".");
            i++;
        }

        printf("|\n");

        ptr += i;
    }

    printf("%" PRIxPTR "\n", (uintptr_t)ptr);
}

int _hexdump_callback(int argc, char **argv)
{
    file_t *file;

    if (argc < 2) {
        fprintf(stderr, "%s: name\n", argv[0]);
        return 1;
    }

    if ((file = tinyfs_file_search(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: no such file\n", argv[0], argv[1]);
        return 1;
    }

    dump((char *)file + sizeof(*file), file->size);

    return 0;
}
