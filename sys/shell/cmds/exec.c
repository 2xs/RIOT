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
 * @brief       Shell command to run a binary in the foreground
 *
 * @}
 */

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "gnrc_xipfs.h"
#include "interface.h"
#include "saul.h"
#include "saul_reg.h"
#include "shell.h"

#define NAKED __attribute__((naked))

#define DEFAULT_STACK_SIZE 1024

#define ROUND(x, y) \
    (((x) + (y) - 1) & ~((y) - 1))

#define THUMB_ADDRESS(x) ((x) | 1)

typedef int (*entryPoint_t)(interface_t *interface, void **syscalls);

extern int isprint(int character);

extern void *riotPartDesc;
extern void *unusedRamStart;

void *sp, *stktop, *ep, **syscalls;
interface_t *itf;

static void
usage(char **argv)
{
    fprintf(stderr, "%s name\n", argv[0]);
}

static int
get_temp(void)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((dev = saul_reg_find_nth(5)) == NULL) {
        return 0;
    }

    if ((dim = saul_reg_read(dev, &res)) <= 0) {
        return 0;
    }

    return res.val[0];
}

static int
get_led(int pos)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((unsigned int)pos > 3) {
        return -1;
    }

    if ((dev = saul_reg_find_nth(pos)) == NULL) {
        return -1;
    }

    if ((dim = saul_reg_read(dev, &res)) <= 0) {
        return -1;
    }

    return res.val[0];
}

static int
set_led(int pos, int val)
{
    saul_reg_t *dev;
    phydat_t res;
    int dim;

    if ((unsigned int)pos > 3) {
        return -1;
    }

    if ((unsigned int)val > 1) {
        return -1;
    }

    if ((dev = saul_reg_find_nth(pos)) == NULL) {
        return -1;
    }

    res.val[0] = val;

    if ((dim = saul_reg_write(dev, &res)) <= 0) {
        return -1;
    }

    return 0;
}

static ssize_t
copy_file(const char *name, void *buf, size_t nbyte)
{
    file_t *file;
    size_t i;

    if ((file = tinyfs_file_search(name)) == NULL) {
        return -1;
    }

    for (i = 0; i < nbyte && i < file->size; i++) {
        ((char *)buf)[i] = ((char *)file + sizeof(*file))[i];
    }

    return i;
}

static int
get_file_size(const char *name, size_t *size)
{
    file_t *file;

    if ((file = tinyfs_file_search(name)) == NULL) {
        return -1;
    }

    *size = file->size;
    return 0;
}

static void NAKED
_exit(int status)
{
    (void)status;
    __asm__ volatile
    (
        "ldr r4, .L1\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "mov sp, r4\n"

        "pop {r4, pc}\n"

        ".align 2\n"
        ".L1:\n"
        ".word sp(GOT)\n"
    );
}

static void NAKED
start(int status)
{
    (void)status;
    __asm__ volatile
    (
        "push {r4, lr}\n"

        "ldr r4, .L2\n"
        "ldr r4, [r10, r4]\n"
        "str sp, [r4]\n"

        "ldr r4, .L2+4\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "mov sp, r4\n"

        "ldr r0, .L2+8\n"
        "ldr r0, [r10, r0]\n"
        "ldr r0, [r0]\n"

        "ldr r1, .L2+12\n"
        "ldr r1, [r10, r1]\n"
        "ldr r1, [r1]\n"

        "ldr r4, .L2+16\n"
        "ldr r4, [r10, r4]\n"
        "ldr r4, [r4]\n"
        "blx r4\n"

        /* TODO POP in case of crt0 return */

        ".align 2\n"
        ".L2:\n"
        ".word sp(GOT)\n"
        ".word stktop(GOT)\n"
        ".word itf(GOT)\n"
        ".word syscalls(GOT)\n"
        ".word ep(GOT)\n"
    );
}

static int
_main(int argc, char **argv)
{
    void *child_argc, *child_argv;
    void *stkbot, *freeram;
    size_t neededram;
    file_t *file;
    size_t i;
    int ret;

    if (argc < 2) {
        usage(argv);
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

    /* XXX fix read needed RAM size instead of '+ (2*4096)' */
    neededram = ROUND(DEFAULT_STACK_SIZE + (2*4096), FLASHPAGE_SIZE);

    stkbot   = unusedRamStart;
    freeram  = (void *)((char *)unusedRamStart + DEFAULT_STACK_SIZE);
    itf      = (void *)((char *)freeram - sizeof(interface_t));
    syscalls = (void *)((char *)itf - 11 * sizeof(uint32_t));
    child_argv = (uint32_t *)syscalls - argc - 1;
    child_argc = (uint32_t *)child_argv - 1;
    stktop   = (void *)child_argc;

    for (i = 1; i < (size_t)argc; i++) {
        ((char **)child_argv)[i-1] = argv[i];
    }
    *(uint32_t *)child_argc = argc - 1;

    ((void **)syscalls)[0] = (void *)0; /* pip */
    ((void **)syscalls)[1] = _exit;
    ((void **)syscalls)[2] = vprintf;
    ((void **)syscalls)[3] = get_temp;
    ((void **)syscalls)[4] = isprint;
    ((void **)syscalls)[5] = strtol;
    ((void **)syscalls)[6] = get_led;
    ((void **)syscalls)[7] = set_led;
    ((void **)syscalls)[8] = copy_file;
    ((void **)syscalls)[9] = get_file_size;
    ((void **)syscalls)[10] = memset;

    itf->partDescBlockId = riotPartDesc;
    itf->stackLimit      = stkbot;
    itf->stackTop        = stktop;
    itf->vidtStart       = (void *)0;
    itf->vidtEnd         = (void *)0;
    itf->root            = (void *)((uintptr_t)file + sizeof(file_t));
    itf->unusedRomStart  = (void *)((uintptr_t)file + file->size);
    itf->romEnd          = (void *)ROUND((uintptr_t)file + file->size, FLASHPAGE_SIZE);
    itf->unusedRamStart  = freeram;
    itf->ramEnd          = (void *)((uintptr_t)unusedRamStart + neededram);

    /* push */
    unusedRamStart += neededram;

    start(0);

    /* pop */
    unusedRamStart -= neededram;

    /* clean memory */
    (void)memset(unusedRamStart, 0, neededram);

    return ret;
}

SHELL_COMMAND(exec, "run a binary in the foreground", _main);
