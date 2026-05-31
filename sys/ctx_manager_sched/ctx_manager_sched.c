#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ctx_manager_sched.h"
#include "ztimer.h"
#include "ztimer/periodic.h"
#include "vfs.h"
#include "include/fs.h"   /* pour XIPFS_SYSCALL_MAX */

static const void *_local_syscalls[XIPFS_SYSCALL_MAX];

#ifndef CTX_SCHED_THREAD_PRIORITY
#define CTX_SCHED_THREAD_PRIORITY  THREAD_PRIORITY_MAIN - 1
#endif
/* ================= INTERNAL STATE ================= */

static ctx_task_t _pool[CTX_SCHED_MAX_TASKS];
static uint8_t _task_stacks[CTX_SCHED_MAX_TASKS][CTX_SCHED_TASK_STACK_SIZE];

static ctx_task_t *_head = NULL;
static ctx_task_t *_cur  = NULL;

static int _count = 0;

static kernel_pid_t _sched_pid = KERNEL_PID_UNDEF;
static mutex_t _lock = MUTEX_INIT;

static char _sched_stack[CTX_SCHED_THREAD_STACK_SIZE];

static ztimer_periodic_t _tick_timer;
static ctx_regs_t _sched_regs;

/* ================= XIPFS EXEC SYSCALLS ================= */__attribute__((naked))
int ctx_save(ctx_regs_t *r)
{
    __asm__ volatile (
        "stmia r0!, {r4-r11}\n"
        "mrs r1, psp\n"
        "str r1, [r0], #4\n"
        "str lr, [r0], #4\n"
        "mov r0, #0\n"
        "bx lr\n"
    );
}

__attribute__((naked))
void ctx_restore(ctx_regs_t *r)
{
    __asm__ volatile (
        "ldmia r0!, {r4-r11}\n"
        "ldr r1, [r0], #4\n"
        "msr psp, r1\n"
        "ldr lr, [r0]\n"
        "mov r0, #1\n"
        "bx lr\n"
    );
}



extern int xipfs_file_exec(
    xipfs_file_t *filp,
    char *const argv[],
    const void *user_syscalls[XIPFS_SYSCALL_MAX]
);

/* syscall table minimaliste (peut être étendue) */
static int _sys_exit(int code) { (void)code; return 0; }

static const void *_local_syscalls[XIPFS_SYSCALL_MAX] = {
    _sys_exit
};

/* ================= CONTEXT SWITCH (ASM EXTERN) ================= */

extern int  ctx_save(ctx_regs_t *r);
extern void ctx_restore(ctx_regs_t *r);

/* ================= YIELD ================= */

static void _ctx_yield(void)
{
    if (!_cur || _cur->state != CTX_TASK_RUNNING) return;

    if (ctx_save(&_cur->regs) == 0) {
        _cur->state = CTX_TASK_READY;
        ctx_restore(&_sched_regs);
    }

    _cur->state = CTX_TASK_RUNNING;
}

/* ================= SWITCH TASK ================= */

static void switch_to(ctx_task_t *t)
{
    _cur = t;

    if (t->state == CTX_TASK_CREATED) {
        t->state = CTX_TASK_RUNNING;

        if (!t->filp) {
            printf("[ctx] filp NULL\n");
            t->state = CTX_TASK_DONE;
            return;
        }

        uint8_t *stack_top =
            (uint8_t *)(((uintptr_t)(t->stack + t->stack_size)) & ~7u);

        if (ctx_save(&_sched_regs) == 0) {
            xipfs_file_exec(t->filp, t->argv, _local_syscalls);
        }

    } else if (t->state == CTX_TASK_READY) {
        t->state = CTX_TASK_RUNNING;

        if (ctx_save(&_sched_regs) == 0) {
            ctx_restore(&t->regs);
        }
    }
}

/* ================= TICK ISR ================= */

static bool _tick_cb(void *arg)
{
    (void)arg;

    if (_sched_pid != KERNEL_PID_UNDEF) {
        thread_t *t = thread_get(_sched_pid);
        if (t) thread_flags_set(t, CTX_FLAG_TICK);
    }
    return true;
}

/* ================= SCHEDULER THREAD ================= */

static void *_sched_thread(void *arg)
{
    (void)arg;

    _sched_pid = thread_getpid();

    while (1) {
        thread_flags_wait_any(CTX_FLAG_START);

        printf("[ctx] scheduler start (%d tasks)\n", _count);

        ztimer_periodic_init(ZTIMER_MSEC, &_tick_timer,
                             _tick_cb, NULL, CTX_SCHED_TICK_MS);
        ztimer_periodic_start(&_tick_timer);

        ctx_task_t *cur = _head;

        int remaining;

        do {
            remaining = 0;
            ctx_task_t *start = cur;

            do {
                if (cur->state == CTX_TASK_DONE) {
                    cur = cur->next;
                    continue;
                }

                remaining++;

                switch_to(cur);

                if (cur->state == CTX_TASK_RUNNING) {
                    thread_flags_wait_any(CTX_FLAG_TICK);
                    _ctx_yield();
                }

                cur = cur->next;

            } while (cur != start);

        } while (remaining > 0);

        ztimer_periodic_stop(&_tick_timer);

        printf("[ctx] all tasks done\n");

        mutex_lock(&_lock);
        memset(_pool, 0, sizeof(_pool));
        _count = 0;
        _head = NULL;
        _cur = NULL;
        mutex_unlock(&_lock);
    }

    return NULL;
}

/* ================= API ================= */

void ctx_sched_init(void)
{
    memset(_pool, 0, sizeof(_pool));
    memset(_task_stacks, 0, sizeof(_task_stacks));

    _count = 0;
    _head = NULL;
    _cur  = NULL;

    _sched_pid = KERNEL_PID_UNDEF;

    thread_create(
        _sched_stack, sizeof(_sched_stack),
        CTX_SCHED_THREAD_PRIORITY,
        THREAD_CREATE_STACKTEST | THREAD_CREATE_SLEEPING,
        _sched_thread,
        NULL,
        "ctx_sched"
    );
}

/* ================= ENQUEUE ================= */

int ctx_sched_enqueue(const char *path)
{
    if (!path) return -EINVAL;

    mutex_lock(&_lock);

    if (_count >= CTX_SCHED_MAX_TASKS) {
        mutex_unlock(&_lock);
        return -ENOMEM;
    }

    ctx_task_t *t = &_pool[_count];
    memset(t, 0, sizeof(*t));

    strncpy(t->path, path, sizeof(t->path) - 1);

    t->argv[0] = t->path;
    t->argv[1] = NULL;

    t->stack = _task_stacks[_count];
    t->stack_size = CTX_SCHED_TASK_STACK_SIZE;

    /* IMPORTANT: filp doit être résolu ici (simplification demandée) */
    t->filp = (xipfs_file_t *)vfs_open(path, O_RDONLY, 0);
    if (!t->filp) {
        mutex_unlock(&_lock);
        return -ENOENT;
    }

    t->state = CTX_TASK_CREATED;

    if (!_head) {
        _head = t;
        t->next = t;
    } else {
        ctx_task_t *tail = _head;
        while (tail->next != _head) tail = tail->next;
        tail->next = t;
        t->next = _head;
    }

    _count++;

    mutex_unlock(&_lock);

    return 0;
}

/* ================= START ================= */

void ctx_sched_start(void)
{
    if (!_count) return;

    thread_wakeup(_sched_pid);

    thread_t *t = thread_get(_sched_pid);
    if (t) thread_flags_set(t, CTX_FLAG_START);
}