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
#include <ctype.h> /*isprint*/

#include "periph/flashpage.h"
#include "gnrc_xipfs.h"
#include "exec_common.h"

#define NAKED __attribute__((naked))

extern void *unusedRamStart;

void *sp_backup, *stktop, *ep;
crt0_ctx_t *crt0_ctx;

static void NAKED __attribute__((noinline))
_exit(int status)
{
    (void)status;
    __asm__ volatile
    (
        "ldr r4, .L1\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "mov sp, r4\n"

        "pop {r0-r8}\n"

        "pop {r4, pc}\n"

        ".align 2\n"
        ".L1:\n"
        ".word sp_backup(GOT)\n"
    );
}

static void *xipfs_syscall_table[XIPFS_SYSCALL_MAX] = {
    [XIPFS_SYSCALL_EXIT         ] = _exit,
    [XIPFS_SYSCALL_VPRINTF      ] = vprintf,
    [XIPFS_SYSCALL_GET_TEMP     ] = get_temp,
    [XIPFS_SYSCALL_ISPRINT      ] = isprint,
    [XIPFS_SYSCALL_STRTOL       ] = strtol,
    [XIPFS_SYSCALL_GET_LED      ] = get_led,
    [XIPFS_SYSCALL_SET_LED      ] = set_led,
    [XIPFS_SYSCALL_COPY_FILE    ] = copy_file,
    [XIPFS_SYSCALL_GET_FILE_SIZE] = get_file_size,
    [XIPFS_SYSCALL_MEMSET       ] = memset
};

static void NAKED __attribute__((noinline))
start_exec(void)
{
    __asm__ volatile
    (
        /* Save r4 and the return address */
        "push {r4, lr}\n"

        "push {r0-r8}\n"

        /* Save current stack pointer in sp_backup */
        "ldr r4, .L2\n"
        "ldr r4, [r10, r4]\n"
        "str sp, [r4]\n"

        /* Set the new stack pointer to stktop */
        "ldr r4, .L2+4\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "mov sp, r4\n"

        /* Set crt0_ctx as first parameter */
        "ldr r0, .L2+8\n"
        "ldr r0, [r10, r0]\n"
        "ldr r0, [r0]\n"

        /* Call the crt0 start thanks to the entrypoint */
        "ldr r4, .L2+12\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "blx r4\n"

        ".align 2\n"
        ".L2:\n"
        ".word sp_backup(GOT)\n"
        ".word stktop(GOT)\n"
        ".word crt0_ctx(GOT)\n"
        ".word ep(GOT)\n"
        :::"r0", "r1", "r4"
    );
}

int _exec_callback(int argc, char **argv)
{
    xipfs_crt0_ctx_data_t *xipfs_crt0_ctx_data;
    void *freeram;
    size_t neededram;
    file_t *file;
    size_t i;
    int  ret = 0;


    if (argc < 2) {
        fprintf(stderr, "%s name\n", argv[0]);
        return 1;
    }

    if ((file = tinyfs_file_search(argv[1])) == NULL) {
        fprintf(stderr, "%s: %s: no such file\n", argv[0], argv[1]);
        return 1;
    }

    if (file->status != TINYFS_STATUS_LOADED) {
        fprintf(stderr, "%s: %s: the file is not loaded\n", argv[0],
            argv[1]);
        return 1;
    }

    if (file->exec == 0) {
        fprintf(stderr, "%s: %s: permission denied\n", argv[0],
            argv[1]);
        return 1;
    }

    ep = (void *)THUMB_ADDRESS((uintptr_t)file + sizeof(file_t));

    /* XXX fix read needed RAM size instead of '+ (3*4096)' */
    neededram = DEFAULT_STACK_SIZE + (3*4096);

    freeram             = (void *)((char *)unusedRamStart + DEFAULT_STACK_SIZE);
    crt0_ctx            = (void *)((char *)freeram - sizeof(crt0_ctx_t));
    xipfs_crt0_ctx_data = (void *)((char *)crt0_ctx - sizeof(xipfs_crt0_ctx_data_t));
    stktop              = (void *)xipfs_crt0_ctx_data;

    //printf("unusedRamStart %p, unusedRamStart + default stacksize = %p\n", unusedRamStart, freeram);

    xipfs_crt0_ctx_data->is_safe_call = 0;

    crt0_ctx->bin_base             = (void *)((uintptr_t)file + sizeof(file_t));
    xipfs_crt0_ctx_data->file_base = (void *)file;

    crt0_ctx->ram_start = freeram;
    crt0_ctx->ram_end   = unusedRamStart + neededram;

    crt0_ctx->nvm_start = (void *)((uintptr_t)file + file->size);
    crt0_ctx->nvm_end   = (void *)ROUND((uintptr_t)file + file->size, FLASHPAGE_SIZE);

    xipfs_crt0_ctx_data->argc = argc - 1;
    for (i = 1; i < (size_t)argc; i++) {
        xipfs_crt0_ctx_data->argv[i-1] = argv[i];
    }

    xipfs_crt0_ctx_data->syscall_table = xipfs_syscall_table;

    __asm__ volatile (
        "mov %0, sl"
        : "=r"(xipfs_crt0_ctx_data->former_got)
    );

    crt0_ctx->argv = xipfs_crt0_ctx_data;

    /* push */
    unusedRamStart += neededram;

    start_exec();

    /* pop */
    unusedRamStart -= neededram;

    /* clean memory */
    (void)memset(unusedRamStart, 0, neededram);

    return ret;
}
