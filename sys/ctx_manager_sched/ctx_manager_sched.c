#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <fcntl.h>

#include "ctx_manager_sched.h"
#include "ztimer.h"
#include "ztimer/periodic.h"
#include "vfs.h"
#include "include/fs.h"    /* xipfs_fs_head, xipfs_fs_next, XIPFS_SYSCALL_MAX */
#include "include/file.h"  /* xipfs_file_exec, xipfs_file_t                   */
#include "include/xipfs.h" /* xipfs_mount_t, xipfs_file_t                     */

#ifndef CTX_SCHED_THREAD_PRIORITY
#define CTX_SCHED_THREAD_PRIORITY  (THREAD_PRIORITY_MAIN - 1)
#endif




extern void xipfs_exec_exit(int status);

static const void *_syscalls[XIPFS_SYSCALL_MAX] = {
    [XIPFS_SYSCALL_EXIT]    = (void *)xipfs_exec_exit,
    [XIPFS_SYSCALL_VPRINTF] = (void *)vprintf,
    [XIPFS_SYSCALL_ISPRINT] = (void *)isprint,
    [XIPFS_SYSCALL_STRTOL]  = (void *)strtol,
    [XIPFS_SYSCALL_MEMSET]  = (void *)memset,
};

/* ================================================================
 * CONTEXT SAVE / RESTORE  (ARMv7-M, Cortex-M4 du DWM1001)
 *
 * ctx_save  : sauvegarde r4-r11, PSP, LR → retourne 0
 * ctx_restore : restaure r4-r11, PSP, LR → retourne 1 (dans ctx_save appelant)
 * ================================================================ */

__attribute__((naked))
int ctx_save(ctx_regs_t *r)
{
    __asm__ volatile (
        "stmia r0!, {r4-r11}\n"   /* save callee-saved regs      */
        "mrs   r1,  psp\n"        /* save PSP                    */
        "str   r1,  [r0], #4\n"
        "str   lr,  [r0], #4\n"   /* save EXC_RETURN / LR        */
        "mov   r0,  #0\n"         /* return 0 (first call)       */
        "bx    lr\n"
    );
}

__attribute__((naked))
void ctx_restore(ctx_regs_t *r)
{
    __asm__ volatile (
        "ldmia r0!, {r4-r11}\n"   /* restore callee-saved regs   */
        "ldr   r1,  [r0], #4\n"   /* restore PSP                 */
        "msr   psp, r1\n"
        "ldr   lr,  [r0]\n"       /* restore LR                  */
        "mov   r0,  #1\n"         
        "bx    lr\n"
    );
}


static ctx_task_t          _pool[CTX_SCHED_MAX_TASKS];
static uint8_t             _task_stacks[CTX_SCHED_MAX_TASKS][CTX_SCHED_TASK_STACK_SIZE];

static ctx_task_t         *_head      = NULL;
static ctx_task_t         *_cur       = NULL;
static int                 _count     = 0;

static kernel_pid_t        _sched_pid = KERNEL_PID_UNDEF;
static mutex_t             _lock      = MUTEX_INIT;
static char                _sched_stack[CTX_SCHED_THREAD_STACK_SIZE];

static ztimer_periodic_t   _tick_timer;
static ctx_regs_t          _sched_regs;

static vfs_xipfs_mount_t  *_vmp = NULL;

/* ================================================================
 * HELPER : vfs_xipfs_mount_t  xipfs_mount_t *
 *
 * Copie exacte de _get_xipfs_mount_t() dans xipfs_fs.c :
 *   xipfs_mp = &vfs_xipfs_mp->magic
 * ================================================================ */

static inline xipfs_mount_t *_mp(void)
{
    return (xipfs_mount_t *)(uintptr_t)&_vmp->magic;
}

/* ================================================================
 * HELPER : trouver xipfs_file_t* depuis un path absolu VFS
 *
 * Utilise xipfs_fs_head / xipfs_fs_next (fs.c) qui sont l'API
 * publique de parcours de la liste chaînée en flash.
 * ================================================================ */

static xipfs_file_t *_find_filp(const char *abs_path)
{
    if (!_vmp || !abs_path) return NULL;

    xipfs_mount_t *mp = _mp();

    /* Retirer le préfixe mount_path pour obtenir le path relatif */
    const char *relpath = abs_path;
    size_t mlen = strlen(mp->mount_path);
    if (strncmp(abs_path, mp->mount_path, mlen) == 0) {
        relpath = abs_path + mlen;
    }

    xipfs_file_t *f = xipfs_fs_head(mp);
    while (f != NULL) {
        if (strncmp(f->path, relpath, XIPFS_PATH_MAX) == 0) {
            return f;
        }
        f = xipfs_fs_next(f);
    }

    printf("[ctx] _find_filp: '%s' (rel='%s') not found\n", abs_path, relpath);
    return NULL;
}

/* ================================================================
 * CTX_EXEC : exécute un fichier exécutable (exec=1) depuis son path VFS
 *
 * 1. Trouver le filp correspondant au path (via _find_filp)
 * 2. Vérifier que le filp est exécutable
 * 3. Appeler xipfs_file_exec avec la table de syscalls adaptée
 * ================================================================ */
int ctx_exec(const char *path)
{
    if (!path) return -EINVAL;

    printf("[ctx_exec] '%s'\n", path);

    xipfs_mount_t *mp = (xipfs_mount_t *)(uintptr_t)&_vmp->magic;
    printf("[ctx_exec] mp=%p page_addr=%p mount_path='%s'\n",
           (void*)mp, mp->page_addr, mp->mount_path);

    xipfs_file_t *f = xipfs_fs_head(mp);
    printf("[ctx_exec] head=%p\n", (void*)f);
    while (f) {
        printf("[ctx_exec]   file path='%s' exec=%u filp=%p\n",
               f->path, (unsigned)f->exec, (void*)f);
        f = xipfs_fs_next(f);
    }

    xipfs_file_t *filp = _find_filp(path);
    printf("[ctx_exec] filp=%p\n", (void*)filp);
    if (!filp) return -ENOENT;
    if (!filp->exec) return -EACCES;

    char *argv[] = { (char *)path, NULL };

    int ret = xipfs_file_exec(filp, argv, (const void **)_syscalls);
    printf("[ctx_exec] returned %d\n", ret);
    return ret;
}
/* ================================================================
 * YIELD : sauvegarde le contexte courant, rend la main au scheduler
 * ================================================================ */

static void _ctx_yield(void)
{
    if (!_cur || _cur->state != CTX_TASK_RUNNING) return;

    if (ctx_save(&_cur->regs) == 0) {
        
        _cur->state = CTX_TASK_READY;
        ctx_restore(&_sched_regs);
        
    }
    
    _cur->state = CTX_TASK_RUNNING;
}

static void _switch_to(ctx_task_t *t)
{
    _cur = t;

    if (t->state == CTX_TASK_CREATED) {
        t->state = CTX_TASK_RUNNING;

        if (!t->filp) {
            printf("[ctx] filp NULL for '%s'\n", t->path);
            t->state = CTX_TASK_DONE;
            return;
        }

        if (ctx_save(&_sched_regs) == 0) {
            xipfs_file_exec(t->filp, t->argv, (const void **)_syscalls);
           
            t->state = CTX_TASK_DONE;
            ctx_restore(&_sched_regs);
           
        }
      

    } else if (t->state == CTX_TASK_READY) {
        t->state = CTX_TASK_RUNNING;

        if (ctx_save(&_sched_regs) == 0) {
            ctx_restore(&t->regs);
        }
       
    }
}

static bool _tick_cb(void *arg)
{
    (void)arg;
    if (_sched_pid != KERNEL_PID_UNDEF) {
        thread_t *t = thread_get(_sched_pid);
        if (t) thread_flags_set(t, CTX_FLAG_TICK);
    }
    return true;
}

static void *_sched_thread(void *arg)
{
    (void)arg;

    printf("[ctx] sched thread started\n");

    while (1) {
        printf("[ctx] waiting for START...\n");
        thread_flags_wait_any(CTX_FLAG_START);
        printf("[ctx] START received, %d tasks\n", _count);

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
                _switch_to(cur);

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
        _head  = NULL;
        _cur   = NULL;
        mutex_unlock(&_lock);
    }

    return NULL;
}

void ctx_sched_init(vfs_xipfs_mount_t *mp)
{
    _vmp = mp;

    memset(_pool,        0, sizeof(_pool));
    memset(_task_stacks, 0, sizeof(_task_stacks));

    _count     = 0;
    _head      = NULL;
    _cur       = NULL;

    _sched_pid = thread_create(
        _sched_stack, sizeof(_sched_stack),
        CTX_SCHED_THREAD_PRIORITY,
        THREAD_CREATE_STACKTEST | THREAD_CREATE_SLEEPING,
        _sched_thread,
        NULL,
        "ctx_sched"
    );

    printf("[ctx] scheduler thread created, pid=%d\n", _sched_pid);
}

int ctx_sched_enqueue(const char *path)
{
    if (!path) return -EINVAL;

    mutex_lock(&_lock);

    if (_count >= CTX_SCHED_MAX_TASKS) {
        mutex_unlock(&_lock);
        return -ENOMEM;
    }

    
    xipfs_file_t *filp = _find_filp(path);
    if (!filp) {
        mutex_unlock(&_lock);
        return -ENOENT;
    }
    if (!filp->exec) {
        mutex_unlock(&_lock);
        return -EACCES;
    }

    ctx_task_t *t = &_pool[_count];
    memset(t, 0, sizeof(*t));

    strncpy(t->path, path, sizeof(t->path) - 1);
    t->argv[0]    = t->path;
    t->argv[1]    = NULL;
    t->filp       = filp;
    t->stack      = _task_stacks[_count];
    t->stack_size = CTX_SCHED_TASK_STACK_SIZE;
    t->state      = CTX_TASK_CREATED;

    if (!_head) {
        _head   = t;
        t->next = t;
    } else {
        ctx_task_t *tail = _head;
        while (tail->next != _head) tail = tail->next;
        tail->next = t;
        t->next    = _head;
    }

    _count++;
    printf("[ctx] enqueued '%s' (%d/%d)\n", path, _count, CTX_SCHED_MAX_TASKS);

    mutex_unlock(&_lock);
    return 0;
}

void ctx_sched_start(void)
{
    if (!_count) {
        printf("[ctx] nothing to run\n");
        return;
    }
    printf("[ctx] starting %d task(s), sched_pid=%d\n", _count, _sched_pid);

    
    thread_t *t = thread_get(_sched_pid);
    if (!t) {
        printf("[ctx] ERROR: sched thread not found!\n");
        return;
    }
    thread_flags_set(t, CTX_FLAG_START);

    thread_wakeup(_sched_pid);
}