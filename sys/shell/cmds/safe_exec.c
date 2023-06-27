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

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "gnrc_xipfs.h"
#include "interface.h"
#include "saul.h"
#include "saul_reg.h"
#include "shell.h"
#include "svc.h"

/**
 * @brief   Initial program status register value for a partition
 *
 * In the initial state, only the Thumb mode-bit is set
 */
#define INITIAL_XPSR (0x01000000)

#define MAP_DISCARD    (-1)
#define PREPARE_FORCE  ( 8)

#define RIOT_BLOCK_ID_1 ((void *)0x2000f1ad)
#define RIOT_BLOCK_ID_2 ((void *)0x2000f1be)

#define RIOT_VIDT_MEMFAULT ( 4)
#define RIOT_VIDT_SYSCALL  (54)
#define RIOT_VIDT_DISCARD  (55)

#define RIOT_SYSCALL_EXIT     (0)
#define RIOT_SYSCALL_VPRINTF  (1)
#define RIOT_SYSCALL_GET_TEMP (2)
#define RIOT_SYSCALL_ISPRINT  (3)
#define RIOT_SYSCALL_STRTOL   (4)
#define RIOT_SYSCALL_GET_LED  (5)
#define RIOT_SYSCALL_SET_LED  (6)
#define RIOT_SYSCALL_COPY_FILE (7)
#define RIOT_SYSCALL_GET_FILE_SIZE (8)
#define RIOT_SYSCALL_MEMSET (9)

#define ROUND(x, y) \
    (((x) + (y) - 1) & ~((y) - 1))

#define THUMB_ADDRESS(x) ((x) | 1)

extern int isprint(int character);

extern void *riotPartDesc;
extern vidt_t *riotVidt;
extern void *riotGotAddr;
extern void *unusedRamStart;

static basicContext_t riot_dsp_ctx;
static basicContext_t *child_ctx_addr;
static char riot_stk_addr[512];
static void *child_block_0_id;
static void *child_block_1_id;
static void *child_block_2_id;
static void *child_pd_id;
static void *child_flash_end_id;
static int riot_status;

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

static void
_exit(int status)
{
    riot_status = status;

    if (!Pip_mapMPU(riotPartDesc, NULL, 3)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, NULL, 4)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, NULL, 5)) {
        assert(0);
    }

    Pip_yield(riotPartDesc, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

static void
memfault_handler(void)
{
    printf("Memory access violation\n");

    Pip_yield(riotPartDesc, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

static void
syscall_handler(void)
{
    uint32_t *argv;
    va_list ap;

    if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 3)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, child_block_1_id, 4)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, child_block_2_id, 5)) {
        assert(0);
    }

#if 0
    if (!Pip_mapMPU(riotPartDesc, child_flash_end_id, 6)) {
        assert(0);
    }
#endif

    argv = (uint32_t *)child_ctx_addr->frame.sp;

    switch (argv[0]) {
    case RIOT_SYSCALL_EXIT:
        _exit((int)argv[1]);
        break;
    case RIOT_SYSCALL_VPRINTF:
        __asm__ volatile
        (
            "mov %0, %1\n"
            : "=r" (ap)
            : "r" (argv[2])
            :
        );
        argv[0] = vprintf((const char *)argv[1], ap);
        break;
    case RIOT_SYSCALL_GET_TEMP:
        argv[0] = get_temp();
        break;
    case RIOT_SYSCALL_ISPRINT:
        argv[0] = isprint((int)argv[1]);
        break;
    case RIOT_SYSCALL_STRTOL:
        argv[0] = strtol((const char *)argv[1], (char **)argv[2], (int)argv[3]);
        break;
    case RIOT_SYSCALL_GET_LED:
        argv[0] = get_led((int)argv[1]);
        break;
    case RIOT_SYSCALL_SET_LED:
        argv[0] = set_led((int)argv[1], (int)argv[2]);
        break;
    case RIOT_SYSCALL_COPY_FILE:
    {
        const char *name = (const char *)argv[1];
        void *buf = (void *)argv[2];
        size_t nbyte = (size_t)argv[3];
        uint32_t res;
        if (!Pip_mapMPU(riotPartDesc, child_flash_end_id, 3)) {
            assert(0);
        }
        res = copy_file(name, buf, nbyte);
        if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 3)) {
            assert(0);
        }
        argv[0] = res;
        break;
    }
    case RIOT_SYSCALL_GET_FILE_SIZE:
    {
        const char *name = (const char *)argv[1];
        size_t size = 0;
        uint32_t res;
        if (!Pip_mapMPU(riotPartDesc, child_flash_end_id, 3)) {
            assert(0);
        }
        res = get_file_size(name, &size);
        if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 3)) {
            assert(0);
        }
        *((size_t *)argv[2]) = size;
        argv[0] = res;
        break;
    }
    case RIOT_SYSCALL_MEMSET:
        argv[0] = (uint32_t)memset((void *)argv[1], (int)argv[2], (size_t)argv[3]);
        break;
    }

    if (!Pip_mapMPU(riotPartDesc, NULL, 3)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, NULL, 4)) {
        assert(0);
    }

    if (!Pip_mapMPU(riotPartDesc, NULL, 5)) {
        assert(0);
    }

#if 0
    if (!Pip_mapMPU(riotPartDesc, NULL, 6)) {
        assert(0);
    }
#endif
}

static void
dispatcher(void)
{
    switch (riotVidt->currentInterrupt) {
    case RIOT_VIDT_MEMFAULT:
        memfault_handler();
        break;
    case RIOT_VIDT_SYSCALL:
        syscall_handler();
        break;
    }

    Pip_yield(child_pd_id, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

static int
_main(int argc, char **argv)
{
    void *riot_krn_addr;
    void *riot_krn_id;
    void *child_pd_addr, *child_krn_addr, *child_stk_addr,
    *child_sys_addr, *child_ram_addr, *child_end_addr,
    *child_flash_addr, *child_flash_end_addr;
    interface_t *child_itf_addr;
    vidt_t *child_vidt_addr;
    void *child_krn_id, *child_end_id;
    void *child_block_0_idc, *child_block_1_idc,
    *child_block_2_idc;
    void *memfault_ctx;
    file_t *file;
    void *child_args_addr, *child_argc_addr, *child_argv_addr;
    size_t i, j, k;

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

    /* Disable interrupts */
    Pip_setIntState(0);

    /* Initialize status */
    riot_status = 0;

    /* RIOT's kernel structure needed to create the child */
    riot_krn_addr = (void *)ROUND((uintptr_t)unusedRamStart, 512);
    /* Child's partition descriptor block */
    child_pd_addr = (char *)riot_krn_addr + 512;
    /* Child's kernel structure */
    child_krn_addr = (char *)child_pd_addr + 512;
    /* MPU BLOCK 0 - aligned: 1024, size: 1024 */
    child_stk_addr  = (void *)ROUND((uintptr_t)child_krn_addr + 512, 1024);
    child_vidt_addr = (vidt_t *)((uintptr_t)child_stk_addr + 512);
    /* MPU BLOCK 1 - aligned: 8192, size: 8192 */
    child_itf_addr = (void *)ROUND((uintptr_t)child_vidt_addr + 512, 8192);
    child_sys_addr = (char *)child_itf_addr + sizeof(*child_itf_addr);
    child_ctx_addr = (void *)((uintptr_t)child_sys_addr + 11 * sizeof(uint32_t));
    child_args_addr = (char *)child_ctx_addr + sizeof(*child_ctx_addr);
    child_ram_addr = (char *)child_args_addr + SHELL_DEFAULT_BUFSIZE;
    child_end_addr = (char *)child_itf_addr + 8192;
    /* MPU BLOCK 2 - aligned: 4096, size: 4096 */
    child_flash_addr = (void *)file;
    child_flash_end_addr = (void *)ROUND((uintptr_t)file + file->size, FLASHPAGE_SIZE);

    /* Fill in the child's syscall table */
    ((void **)child_sys_addr)[0] = (void *)1; /* use Pip */
    ((void **)child_sys_addr)[1] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[2] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[3] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[4] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[5] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[6] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[7] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[8] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[9] = (void *)RIOT_VIDT_SYSCALL;
    ((void **)child_sys_addr)[10] = (void *)RIOT_VIDT_SYSCALL;

    /* Pepare arguments */
    child_argv_addr = (char *)child_stk_addr + 512 - (SHELL_DEFAULT_BUFSIZE / 2);
    child_argc_addr = (char *)child_argv_addr - 4;

    for (i = 1, j = 0; i < (size_t)argc; i++) {
        ((char **)child_argv_addr)[i-1] = &((char *)child_args_addr)[j];
        for (k = 0; argv[i][k] != '\0'; k++, j++) {
            ((char *)child_args_addr)[j] = argv[i][k];
        }
        ((char *)child_args_addr)[j++] = '\0';
    }
    *(uint32_t *)child_argc_addr = argc - 1;

    /* Fill in the child's iterface */
    /* the partDescBlockId will be fill in later */
    child_itf_addr->stackLimit = child_stk_addr;
    child_itf_addr->stackTop = child_argc_addr;
    child_itf_addr->vidtStart = child_vidt_addr;
    child_itf_addr->vidtEnd = (char *)child_vidt_addr + 512;
    child_itf_addr->root = (char *)file + sizeof(*file);
    child_itf_addr->unusedRomStart = (char *)file + sizeof(*file) + file->size;
    child_itf_addr->romEnd = (void *)ROUND((uintptr_t)child_itf_addr->unusedRomStart, FLASHPAGE_SIZE);
    child_itf_addr->unusedRamStart = child_ram_addr;
    child_itf_addr->ramEnd = child_end_addr;

    /* Fill in the child's context */
    (void)memset(child_ctx_addr, 0, sizeof(*child_ctx_addr));
    child_ctx_addr->isBasicFrame = 1;
    child_ctx_addr->pipflags = 1;
    child_ctx_addr->frame.r0 = (uint32_t)child_itf_addr;
    child_ctx_addr->frame.r1 = (uint32_t)child_sys_addr;
    child_ctx_addr->frame.sp = (uint32_t)child_argc_addr;
    child_ctx_addr->frame.pc = THUMB_ADDRESS((uint32_t)file + sizeof(*file));
    child_ctx_addr->frame.xpsr = INITIAL_XPSR;

    /* Fill in RIOT's context */
    (void)memset(&riot_dsp_ctx, 0, sizeof(riot_dsp_ctx));
    riot_dsp_ctx.isBasicFrame = 1;
    riot_dsp_ctx.pipflags = 0;
    riot_dsp_ctx.frame.r10 = (uint32_t)riotGotAddr;
    riot_dsp_ctx.frame.sp = (uint32_t)riot_stk_addr + 512;
    riot_dsp_ctx.frame.pc = (uint32_t)dispatcher;
    riot_dsp_ctx.frame.xpsr = INITIAL_XPSR;

    /* Prepare Child's VIDT */
    (void)memset(child_vidt_addr, 0, 512);
    ((vidt_t *)child_vidt_addr)->contexts[0] = child_ctx_addr;

    /* Backup original memfault context */
    memfault_ctx = riotVidt->contexts[RIOT_VIDT_MEMFAULT];

    /* Prepare RIOT's VIDT */
    riotVidt->contexts[RIOT_VIDT_MEMFAULT] = &riot_dsp_ctx;
    riotVidt->contexts[RIOT_VIDT_SYSCALL] = &riot_dsp_ctx;
    riotVidt->contexts[RIOT_VIDT_DISCARD] = NULL;

#if 0
    if (!Pip_findBlock(riotPartDesc, riot_krn_addr, &block_1)) {
        goto abort;
    }

    if ((riot_krn_id = Pip_cutMemoryBlock(block_1.blockAttr.blockentryaddr,
        riot_krn_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }
#else
    if ((riot_krn_id = Pip_cutMemoryBlock(RIOT_BLOCK_ID_1,
        riot_krn_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }
#endif

    if ((child_pd_id = Pip_cutMemoryBlock(riot_krn_id,
        child_pd_addr, 3)) == NULL) {
        goto abort;
    }

    child_itf_addr->partDescBlockId = child_pd_id;

    if (!Pip_mapMPU(riotPartDesc, NULL, 3)) {
        goto abort;
    }

    if (!Pip_prepare(riotPartDesc, PREPARE_FORCE, riot_krn_id)) {
        goto abort;
    }

    if ((child_krn_id = Pip_cutMemoryBlock(child_pd_id,
        child_krn_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_block_0_id = Pip_cutMemoryBlock(child_krn_id,
        child_stk_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_block_1_id = Pip_cutMemoryBlock(child_block_0_id,
        (void *)child_itf_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_end_id = Pip_cutMemoryBlock(child_block_1_id,
        child_end_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

#if 0
    if (!Pip_findBlock(riotPartDesc, child_flash_addr, &block_2)) {
        goto abort;
    }

    if ((child_block_2_id = Pip_cutMemoryBlock(block_2.blockAttr.blockentryaddr,
        child_flash_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }
#else
    if ((child_block_2_id = Pip_cutMemoryBlock(RIOT_BLOCK_ID_2,
        child_flash_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }
#endif

    if ((child_flash_end_id = Pip_cutMemoryBlock(child_block_2_id,
        child_flash_end_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if (!Pip_createPartition(child_pd_id)) {
        goto abort;
    }

    if (!Pip_prepare(child_pd_id, PREPARE_FORCE, child_krn_id)) {
        goto abort;
    }

    if ((child_block_0_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_0_id, 1, 1, 0)) == NULL) {
        goto abort;
    }

    if ((child_block_1_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_1_id, 1, 1, 0)) == NULL) {
        goto abort;
    }

    if ((child_block_2_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_2_id, 1, 0, 1)) == NULL) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, child_block_0_idc, 0)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, child_block_1_idc, 1)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, child_block_2_idc, 2)) {
        goto abort;
    }

    if (!Pip_setVIDT(child_pd_id, (void *)child_vidt_addr)) {
        goto abort;
    }

    Pip_yield(child_pd_id, 0, 0, 1, 1);

    if (!Pip_setVIDT(child_pd_id, NULL)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, NULL, 2)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, NULL, 1)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, NULL, 0)) {
        goto abort;
    }

    if (!Pip_removeMemoryBlock(child_block_2_id)) {
        goto abort;
    }

    if (!Pip_removeMemoryBlock(child_block_1_id)) {
        goto abort;
    }

    if (!Pip_removeMemoryBlock(child_block_0_id)) {
        goto abort;
    }

    if (Pip_collect(child_pd_id) == NULL) {
        goto abort;
    }

    if (!Pip_deletePartition(child_pd_id)) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_block_2_id, child_flash_end_id,
        MAP_DISCARD) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(RIOT_BLOCK_ID_2, child_block_2_id, 2)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_block_1_id, child_end_id,
        MAP_DISCARD) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_block_0_id, child_block_1_id,
        MAP_DISCARD) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_krn_id, child_block_0_id, MAP_DISCARD)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_pd_id, child_krn_id, MAP_DISCARD)
        == NULL) {
        goto abort;
    }

    if (Pip_collect(riotPartDesc) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(riot_krn_id, child_pd_id, MAP_DISCARD)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(RIOT_BLOCK_ID_1, riot_krn_id, 1)
        == NULL) {
        goto abort;
    }

    /* Restore RIOT's VIDT */
    riotVidt->contexts[RIOT_VIDT_MEMFAULT] = memfault_ctx;
    riotVidt->contexts[RIOT_VIDT_SYSCALL] = NULL;
    riotVidt->contexts[RIOT_VIDT_DISCARD] = NULL;

    /* Enable interrupts */
    Pip_setIntState(1);

    /* Clean RAM */
    (void)memset(unusedRamStart, 0, 10752);

    return riot_status;

abort:
    /* What should we do? */
    for (;;);
}

SHELL_COMMAND(safe_exec, "run a binary safely in the foreground", _main);
