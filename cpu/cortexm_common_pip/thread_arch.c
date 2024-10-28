/*
 * Copyright (C) 2014-2015 Freie Universität Berlin
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
 * @brief       Implementation of the kernel's architecture dependent thread
 *              interface
 *
 * Members of the Cortex-M family know stacks and are able to handle register
 * backups partly, so we make use of that.
 *
 * Cortex-M3, Cortex-M33 and Cortex-M4 use the
 * following register layout when saving their context onto the stack:
 *
 * -------- highest address (bottom of stack)
 * | xPSR |
 * --------
 * | PC   |
 * --------
 * | LR   |
 * --------
 * | R12  |
 * --------
 * | R3   |
 * --------
 * | R2   |
 * --------
 * | R1   |
 * --------
 * | R0   | <- the registers from xPSR to R0 are handled by hardware
 * --------
 * | RET  | <- exception return code
 * --------
 * | R11  |
 * --------
 * | R10  |
 * --------
 * | R9   |
 * --------
 * | R8   |
 * --------
 * | R7   |
 * --------
 * | R6   |
 * --------
 * | R5   |
 * --------
 * | R4   | <- R4 lowest address (top of stack)
 * --------
 *
 * For the Cortex-M0, Cortex-M0+ and Cortex-M23 we use a slightly different
 * layout by switching the blocks R11-R8 and R7-R4. This allows more efficient
 * code when saving/restoring the context:
 *
 * ------------- highest address (bottom of stack)
 * | xPSR - R0 | <- same as for Cortex-M3/33/4
 * -------------
 * | RET  | <- exception return code
 * --------
 * | R7   |
 * --------
 * | R6   |
 * --------
 * | R5   |
 * --------
 * | R4   |
 * --------
 * | R11  |
 * --------
 * | R10  |
 * --------
 * | R9   |
 * --------
 * | R8   | <- lowest address (top of stack)
 * --------
 *
 * TODO: Implement handling of FPU registers for Cortex-M4 CPUs
 *
 *
 * @author      Stefan Pfeiffer <stefan.pfeiffer@fu-berlin.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Joakim Nohlgård <joakim.nohlgard@eistec.se>
 * @author      Koen Zandberg <koen@bergzand.net>
 *
 * @}
 */

#include <stdio.h>
#include <string.h>

#include "sched.h"
#include "thread.h"
#include "irq.h"
#include "cpu.h"
#include "context.h"

#define ENABLE_DEBUG 0
#include "debug.h"

extern void *riotPartDesc;
extern void *riotGotAddr;
extern vidt_t *riotVidt;

/* XXX hack - see ble_ll_adv.c */
uint32_t noyld = 0;

/**
 * @brief   CPU core supports full Thumb instruction set
 */
#if defined(CPU_CORE_CORTEX_M0) || defined(CPU_CORE_CORTEX_M0PLUS) || \
    defined(CPU_CORE_CORTEX_M23)
#define CPU_CORE_CORTEXM_FULL_THUMB 0
#else
#define CPU_CORE_CORTEXM_FULL_THUMB 1
#endif

/**
 * @brief   Noticeable marker marking the beginning of a stack segment
 *
 * This marker is used e.g. by *cpu_switch_context_exit* to identify the
 * stacks beginning.
 */
#define STACK_MARKER                (0x77777777)

/**
 * @brief   Initial program status register value for a newly created thread
 *
 * In the initial state, only the Thumb mode-bit is set
 */
#define INITIAL_XPSR                (0x01000000)

/**
 * @brief   ARM Cortex-M specific exception return value, that triggers the
 *          return to the task mode stack pointer
 */
#define EXCEPT_RET_TASK_MODE        (0xfffffffd)

char *thread_stack_init(thread_task_func_t task_func,
                             void *arg,
                             void *stack_start,
                             int stack_size)
{
    basicContext_t *intctx, *stkctx;
    void *sp, *spthd;

    /* set sp to the stack top */
    sp = (void *)((uintptr_t)stack_start +
        (uintptr_t)stack_size);

    /* align sp to a four-byte boundary */
    sp = (void *)(((uintptr_t)sp) & ~((uintptr_t)0x3));

    /* reserved place for the interrupted context */
    intctx = sp = (void *)((uintptr_t)sp -
        sizeof(extendedContext_t));

    /* reserved place for the stack marker */
    sp = (void *)((uintptr_t)sp - sizeof(void *));
    *(uint32_t *)sp = STACK_MARKER;

    sp = (void *)((uintptr_t)sp - sizeof(void *));
    /* align sp to a eight-byte boundray */
    if ((((uintptr_t)sp) & 0x7) != 0) {
        /* add a single word padding */
        sp = (void *)((uintptr_t)sp - sizeof(void *));
        *(uint32_t *)sp = ~((uint32_t)STACK_MARKER);
    }
    spthd = sp;

    /* reserved place for the stacked context */
    stkctx = sp = (void *)((uintptr_t)sp -
        sizeof(basicContext_t));

    /* initialize the interrupted context */
    (void)memset(intctx, 0, sizeof(extendedContext_t));
    intctx->isBasicFrame = 1;
    intctx->pipflags = 1;
    intctx->frame.r0 = (uint32_t)arg;
    intctx->frame.r10 = (uint32_t)riotGotAddr;
    intctx->frame.pc = (uint32_t)task_func;
    intctx->frame.sp = (uint32_t)spthd;
    intctx->frame.xpsr = INITIAL_XPSR;

    /* initialize the stacked context */
    (void)memset(stkctx, 0, sizeof(basicContext_t));
    stkctx->isBasicFrame = 1;
    stkctx->pipflags = 1;
    stkctx->frame.r0 = (uint32_t)arg;
    stkctx->frame.r10 = (uint32_t)riotGotAddr;
    stkctx->frame.pc = (uint32_t)task_func;
    stkctx->frame.sp = (uint32_t)spthd;
    stkctx->frame.xpsr = INITIAL_XPSR;

    /* check stack overflow */
    assert((uintptr_t)sp >= (uintptr_t)stack_start);

    return (char *)sp;
}

void thread_stack_print(void)
{
    int count = 0;
    /* The stack pointer will be aligned to word boundary by thread_create,
     * which is 32 bit for all Cortex M MCUs. We can silence -Wcast-align here
     */
    uint32_t *sp = (uint32_t *)(uintptr_t)thread_get_active()->sp;

    printf("printing the current stack of thread %" PRIkernel_pid "\n",
           thread_getpid());
    printf("  address:      data:\n");

    do {
        printf("  0x%08x:   0x%08x\n", (unsigned int)sp, (unsigned int)*sp);
        sp++;
        count++;
    } while (*sp != STACK_MARKER);

    printf("current stack size: %i byte\n", count);
}

int thread_isr_stack_usage(void)
{
    /* there is no ISR stack */
    return 0;
}

void *thread_isr_stack_pointer(void)
{
    /* there is no ISR stack */
    return NULL;
}

void *thread_isr_stack_start(void)
{
    /* there is no ISR stack */
    return NULL;
}

void NORETURN cpu_switch_context_exit(void)
{
    /* enable IRQs to make sure the PENDSV interrupt is reachable */
    irq_enable();

    thread_yield_higher();

    UNREACHABLE();
}

#if CPU_CORE_CORTEXM_FULL_THUMB
void __attribute__((used)) isr_pendsv(void)
{
    void *intctx, *stkctx, *curctx = NULL;
    thread_t *curthd, *newthd;

    /* XXX hack - see ble_ll_adv.c */
    if (noyld == 1)
        return;

    /* elect the new thread to run */
    curthd = thread_get_active();
    if ((newthd = sched_run()) == curthd)
        return;

    /* retrieve current thread interrupted context */
    if (curthd != NULL)
        curctx = (void *)(((uintptr_t)curthd -
            sizeof(extendedContext_t)) & ~(0x3));

    /* retrieve new thread interrupted context */
    intctx = (void *)(((uintptr_t)newthd -
        sizeof(extendedContext_t)) & ~(0x3));

    switch (*(uint32_t *)intctx) {
    case 0: /* extended context */
        /* update sp field of the new thread structure */
        newthd->sp = (char *)(uintptr_t)
            ((extendedContext_t *)intctx)->frame.sp;
        /* retrieve stacked context */
        stkctx = (void *)((((uintptr_t)newthd->sp -
            0x68) & ~(0x4)) - 108);
        /* sanity check */
        assert(memcmp(&((extendedContext_t *)intctx)->frame,
            &((extendedContext_t *)stkctx)->frame,
            sizeof(extendedFrame_t)) == 0);
        break;
    case 1: /* basic context */
        /* update sp field of the new thread structure */
        newthd->sp = (char *)(uintptr_t)
            ((basicContext_t *)intctx)->frame.sp;
        /* retrieve stacked context */
        stkctx = (void *)((((uintptr_t)newthd->sp -
            0x20) & ~(0x4)) - 44);
        /* sanity check */
        assert(memcmp(&((basicContext_t *)intctx)->frame,
            &((basicContext_t *)stkctx)->frame,
            sizeof(basicFrame_t)) == 0);
        break;
    default: /* corrupted context */
        assert(0);
    }

    /* update riot vidt */
    riotVidt->contexts[0] = stkctx;
    riotVidt->contexts[9] = intctx;
    riotVidt->contexts[47] = curctx;

    /* restore index 0 and save the current context at index 47 */
    (void)Pip_yield(riotPartDesc, 0, 47, 1, 1);
}
#else /* CPU_CORE_CORTEXM_FULL_THUMB */
void __attribute__((naked)) __attribute__((used)) isr_pendsv(void) {
    __asm__ volatile (
    /* PendSV handler entry point */
    /* save context by pushing unsaved registers to the stack */
    /* {r0-r3,r12,LR,PC,PSR} are saved automatically on exception entry */
    ".thumb_func                      \n"
    ".syntax unified                  \n"

    /* skip context saving if sched_active_thread == NULL */
    "ldr    r1, =sched_active_thread  \n" /* r1 = &sched_active_thread  */
    "push   {r4,lr}                   \n" /* push r4 and exception return code */
    "ldr    r4, [r1]                  \n" /* r4 = sched_active_thread */

    "cpsid  i                         \n" /* Disable IRQs during sched_run */
    "bl     sched_run                 \n" /* perform scheduling */
    "cpsie  i                         \n" /* Re-enable interrupts */

    /* Cortex-M0, Cortex-M0+ and Cortex-M23 */
    "cmp    r0, r4                    \n" /* if r0 == previous_thread: */
    "bne    cont_schedule             \n" /*   jump over pop if r0 != 0 */
    "pop    {r4,pc}                   \n" /*   Pop exception return to PC */

    "cont_schedule:                   \n" /* Otherwise continue the ctx switch */

    "pop    {r1,r2}                   \n" /* Pop LR from the exception stack */
                                          /* r1 contains r4 from the thread */
    "mov    lr, r2                    \n" /* Store LR in lr */
    "cmp    r4, #0                    \n" /* Test if sched_active_thread == NULL */
    "mov    r12, sp                   \n" /* remember the exception SP in r12 */
    "beq    restore_context           \n" /* goto restore_context if r1 == NULL */

    "mrs    r0, psp                   \n" /* get stack pointer from user mode */
    "mov    sp, r0                    \n" /* set user mode SP as active SP */

    /* Calculate the expected stack offset beforehand so that we don't have to
     * store the old SP from here on, saves a register we don't have */
    "subs   r0, #36                   \n" /* Move saved SP with 9 words */
    "str    r0, [r4]                  \n" /* And store */
    "mov    r4, r1                    \n" /* restore r4 content */

    /* we can not push high registers directly, so we move R11-R8 into
     * R4-R0, as these are already saved */
    "mov    r0, r8                    \n" /* move R11-R8 into R3-R0 */
    "mov    r1, r9                    \n"
    "mov    r2, r10                   \n"
    "mov    r3, r11                   \n"
    "push   {r0-r7,lr}                \n" /* now push them onto the stack */
    /* SP should match the expected SP calculated above from here on */

    /* current thread context is now saved */

    "restore_context:                 \n" /* Label to skip thread state saving */

    "ldr    r0, =sched_active_thread  \n" /* load address of current TCB */
    "ldr    r0, [r0]                  \n" /* dereference TCB */
    "ldr    r0, [r0]                  \n" /* load tcb-sp to R0 */
    "mov    sp, r0                    \n" /* make user mode SP active SP */
    "pop    {r0-r7}                   \n" /* get R11-R8 and R7-R4 from stack */
    "mov    r8, r0                    \n" /* move R11-R8 to correct registers */
    "mov    r9, r1                    \n"
    "mov    r10, r2                   \n"
    "mov    r11, r3                   \n"
    "pop    {r0}                      \n" /* restore LR from stack */
    /* restore the application mode stack pointer PSP */
    "mov    r1, sp                    \n" /* restore the user mode SP */
    "msr    psp, r1                   \n" /* for this write it to the PSP reg */
    "mov    sp, r12                   \n" /* and get the parked MSR SP back */
    "bx     r0                        \n" /* load exception return value to PC,
                                           * causes end of exception*/

    /* return from exception mode to application mode */
    /* {r0-r3,r12,LR,PC,xPSR,s0-s15,FPSCR} are restored automatically on exception return */
     ".ltorg                           \n" /* literal pool needed to access
                                            * sched_active_thread */
     :
     :
     :
    );
}
#endif

#ifdef MODULE_CORTEXM_SVC
void __attribute__((naked)) __attribute__((used)) isr_svc(void)
{
    /* these two variants do exactly the same, but Cortex-M3 can use Thumb2
     * conditional execution, which are a bit faster. */

    /* TODO: currently, cpu_switch_context_exit() is used to start threading
     * from kernel_init(), which executes on MSP.  That could probably be
     * rewritten to not use the supervisor call at all. Then we can assume that
     * svc is only used by threads, saving a couple of instructions. /Kaspar
     */

#if defined(CPU_CORE_CORTEX_M0) || defined(CPU_CORE_CORTEX_M0PLUS) \
    || defined(CPU_CORE_CORTEX_M23)
    __asm__ volatile (
    ".thumb_func            \n"
    "movs   r0, #4          \n" /* if bit4(lr) == 1):       */
    "mov    r1, lr          \n"
    "tst    r0, r1          \n"
    "beq    came_from_msp   \n" /*     goto came_from_msp   */
    "mrs    r0, psp         \n" /* r0 = psp                 */
    "b      _svc_dispatch   \n" /* return svc_dispatch(r0)  */
    "came_from_msp:         \n"
    "mrs    r0, msp         \n" /* r0 = msp                 */
    "b      _svc_dispatch   \n" /* return svc_dispatch(r0)  */
    );
#else
    __asm__ volatile (
    ".thumb_func            \n"
    "tst    lr, #4          \n" /* switch bit4(lr) == 1):   */
    "ite    eq              \n"
    "mrseq  r0, msp         \n" /* case 1: r0 = msp         */
    "mrsne  r0, psp         \n" /* case 0: r0 = psp         */
    "b      _svc_dispatch   \n" /* return svc_dispatch()    */
    );
#endif
}

static void __attribute__((used)) _svc_dispatch(unsigned int *svc_args)
{
    /* stack frame:
     * r0, r1, r2, r3, r12, r14, the return address and xPSR
     * - r0   = svc_args[0]
     * - r1   = svc_args[1]
     * - r2   = svc_args[2]
     * - r3   = svc_args[3]
     * - r12  = svc_args[4]
     * - lr   = svc_args[5]
     * - pc   = svc_args[6]
     * - xPSR = svc_args[7]
     */

    /* svc_args[6] is the stacked PC / return address. It is the address of the
     * instruction after the SVC.  The SVC instruction is located in the memory
     * address [stacked_PC - 2], because SVC is a 2 byte instruction.  The SVC
     * number is the lower byte of the instruction.
     */
    unsigned int svc_number = ((char *)svc_args[6])[-2];

    switch (svc_number) {
        case 1: /* SVC number used by cpu_switch_context_exit */
            SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
            break;
        default:
            DEBUG("svc: unhandled SVC #%u\n", svc_number);
            break;
    }
}

#else /* MODULE_CORTEXM_SVC */
void __attribute__((used)) isr_svc(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
}
#endif /* MODULE_CORTEXM_SVC */

void sched_arch_idle(void)
{
#ifdef MODULE_PM_LAYERED
    void pm_set_lowest(void);
    pm_set_lowest();
#else
    /*
     * XXX Implement the Pip_wfi system call to reduce power
     *     consumption while waiting for interrupts.
     */
    /* __WFI(); */
#endif
    /* Briefly re-enable IRQs to allow pending interrupts to be serviced and
     * have them update the runqueue */
    __enable_irq();
    __ISB();
    __disable_irq();
}
