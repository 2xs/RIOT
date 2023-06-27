/*
 * Copyright (C) 2015 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_cortexm_common_pip
 * @{
 *
 * @file
 * @brief       Default implementations for Cortex-M specific interrupt and
 *              exception handlers
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Daniel Krebs <github@daniel-krebs.net>
 * @author      Joakim Gebart <joakim.gebart@eistec.se>
 * @author      Sören Tempel <tempel@uni-bremen.de>
 *
 * @}
 */

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>

#include "cpu.h"
#include "periph_cpu.h"
#include "kernel_init.h"
#include "board.h"
#ifdef MODULE_GNRC_XIPFS
#include "gnrc_xipfs.h"
#endif
#include "mpu.h"
#include "panic.h"
#include "sched.h"
#include "vectors_cortexm.h"
#ifdef MODULE_PUF_SRAM
#include "puf_sram.h"
#endif
#ifdef MODULE_DBGPIN
#include "dbgpin.h"
#endif

#include "interface.h"
#include "context.h"
#include "svc.h"

#ifndef SRAM_BASE
#define SRAM_BASE 0
#endif

#ifndef CPU_BACKUP_RAM_NOT_RETAINED
#define CPU_BACKUP_RAM_NOT_RETAINED 0
#endif

#define CORTEX_IRQ_NUMOF 15

/**
 * @brief   Memory markers, defined in the linker script
 * @{
 */
static uint32_t *sstack;
extern uint8_t _sram;
extern uint8_t _eram;
/** @} */

void *riotPartDesc;
void *riotGotAddr;
vidt_t *riotVidt;
#ifdef MODULE_GNRC_XIPFS
void *unusedRamStart;
#endif

/**
 * @brief   Pre-start routine for CPU-specific settings
 */
__attribute__((weak)) void pre_startup (void)
{
}

/**
 * @brief   Post-start routine for CPU-specific settings
 */
__attribute__((weak)) void post_startup (void)
{
}

/**
 * @brief   Function that will be called by the crt0.
 */
void start(interface_t *interface, void *gotaddr, void *oldgotaddr, void **syscalls)
{
    (void)oldgotaddr;
    (void)syscalls;
#ifdef MODULE_GNRC_XIPFS
    #define FS_ROUND(x, y) (((x) + (y) - 1) & ~((y) - 1))
    /* initialize the file system */
    //fs_initialize(interface->unusedRomStart, interface->romEnd);
    tinyfs_init((void *)0x28000, interface->romEnd);
    unusedRamStart = (void *)FS_ROUND((uintptr_t)interface->unusedRamStart, 4096);
#endif

    /* initialization of the heap */
    extern void heap_init(void *start, void *end);
    heap_init(interface->unusedRamStart, interface->ramEnd);

    /* initialization of global variables with
     * values only known at runtime */
    sstack = (uint32_t *)interface->stackLimit;

    extern void cortexm_pip_ctx_init(void *sp, void *sl);
    cortexm_pip_ctx_init(interface->stackTop, gotaddr);

    extern void cortexm_pip_vidt_init(vidt_t *vidt);
    cortexm_pip_vidt_init(interface->vidtStart);

    extern void nrf52_pip_ctx_init(void *sp, void *sl);
    nrf52_pip_ctx_init(interface->stackTop, gotaddr);

    extern void nrf52_pip_vidt_init(vidt_t *vidt);
    nrf52_pip_vidt_init(interface->vidtStart);

    riotPartDesc = interface->partDescBlockId;

    riotVidt = interface->vidtStart;

    riotGotAddr = gotaddr;

    Pip_setIntState(1);

    /* call RIOT entry point */
    reset_handler_default();
}

void reset_handler_default(void)
{
    pre_startup();

#ifdef DEVELHELP
    /* cppcheck-suppress constVariable
     * (top is modified by asm) */
    uint32_t *dst, *top;
    /* Fill stack space with canary values up until the current stack pointer */
    /* Read current stack pointer from CPU register */
    __asm__ volatile ("mov %[top], sp" : [top] "=r" (top) : : );
    dst = sstack;
    while (dst < top) {
        *(dst++) = STACK_CANARY_WORD;
    }
#endif

    post_startup();

#ifdef MODULE_DBGPIN
    dbgpin_init();
#endif

    /* initialize the CPU */
    extern void cpu_init(void);
    cpu_init();

    /* initialize the board (which also initiates CPU initialization) */
    board_init();

#if MODULE_NEWLIB || MODULE_PICOLIBC
    /* initialize std-c library (this must be done after board_init) */
    extern void __libc_init_array(void);
    __libc_init_array();
#endif

    /* startup the kernel */
    kernel_init();
}

__attribute__((weak))
void nmi_handler(void)
{
    core_panic(PANIC_NMI_HANDLER, "NMI HANDLER");
}

void hard_fault_default(void)
{
    core_panic(PANIC_HARD_FAULT, "HARD FAULT HANDLER");
}

#if defined(CPU_CORE_CORTEX_M3) || defined(CPU_CORE_CORTEX_M33) || \
    defined(CPU_CORE_CORTEX_M4) || defined(CPU_CORE_CORTEX_M4F) || \
    defined(CPU_CORE_CORTEX_M7)
void mem_manage_default(void)
{
    core_panic(PANIC_MEM_MANAGE, "MEM MANAGE HANDLER");
}

void bus_fault_default(void)
{
    core_panic(PANIC_BUS_FAULT, "BUS FAULT HANDLER");
}

void usage_fault_default(void)
{
    core_panic(PANIC_USAGE_FAULT, "USAGE FAULT HANDLER");
}

void debug_mon_default(void)
{
    core_panic(PANIC_DEBUG_MON, "DEBUG MON HANDLER");
}
#endif

void dummy_handler_default(void)
{
    core_panic(PANIC_DUMMY_HANDLER, "DUMMY HANDLER");
}

/* Cortex-M common interrupt vectors */
__attribute__((weak, alias("dummy_handler_default"))) void isr_svc(void);
__attribute__((weak, alias("dummy_handler_default"))) void isr_pendsv(void);
__attribute__((weak, alias("dummy_handler_default"))) void isr_systick(void);

/**
 * @brief   Handlers for each Cortex-M interrupt.
 */
static const isr_t __attribute__((used))
cortexm_pip_handlers[CORTEX_IRQ_NUMOF] =
{
    [ 0] = reset_handler_default,
    [ 1] = nmi_handler,
    [ 2] = hard_fault_default,
    [10] = isr_svc,
    [13] = isr_pendsv,
    [14] = isr_systick,

    #ifdef CORTEXM_VECTOR_RESERVED_0X1C
    [6] = (isr_t)(CORTEXM_VECTOR_RESERVED_0X1C),
    #endif  /* CORTEXM_VECTOR_RESERVED_0X1C */
    #ifdef CORTEXM_VECTOR_RESERVED_0X20
    [7] = (isr_t)(CORTEXM_VECTOR_RESERVED_0X20),
    #endif  /* CORTEXM_VECTOR_RESERVED_0X20 */
    #ifdef CORTEXM_VECTOR_RESERVED_0X24
    [8] = (isr_t)(CORTEXM_VECTOR_RESERVED_0X24),
    #endif  /* CORTEXM_VECTOR_RESERVED_0X24 */
    #ifdef CORTEXM_VECTOR_RESERVED_0X28
    [9] = (isr_t)(CORTEXM_VECTOR_RESERVED_0X28),
    #endif  /* CORTEXM_VECTOR_RESERVED_0X28 */

#if defined(CPU_CORE_CORTEX_M3) || defined(CPU_CORE_CORTEX_M33) || \
    defined(CPU_CORE_CORTEX_M4) || defined(CPU_CORE_CORTEX_M4F) || \
    defined(CPU_CORE_CORTEX_M7)
    [ 3] = mem_manage_default,
    [ 4] = bus_fault_default,
    [ 5] = usage_fault_default,
    [11] = debug_mon_default,
#endif
};

/**
 * @brief   Interrupt dispatcher for each Cortex-M interrupt.
 *
 * 1. Retrieves the context address of the interrupted thread at
 *    the index 9 of the VIDT;
 *
 * 2. Computes the stack top address, i.e. the address where the
 *    context of the interrupted thread is located on its stack;
 *
 * 3. Moves the stack pointer to the address where the context
 *    of the interrupted thread is located on its stack;
 *
 * 4. Executes the handler corresponding to the interrupt number;
 *
 * 5. Stores the address of the interrupted thread's context on
 *    its stack at index 0 of the VIDT;
 *
 * 6. Executes the system call yield to restore the context at
 *    index 0 of the VIDT.
 *
 * This behavior allows stacking interrupt contexts in the
 * manner of RIOT.
 */
void __attribute__((naked)) cortexm_pip_dispatcher(void)
{
    /*
     * We use r4-r8 because they are callee-saved registers.
     */
    __asm__ volatile
    (
        /* void *intctx, *stkctx;                                  */
        /* isr_t *handler;                                         */
        /* vidt_t *vidt;                                           */
        /* size_t intno;                                           */
        /*                                                         */
        /* extern vidt_t *riotVidt;                                */
        /* vidt = riotVidt;                                        */
        "ldr    r4, .L1                                           \n"
        "ldr    r4, [r10, r4]                                     \n"
        "ldr    r4, [r4]                                          \n"
        /* intctx = vidt->contexts[9];                             */
        /* if (intctx == NULL)                                     */
        /*     goto skip;                                          */
        "ldr    r5, [r4, #40]                                     \n"
        "cbz    r5, 1f                                            \n"
        /* XXX check if the context is corrupted                   */
        /* if (*(uint32_t *)intctx == 0)                           */
        "ldr    r6, [r5]                                          \n"
        "cmp    r6, #0                                            \n"
        "ittee  eq                                                \n"
        /*     stkctx = (void *)(((extendedContext_t *)intctx)->   */
        /*         frame.sp - 0x68);                               */
        "ldreq  r6, [r5, #72]                                     \n"
        "subeq  r6, #0x68                                         \n"
        /* else                                                    */
        /*     stkctx = (void *)(((basicContext_t *)intctx)->      */
        /*         frame.sp - 0x20);                               */
        "ldrne  r6, [r5, #8]                                      \n"
        "subne  r6, #0x20                                         \n"
        /* stkctx = (void *)((uintptr_t)stkctx & ~(0x4));          */
        "bic    r6, #4                                            \n"
        /* if (*(uint32_t *)intctx == 0)                           */
        "ite    eq                                                \n"
        /*     stkctx = (void *)((uintptr_t)stkctx - 108);         */
        "subeq  r6, #108                                          \n"
        /* else                                                    */
        /*     stkctx = (void *)((uintptr_t)stkctx - 44);          */
        "subne  r6, #44                                           \n"
        /* ((basicContext_t *)stkctx)->pipflags =                  */
        /*    ((basicContext_t *)intctx)->pipflags;                */
        "ldr    r5, [r5, #4]                                      \n"
        "str    r5, [r6, #4]                                      \n"
        /* __asm__ volatile                                        */
        /* (                                                       */
        /*     "mov sp, %0"                                        */
        /*     :                                                   */
        /*     : "r" (stkctx)                                      */
        /*     :                                                   */
        /* );                                                      */
        "mov    sp, r6                                            \n"
        /* skip:                                                   */
        "1:                                                       \n"
        /* intno = vidt->currentInterrupt - 1;                     */
        "ldr    r5, [r4]                                          \n"
        "subs   r5, #1                                            \n"
        /* handler = cortexm_pip_handlers[intno];                  */
        "ldr    r6, .L1+4                                         \n"
        "ldr    r6, [r10, r6]                                     \n"
        "ldr    r6, [r6, r5, lsl #2]                              \n"
        /* handler();                                              */
        "blx    r6                                                \n"
        /* Pip_setIntState(0);                                     */
        "mov    r0, #0                                            \n"
        "svc    #15                                               \n"
        /* vidt->contexts[0] = stkctx;                             */
        "str    sp, [r4, #4]                                      \n"
        /* Pip_yield(riotPartDesc, 0, 46, 0, 0);                   */
        "ldr    r4, .L1+8                                         \n"
        "ldr    r0, [r10, r4]                                     \n"
        "ldr    r0, [r0]                                          \n"
        "movs   r1, #0                                            \n"
        "movs   r2, #46                                           \n"
        "movs   r3, #0                                            \n"
        "movs   r4, #0                                            \n"
        "svc    #12                                               \n"
        /* for (;;);                                               */
        "b      .                                                 \n"
        ".align 2                                                 \n"
        ".L1:                                                     \n"
        ".word riotVidt(GOT)                                      \n"
        ".word cortexm_pip_handlers(GOT)                          \n"
        ".word riotPartDesc(GOT)                                  \n"
    );
}

/**
 * @brief   Context for each Cortex-M interrupt.
 */
static basicContext_t cortexm_pip_ctx = {
    .isBasicFrame = 1,
    /* We must not be interrupted in exception handler. */
    .pipflags = 0,
    .frame = {
        /* the SP field will be initialized in the
	 * start() function because its value will
	 * only be known at runtime */
        .sp = 0,
        .r4 = 0,
        .r5 = 0,
        .r6 = 0,
        .r7 = 0,
        .r8 = 0,
        .r9 = 0,
        /* the R10 field will be initialized in the
	 * start() function because its value will
	 * only be known at runtime. */
        .r10 = 0,
        .r11 = 0,
        .r0 = 0,
        .r1 = 0,
        .r2 = 0,
        .r3 = 0,
        .r12 = 0,
        .lr = 0,
        .pc = (uint32_t) cortexm_pip_dispatcher,
        .xpsr = 0
    }
};

/**
 * @brief   Initialize the context for each Cortex-M interrupt
 *          with values only known at runtime.
 *
 * @param sp The address of the stack pointer.
 *
 * @param sl The address of the GOT.
 */
void cortexm_pip_ctx_init(void *sp, void *sl)
{
    cortexm_pip_ctx.frame.sp = (uint32_t)sp;
    cortexm_pip_ctx.frame.r10 = (uint32_t)sl;
}

/**
 * @brief   Initialize the entries of the VIDT corresponding to
 *          the interrupts of the Cortex-M with the address of
 *          the handler's context.
 *
 * @param vidt The address of the VIDT of RIOT.
 */
void cortexm_pip_vidt_init(vidt_t *vidt)
{
    vidt->currentInterrupt = 0;
    /* the index 0 is reserved for the current thread */
    vidt->contexts[0] = NULL;
    for (size_t i = 1; i < 16; i++) {
        vidt->contexts[i] = &cortexm_pip_ctx;
    }
    /*
     * The index 8 is reserved by Pip to save an interrupted context
     * when a partition asks to be CLI. This is not relevant for the
     * root partition.
     */
    vidt->contexts[8] = NULL;
    /*
     * The index 8 is reserved by Pip to save an interrupted context
     * when a partition asks to be STI.
     */
    vidt->contexts[9] = NULL;
}
