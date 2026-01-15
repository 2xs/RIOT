/*
 * Copyright (C) 2023
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "gnrc_xipfs.h"
#include "saul.h"
#include "saul_reg.h"
#include "shell.h"
#include "svc.h"
#include "exec_common.h"

/**
 * @brief   Initial program status register value for a partition
 *
 * In the initial state, only the Thumb mode-bit is set
 */
#define INITIAL_XPSR (0x01000000)

#define MAP_DISCARD    (-1)
#define PREPARE_FORCE  ( 8)

#define RIOT_BLOCK_ID_1     ((void *)0x2000f1ad)
#define RIOT_BLOCK_START_1  (0x20000000 + 0x8000)
#define RIOT_BLOCK_SIZE_1   (0x4000)
#define RIOT_BLOCK_END_1    (RIOT_BLOCK_START_1 + RIOT_BLOCK_SIZE_1)

#define RIOT_BLOCK_ID_2     ((void *)0x2000f1be)
#define RIOT_BLOCK_START_2  (RIOT_BLOCK_END_1)
#define RIOT_BLOCK_SIZE_2   (0x2000)
#define RIOT_BLOCK_END_2    (RIOT_BLOCK_START_2 + RIOT_BLOCK_SIZE_2)

#define RIOT_BLOCK_ID_3     ((void *)0x2000f1cf)
#define RIOT_BLOCK_START_3  (RIOT_BLOCK_END_2)
#define RIOT_BLOCK_SIZE_3   (0x1000)
#define RIOT_BLOCK_END_3    (RIOT_BLOCK_START_3 + RIOT_BLOCK_SIZE_3)


#define RIOT_BLOCK_ID_4 ((void *)0x2000f1e0)

#define RIOT_VIDT_MEMFAULT ( 4)
#define RIOT_VIDT_SYSCALL  (54)
#define RIOT_VIDT_DISCARD  (55)

#define ROUND(x, y) \
    (((x) + (y) - 1) & ~((y) - 1))

#define THUMB_ADDRESS(x) ((x) | 1)

extern int isprint(int character);

extern void *riotPartDesc;
extern vidt_t *riotVidt;
extern void *riotGotAddr;
extern void *unusedRamStart;

extern uint32_t _start_shared_api_code;
extern uint32_t _end_shared_api_code;
extern uint32_t _start_shared_api_data;
extern uint32_t _end_shared_api_data;

static basicContext_t riot_dsp_ctx, riot_save_ctx;
static basicContext_t *child_ctx_addr;
static crt0_ctx_t *child_crt0_ctx;
static xipfs_crt0_ctx_data_t *xipfs_crt0_ctx_data;
static char riot_stk_addr[512];
static void *child_block_0_id;
//static void *child_block_1_id;
static void *child_block_2_id;
static void *child_block_3_id;
static void *child_pd_id;
static void *child_flash_end_id;
static int riot_status;

static void
_exit(int status)
{
    riot_status = status;

    if (!Pip_mapMPU(riotPartDesc, NULL, 5)) {
        assert(0);
    }

    Pip_yield(riotPartDesc, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

/**
 * @internal
 *
 * @def STR_HELPER
 *
 * @brief Used for preprocessing in asm statements
 */
#define STR_HELPER(x) #x

/**
 * @internal
 *
 * @def STR
 *
 * @brief Used for preprocessing in asm statements
 */
#define STR(x) STR_HELPER(x)

#define SVC_NUMBER 54

__attribute__((section(".shared_api_code")))
static void exit_wrapper(int status)
{

    /*
     * Pip performs a SVC by first yielding (svc #12) with the targetInterruptLevel
     * set to PIP_SVC_NUMBER. Then it yields to the targetInterruptLevel with
     * args coming from stack.
     */
    __asm__ volatile
    (
        "mov r0, %0                  \n"
        "mov r1, %1                  \n"
        "push {r0, r1}               \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        /* UNREACHABLE */
        :
        : "r"(XIPFS_SYSCALL_EXIT), "r" (status)
    );
}

__attribute__((section(".shared_api_code")))
static int vprintf_wrapper(const char *format, va_list va)
{
    int ret;
    __asm__ volatile
    (
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "mov r2, %3                  \n"
        "push {r0, r1, r2}           \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #12             \n"
        : "=r"(ret)
        : "r" (XIPFS_SYSCALL_VPRINTF),
          "r" (format),
          "r" (va)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}


__attribute__((section(".shared_api_code")))
static int get_temp_wrapper(void)
{
    int ret;
    __asm__ volatile
    (
        "mov r0, %1                  \n"
        "push {r0}                   \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #4              \n"
        : "=r"(ret)
        : "r"(XIPFS_SYSCALL_GET_TEMP)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static int isprint_wrapper(int character)
{
    int ret;
    __asm__ volatile
    (
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "push {r0, r1}               \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #8              \n"
        : "=r"(ret)
        : "r"(XIPFS_SYSCALL_ISPRINT), "r" (character)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static long strtol_wrapper(const char *str, char **endptr, int base)
{
    int ret;

    __asm__ volatile
    (
        "mov r0, %2                  \n" // XIPFS_SYSCALL_STRTOL
        "mov r1, %3                  \n" // str
        "mov r2, %1                  \n" // endptr
        "mov r3, %4                  \n" // base
        /*
         * We substract 4 before the SVC so that the stack is aligned on a 8-bytes boundary manually.
         * The SVC won't trigger the CPU to align the stack silently.
         */
        "sub sp, sp, #4              \n"
        "push {r0-r3}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #20             \n"
        : "=r"(ret), "+r" (endptr)
        : "r" (XIPFS_SYSCALL_STRTOL),
          "r" (str),
          "r" (base)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return (long)ret;

}

__attribute__((section(".shared_api_code")))
static int get_led_wrapper(int pos)
{
    int ret;
    __asm__ volatile(
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "push {r0-r1}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #8              \n"
        : "=r"(ret)
        : "r" (XIPFS_SYSCALL_GET_LED), "r"(pos)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static int set_led_wrapper(int pos, int val)
{
    int ret;
    __asm__ volatile (
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "mov r2, %3                  \n"
        "push {r0-r2}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #12             \n"
        : "=r"(ret)
        : "r"(XIPFS_SYSCALL_SET_LED), "r"(pos), "r"(val)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static ssize_t copy_file_wrapper(const char *name, void *buf, size_t nbyte)
{
    ssize_t ret;
    __asm__ volatile (
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "mov r2, %3                  \n"
        "mov r3, %4                  \n"
        /*
         * We substract 4 before the SVC so that the stack is aligned on a 8-bytes boundary manually.
         * The SVC won't trigger the CPU to align the stack silently.
         */
        "sub sp, sp, #4              \n"
        "push {r0-r3}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #20             \n"
        : "=r"(ret)
        : "r"(XIPFS_SYSCALL_COPY_FILE), "r"(name), "r"(buf), "r"(nbyte)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static int get_file_size_wrapper(const char *name, size_t *size)
{
    int ret;
    __asm__ volatile (
        "mov r0, %2                  \n"
        "mov r1, %3                  \n"
        "mov r2, %1                  \n"
        "push {r0-r2}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #12             \n"
        : "=r"(ret), "+r"(size)
        : "r"(XIPFS_SYSCALL_GET_FILE_SIZE), "r"(name)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return ret;
}

__attribute__((section(".shared_api_code")))
static void *memset_wrapper(void *m, int c, size_t n)
{
    int ret;
    __asm__ volatile (
        "mov r0, %1                  \n"
        "mov r1, %2                  \n"
        "mov r2, %3                  \n"
        "mov r3, %4                  \n"
        /*
         * We substract 4 before the SVC so that the stack is aligned on a 8-bytes boundary manually.
         * The SVC won't trigger the CPU to align the stack silently.
         */
        "sub sp, sp, #4              \n"
        "push {r0-r3}                \n"
        "mov r0, #0                  \n"
        "mov r1, #" STR(SVC_NUMBER) "\n"
        "mov r2, #0                  \n"
        "mov r3, #1                  \n"
        "mov r4, #1                  \n"
        "svc #12                     \n"
        "pop {%0}                    \n"
        "add sp, sp, #20             \n"
        : "=r"(ret)
        : "r"(XIPFS_SYSCALL_MEMSET), "r"(m), "r"(c), "r"(n)
        : "r0", "r1", "r2", "r3", "r4"
    );

    return (void *)ret;
}

__attribute__((section(".shared_api_data")))
static void *xipfs_syscall_table[XIPFS_SYSCALL_MAX] = {
    [XIPFS_SYSCALL_EXIT         ] = exit_wrapper,
    [XIPFS_SYSCALL_VPRINTF      ] = vprintf_wrapper,
    [XIPFS_SYSCALL_GET_TEMP     ] = get_temp_wrapper,
    [XIPFS_SYSCALL_ISPRINT      ] = isprint_wrapper,
    [XIPFS_SYSCALL_STRTOL       ] = strtol_wrapper,
    [XIPFS_SYSCALL_GET_LED      ] = get_led_wrapper,
    [XIPFS_SYSCALL_SET_LED      ] = set_led_wrapper,
    [XIPFS_SYSCALL_COPY_FILE    ] = copy_file_wrapper,
    [XIPFS_SYSCALL_GET_FILE_SIZE] = get_file_size_wrapper,
    [XIPFS_SYSCALL_MEMSET       ] = memset_wrapper
};

static __attribute__((noinline)) void memfault_handler(void)
{
    printf("Memory access violation\n");

    Pip_yield(riotPartDesc, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

static __attribute__((noinline)) void syscall_handler(void)
{
    uint32_t *argv;
    va_list ap;
    int ret;

    if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 5)) {
        assert(0);
    }

    argv = (uint32_t *)child_ctx_addr->frame.sp;

    switch (argv[0]) {
    case XIPFS_SYSCALL_EXIT:
        _exit((int)argv[1]);
        break;
    case XIPFS_SYSCALL_VPRINTF:
        __asm__ volatile
        (
            "mov %0, %1\n"
            : "=r" (ap)
            : "r" (argv[2])
            :
        );
        ret = vprintf((const char *)argv[1], ap);
        break;

    case XIPFS_SYSCALL_GET_TEMP:
        ret = get_temp();
        break;

    case XIPFS_SYSCALL_ISPRINT:
        ret = isprint((int)argv[1]);
        break;
    case XIPFS_SYSCALL_STRTOL:
        ret = strtol((const char *)argv[1], (char **)argv[2], (int)argv[3]);
        break;
    case XIPFS_SYSCALL_GET_LED:
        ret = get_led((int)argv[1]);
        break;
    case XIPFS_SYSCALL_SET_LED:
        ret = set_led((int)argv[1], (int)argv[2]);
        break;
    case XIPFS_SYSCALL_COPY_FILE:
    {
        const char *name = (const char *)argv[1];
        void *buf = (void *)argv[2];
        size_t nbyte = (size_t)argv[3];
        if (!Pip_mapMPU(riotPartDesc, child_flash_end_id, 3)) {
            assert(0);
        }
        ret = copy_file(name, buf, nbyte);
        if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 3)) {
            assert(0);
        }
        break;
    }
    case XIPFS_SYSCALL_GET_FILE_SIZE:
    {
        const char *name = (const char *)argv[1];
        size_t size = 0;
        if (!Pip_mapMPU(riotPartDesc, child_flash_end_id, 3)) {
            assert(0);
        }
        ret = get_file_size(name, &size);
        if (!Pip_mapMPU(riotPartDesc, child_block_0_id, 3)) {
            assert(0);
        }
        *((size_t *)argv[2]) = size;
        break;
    }
    case XIPFS_SYSCALL_MEMSET:
        ret = (int)(uintptr_t)memset((void *)argv[1], (int)argv[2], (size_t)argv[3]);
        break;
    default :
        ret = 0;
        break;
    }

    child_ctx_addr->frame.sp -= 4;
    *((uint32_t *)(child_ctx_addr->frame.sp)) = ret;

    if (!Pip_mapMPU(riotPartDesc, NULL, 5)) {
        assert(0);
    }
}

static void dispatcher(void)
{
    switch (riotVidt->currentInterrupt) {
    case RIOT_VIDT_MEMFAULT:
        memfault_handler();
        break;
    case RIOT_VIDT_SYSCALL:
        syscall_handler();
        break;
    /*case RIOT_VIDT_DISCARD:
        return;*/
    }

    Pip_yield(child_pd_id, 0, RIOT_VIDT_DISCARD, 1, 1);

    for (;;);
}

int _safe_exec_callback(int argc, char **argv)
{
    void *riot_krn_addr;
    void *riot_krn_id;
    void    *child_pd_addr, *child_krn_addr, *child_stk_addr,
            /**ram_end,*/ *flash_start, *flash_end,
            *child_shared_data, *child_vidt_plus_512, /**child_vidt_plus_512_cut,*/
            *child_args_addr;
    void *child_end_of_block3_id;
    vidt_t *child_vidt_addr;
    void *child_krn_id/*, *child_end_id*/;
    void *child_block_0_idc, *child_block_1_idc,
         *child_block_2_idc, *child_block_3_idc;
    void *riot_memfault_ctx_backup, *riot_stk_ctx_backup;
    file_t *file;
    size_t i, j, k;

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

    /* Disable interrupts */
    Pip_setIntState(0);

    /* Initialize status */
    riot_status = 0;

    /* RIOT's kernel structure needed to create the child */
    //riot_krn_addr = (void *)ROUND((uintptr_t)unusedRamStart, 1024);
    riot_krn_addr = (void *)RIOT_BLOCK_START_3;

    /* Child's partition descriptor block */
    child_pd_addr = (char *)riot_krn_addr + 512;
    /* Child's kernel structure */
    child_krn_addr = (char *)child_pd_addr + 512;


    //child_stk_addr      = (void   *)ROUND((uintptr_t)child_krn_addr + 512, 1024);
    child_stk_addr      = (void   *)(RIOT_BLOCK_START_1 + (RIOT_BLOCK_SIZE_1 / 2));
    child_shared_data   = (void   *)((char *)child_stk_addr + 1024);
    child_vidt_addr     = (vidt_t *)(uintptr_t)((char *)child_shared_data + 512);
    child_vidt_plus_512 = (void   *)((char *)child_vidt_addr + 512);

    child_crt0_ctx      = (void *)ROUND((uintptr_t)child_vidt_plus_512 + 512, 2048);
    xipfs_crt0_ctx_data =
        (xipfs_crt0_ctx_data_t *)(uintptr_t)((char *)child_crt0_ctx + sizeof(*child_crt0_ctx));
    child_ctx_addr      =
        (basicContext_t *)(uintptr_t)((char *)xipfs_crt0_ctx_data + sizeof(xipfs_crt0_ctx_data_t));
    child_args_addr     = (char *)child_ctx_addr + sizeof(*child_ctx_addr);
    child_crt0_ctx->ram_start = (char *)child_args_addr + SHELL_DEFAULT_BUFSIZE;
    child_crt0_ctx->ram_end   = (char *)RIOT_BLOCK_END_2;
    //ram_end = child_crt0_ctx->ram_end;

    child_crt0_ctx->argv      = xipfs_crt0_ctx_data;

    /* MPU BLOCK 2 - aligned: 4096, size: Multiple of 4096 */
    xipfs_crt0_ctx_data->file_base = (void *)file;
    flash_start = xipfs_crt0_ctx_data->file_base;
    child_crt0_ctx->bin_base  = (void *)( ((char *)file) + sizeof(*file) );
    child_crt0_ctx->nvm_start = (void *)((uintptr_t)file + sizeof(*file) + file->size);
    child_crt0_ctx->nvm_end = (void *)ROUND(
        (uintptr_t)file + sizeof(*file) + file->size, FLASHPAGE_SIZE );
    flash_end = child_crt0_ctx->nvm_end;

    xipfs_crt0_ctx_data->is_safe_call = 1;

    memcpy(child_shared_data, xipfs_syscall_table, sizeof(xipfs_syscall_table));
    xipfs_crt0_ctx_data->syscall_table = child_shared_data;

    /*
     * Prepare arguments
     *
     * We need to copy args in RAM's executable otherwise the MPU would trigger
     * a fault when the program would access them.
     */
    for (i = 1, j = 0; i < (size_t)argc; i++) {
        xipfs_crt0_ctx_data->argv[i - 1] = &( ((char *)child_args_addr)[j] );

        for (k = 0; argv[i][k] != '\0'; ++k, ++j) {
            ((char *)child_args_addr)[j] = argv[i][k];
        }
        ((char *)child_args_addr)[j++] = '\0';
    }
    xipfs_crt0_ctx_data->argc = argc - 1;

    xipfs_crt0_ctx_data->former_got = riotGotAddr;

    /* Fill in the child's context */
    (void)memset(child_ctx_addr, 0, sizeof(*child_ctx_addr));
    child_ctx_addr->isBasicFrame = 1;
    child_ctx_addr->pipflags = 1;
    child_ctx_addr->frame.r0 = (uint32_t)child_crt0_ctx;
    child_ctx_addr->frame.sp = (uint32_t)child_stk_addr + 1024;
    child_ctx_addr->frame.pc = THUMB_ADDRESS((uint32_t)file + sizeof(*file));
    child_ctx_addr->frame.xpsr = INITIAL_XPSR;

    /* Fill in RIOT's dispatcher context */
    (void)memset(&riot_dsp_ctx, 0, sizeof(riot_dsp_ctx));
    riot_dsp_ctx.isBasicFrame = 1;
    riot_dsp_ctx.pipflags = 0;
    riot_dsp_ctx.frame.r10 = (uint32_t)riotGotAddr;
    riot_dsp_ctx.frame.sp = (uint32_t)riot_stk_addr + 512;
    riot_dsp_ctx.frame.pc = THUMB_ADDRESS( (uint32_t)dispatcher );
    riot_dsp_ctx.frame.xpsr = INITIAL_XPSR;

    /* Prepare Child's VIDT */
    (void)memset(child_vidt_addr, 0, sizeof(*child_vidt_addr));
    ((vidt_t *)child_vidt_addr)->contexts[0] = child_ctx_addr;

    /* Backup both original memfault context */
    riot_stk_ctx_backup      = riotVidt->contexts[0];
    riot_memfault_ctx_backup = riotVidt->contexts[RIOT_VIDT_MEMFAULT];

    /* Prepare RIOT's VIDT */
    /* Set the save context for stream execution continuation after the yield to the child */
    riotVidt->contexts[0] = &riot_save_ctx;
    /* Set the dispatcher context for handled signals */
    riotVidt->contexts[RIOT_VIDT_MEMFAULT] = &riot_dsp_ctx;
    riotVidt->contexts[RIOT_VIDT_SYSCALL] = &riot_dsp_ctx;
    /* Set NULL to discard context saving */
    riotVidt->contexts[RIOT_VIDT_DISCARD] = NULL;

    riot_krn_id = RIOT_BLOCK_ID_3;

    if ((child_pd_id = Pip_cutMemoryBlock(riot_krn_id,
        child_pd_addr, MAP_DISCARD) ) == NULL) {
        goto abort;
    }

    if ((child_krn_id = Pip_cutMemoryBlock(child_pd_id,
        child_krn_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if (!Pip_prepare(riotPartDesc, riot_krn_id)) {
        goto abort;
    }

    if (!Pip_createPartition(child_pd_id)) {
        goto abort;
    }

    if (!Pip_prepare(child_pd_id, child_krn_id)) {
        goto abort;
    }

    if ((child_block_0_id = Pip_cutMemoryBlock(RIOT_BLOCK_ID_1,
        child_stk_addr, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_block_3_id = Pip_cutMemoryBlock(RIOT_BLOCK_ID_4,
        &_start_shared_api_code, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_end_of_block3_id = Pip_cutMemoryBlock(child_block_3_id,
        &_end_shared_api_code, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_block_2_id = Pip_cutMemoryBlock(child_end_of_block3_id,
        flash_start, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_flash_end_id = Pip_cutMemoryBlock(child_block_2_id,
        flash_end, MAP_DISCARD)) == NULL) {
        goto abort;
    }

    if ((child_block_0_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_0_id, 1, 1, 0)) == NULL) {
        goto abort;
    }

    if ((child_block_1_idc = Pip_addMemoryBlock(child_pd_id,
        RIOT_BLOCK_ID_2, 1, 1, 0)) == NULL) {
        goto abort;
    }

    if ((child_block_2_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_2_id, 1, 0, 1)) == NULL) {
        goto abort;
    }

    if ((child_block_3_idc = Pip_addMemoryBlock(child_pd_id,
        child_block_3_id, 1, 0, 1)) == NULL) {
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

    if (!Pip_mapMPU(child_pd_id, child_block_3_idc, 3)) {
        goto abort;
    }

    if (!Pip_setVIDT(child_pd_id, (void *)child_vidt_addr)) {
        goto abort;
    }

    Pip_setIntState(1);

    Pip_yield(child_pd_id, 0, 0, 1, 1);

    Pip_setIntState(0);

    if (!Pip_setVIDT(child_pd_id, NULL)) {
        goto abort;
    }

    if (!Pip_mapMPU(child_pd_id, NULL, 3)) {
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

    if (!Pip_removeMemoryBlock(child_block_3_id)) {
        goto abort;
    }

    if (!Pip_removeMemoryBlock(child_block_2_id)) {
        goto abort;
    }

    if (!Pip_removeMemoryBlock(RIOT_BLOCK_ID_2)) {
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

    if (Pip_mergeMemoryBlocks(child_end_of_block3_id, child_block_2_id,
        MAP_DISCARD)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_block_3_id, child_end_of_block3_id,
        MAP_DISCARD) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(RIOT_BLOCK_ID_4, child_block_3_id, 4)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(RIOT_BLOCK_ID_1, child_block_0_id, 1)
        == NULL) {
        goto abort;
    }

    if (Pip_collect(riotPartDesc) == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(child_pd_id, child_krn_id, MAP_DISCARD)
        == NULL) {
        goto abort;
    }

    if (Pip_mergeMemoryBlocks(riot_krn_id, child_pd_id, 3) == NULL) {
        goto abort;
    }

    /* Restore RIOT's VIDT */
    riotVidt->contexts[0]                  = riot_stk_ctx_backup;
    riotVidt->contexts[RIOT_VIDT_MEMFAULT] = riot_memfault_ctx_backup;
    riotVidt->contexts[RIOT_VIDT_SYSCALL] = NULL;
    riotVidt->contexts[RIOT_VIDT_DISCARD] = NULL;

    /* Enable interrupts */
    Pip_setIntState(1);

    return riot_status;

abort:
    /* What should we do? */
    for (;;);
}
