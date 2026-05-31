#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "mutex.h"
#include "thread.h"
#include "thread_flags.h"
#include "vfs.h"
#include "ztimer/periodic.h"

#include "ctx_manager_sched.h"

/* extern ASM */
extern int  ctx_save_asm(ctx_regs_t *r);
extern void ctx_restore_asm(ctx_regs_t *r) __attribute__((noreturn));
extern void _ctx_first_launch_asm(xipfs_file_t *f,
                                  char *const argv[],
                                  const void *syscalls[],
                                  uint8_t *stack_top)
                                  __attribute__((noreturn));

extern int xipfs_execv_check(xipfs_mount_t *,
                              const char *,
                              char *const [],
                              const void *,
                              xipfs_path_t *);

extern const void *xipfs_extended_driver_execv_syscalls[];

/* state */
static ctx_task_t _pool[CTX_SCHED_MAX_TASKS];
static ctx_task_t *_head = NULL;
static ctx_task_t *_cur = NULL;
static int _count = 0;

static kernel_pid_t _sched_pid;
static mutex_t _lock = MUTEX_INIT;

static uint8_t _stacks[CTX_SCHED_MAX_TASKS][CTX_SCHED_TASK_STACK_SIZE];
static char _sched_stack[CTX_SCHED_THREAD_STACK_SIZE];

static ctx_regs_t _sched_regs;
static ztimer_periodic_t _tick;

/* ========================= SAVE / RESTORE ========================= */

static inline int ctx_save(ctx_regs_t *r)
{
    return ctx_save_asm(r);
}

static inline void ctx_restore(ctx_regs_t *r)
{
    ctx_restore_asm(r);
}

/* ========================= YIELD ========================= */

static void _yield(void)
{
    if (!_cur) return;

    if (ctx_save(&_cur->regs) == 0) {
        _cur->state = CTX_TASK_READY;
        ctx_restore(&_sched_regs);
    }

    _cur->state = CTX_TASK_RUNNING;
}

/* ========================= SWITCH ========================= */

static void switch_to(ctx_task_t *t)
{
    _cur = t;

    if (t->state == CTX_TASK_CREATED) {

        xipfs_mount_t mp;
        xipfs_path_t p;

        if (xipfs_execv_check(&mp, t->path, t->argv,
                              xipfs_extended_driver_execv_syscalls,
                              &p) < 0) {
            t->state = CTX_TASK_DONE;
            return;
        }

        uint8_t *stack_top =
            (uint8_t *)((uintptr_t)(t->stack + t->stack_size) & ~7u);

        if (ctx_save(&_sched_regs) == 0) {

            mutex_lock(mp.execution_mutex);

            _ctx_first_launch_asm(
                p.witness,
                t->argv,
                xipfs_extended_driver_execv_syscalls,
                stack_top
            );

            mutex_unlock(mp.execution_mutex);
        }

    } else if (t->state == CTX_TASK_READY) {

        if (ctx_save(&_sched_regs) == 0) {
            ctx_restore(&t->regs);
        }
    }
}

/* ========================= SCHED THREAD ========================= */

static void *_sched(void *arg)
{
    (void)arg;
    _sched_pid = thread_getpid();

    while (1) {

        thread_flags_wait_any(CTX_FLAG_START);

        ztimer_periodic_start(&_tick);

        ctx_task_t *cur = _head;

        while (_count > 0) {

            if (!cur) break;

            if (cur->state != CTX_TASK_DONE) {
                switch_to(cur);

                if (cur->state == CTX_TASK_RUNNING) {
                    thread_flags_wait_any(CTX_FLAG_TICK);
                    _yield();
                }
            }

            cur = cur->next;
        }

        ztimer_periodic_stop(&_tick);

        memset(_pool, 0, sizeof(_pool));
        _count = 0;
        _head = NULL;
        _cur = NULL;
    }
}

/* ========================= API ========================= */

void ctx_sched_init(void)
{
    kernel_pid_t pid = thread_create(
        _sched_stack, sizeof(_sched_stack),
        CTX_SCHED_THREAD_PRIORITY,
        THREAD_CREATE_STACKTEST | THREAD_CREATE_SLEEPING,
        _sched, NULL, "ctx_sched"
    );

    _sched_pid = pid;
}

int ctx_sched_enqueue(const char *path)
{
    if (!path) return -EINVAL;

    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) return -ENOENT;
    vfs_close(fd);

    mutex_lock(&_lock);

    ctx_task_t *t = &_pool[_count++];

    memset(t, 0, sizeof(*t));
    strncpy(t->path, path, sizeof(t->path)-1);

    t->argv[0] = t->path;
    t->argv[1] = NULL;

    t->stack = _stacks[_count-1];
    t->stack_size = CTX_SCHED_TASK_STACK_SIZE;

    t->state = CTX_TASK_CREATED;

    if (!_head) {
        _head = t;
        t->next = t;
    } else {
        ctx_task_t *n = _head;
        while (n->next != _head) n = n->next;
        n->next = t;
        t->next = _head;
    }

    mutex_unlock(&_lock);
    return 0;
}

void ctx_sched_start(void)
{
    thread_wakeup(_sched_pid);
    thread_flags_set(thread_get(_sched_pid), CTX_FLAG_START);
}