#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "ctx_manager_sched.h"
#include "vfs.h"
#include "include/fs.h"
#include "include/file.h"
#include "include/xipfs.h"
#include "saul_reg.h"
#include "phydat.h"

#ifndef CTX_SCHED_THREAD_PRIORITY
#define CTX_SCHED_THREAD_PRIORITY  (THREAD_PRIORITY_MAIN - 1)
#endif

extern void xipfs_exec_exit(int status);

static int _get_temperature(void)
{
    phydat_t res;
    saul_reg_t *dev = saul_reg_find_type(SAUL_SENSE_TEMP);
    if (!dev) return -1;
    if (saul_reg_read(dev, &res) < 0) return -1;
    return res.val[0];
}

static int _get_led(int pos)
{
    phydat_t res;
    saul_reg_t *dev = saul_reg_find_nth(pos);
    if (!dev) return -1;
    if (saul_reg_read(dev, &res) < 0) return -1;
    return res.val[0];
}

static int _set_led(int pos, int val)
{
    phydat_t res;
    saul_reg_t *dev = saul_reg_find_nth(pos);
    if (!dev) return -1;
    res.val[0] = val;
    return saul_reg_write(dev, &res);
}

static int _get_file_size(const char *path, size_t *size)
{
    struct stat buf;
    if (!path || !size) return -EFAULT;
    *size = 0;
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) return fd;
    int ret = vfs_fstat(fd, &buf);
    vfs_close(fd);
    if (ret < 0) return ret;
    *size = buf.st_size;
    return 0;
}

static int _copy_file(const char *path, void *buf, size_t nbyte)
{
    size_t file_size;
    int ret = _get_file_size(path, &file_size);
    if (ret < 0) return ret;
    if (file_size == 0 || nbyte < file_size) return -EINVAL;
    nbyte = file_size;
    int fd = vfs_open(path, O_RDONLY, 0);
    if (fd < 0) return fd;
    ret = vfs_read(fd, buf, nbyte);
    vfs_close(fd);
    return ret < 0 ? ret : (int)nbyte;
}

static const void *_syscalls[XIPFS_SYSCALL_MAX] = {
    [XIPFS_SYSCALL_EXIT]          = (void *)xipfs_exec_exit,
    [XIPFS_SYSCALL_VPRINTF]       = (void *)vprintf,
    [XIPFS_SYSCALL_GET_TEMP]      = (void *)_get_temperature,
    [XIPFS_SYSCALL_ISPRINT]       = (void *)isprint,
    [XIPFS_SYSCALL_STRTOL]        = (void *)strtol,
    [XIPFS_SYSCALL_GET_LED]       = (void *)_get_led,
    [XIPFS_SYSCALL_SET_LED]       = (void *)_set_led,
    [XIPFS_SYSCALL_COPY_FILE]     = (void *)_copy_file,
    [XIPFS_SYSCALL_GET_FILE_SIZE] = (void *)_get_file_size,
    [XIPFS_SYSCALL_MEMSET]        = (void *)memset,
};


static ctx_task_t   _pool[CTX_SCHED_MAX_TASKS];
static char         _task_stacks[CTX_SCHED_MAX_TASKS][CTX_SCHED_TASK_STACK_SIZE];

static ctx_task_t  *_head      = NULL;
static int          _count     = 0;
static kernel_pid_t _sched_pid = KERNEL_PID_UNDEF;
static mutex_t      _lock      = MUTEX_INIT;
static char         _sched_stack[CTX_SCHED_THREAD_STACK_SIZE];
static vfs_xipfs_mount_t *_vmp = NULL;


static inline xipfs_mount_t *_mp(void)
{
    return (xipfs_mount_t *)(uintptr_t)&_vmp->magic;
}

static xipfs_file_t *_find_filp(const char *abs_path)
{
    if (!_vmp || !abs_path) return NULL;

    xipfs_mount_t *mp = _mp();
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
        f = xipfs_fs_next(mp, f);
    }
    printf("[ctx] not found: '%s'\n", abs_path);
    return NULL;
}

static void *_task_runner(void *arg)
{
    ctx_task_t *t = (ctx_task_t *)arg;
    printf("[ctx] task '%s' start pid=%d\n", t->path, thread_getpid());
    
    t->state = CTX_TASK_RUNNING;
    
    xipfs_file_exec(_mp(), t->filp, t->argv, (const void **)_syscalls);
    
    t->state = CTX_TASK_DONE;
    printf("[ctx] task '%s' done pid=%d\n", t->path, thread_getpid());
    
    thread_t *sched = thread_get(_sched_pid);
    if (sched) thread_flags_set(sched, CTX_FLAG_TICK);
    
    return NULL;
}


static void *_sched_thread(void *arg)
{
    (void)arg;
    printf("[ctx] sched thread started\n");

    while (1) {
        printf("[ctx] waiting for START...\n");
        thread_flags_wait_any(CTX_FLAG_START);
        printf("[ctx] START received, %d tasks\n", _count);

        ctx_task_t *t     = _head;
        ctx_task_t *start = t;

do {
    if (t->state == CTX_TASK_CREATED) {
        t->tid = thread_create(
            t->stack,
            (int)t->stack_size,
            CTX_SCHED_THREAD_PRIORITY,
            THREAD_CREATE_STACKTEST,
            _task_runner,
            t,
            t->path
        );
        printf("[ctx] launched '%s' tid=%d\n", t->path, t->tid);
        
        while (t->state == CTX_TASK_CREATED) {
            thread_yield();
        }
    }
    t = t->next;
} while (t != start);

        int remaining = _count;
        while (remaining > 0) {
            thread_flags_wait_any(CTX_FLAG_TICK);
            remaining--;
            printf("[ctx] %d task(s) remaining\n", remaining);
        }

        printf("[ctx] all tasks done\n");

        mutex_lock(&_lock);
        memset(_pool,        0, sizeof(_pool));
        memset(_task_stacks, 0, sizeof(_task_stacks));
        _count = 0;
        _head  = NULL;
        mutex_unlock(&_lock);
    }

    return NULL;
}

void ctx_sched_init(vfs_xipfs_mount_t *mp)
{
    _vmp = mp;
    memset(_pool,        0, sizeof(_pool));
    memset(_task_stacks, 0, sizeof(_task_stacks));
    _count = 0;
    _head  = NULL;

    _sched_pid = thread_create(
        _sched_stack, sizeof(_sched_stack),
        CTX_SCHED_THREAD_PRIORITY,
        THREAD_CREATE_STACKTEST | THREAD_CREATE_SLEEPING,
        _sched_thread,
        NULL,
        "ctx_sched"
    );
    printf("[ctx] scheduler created, pid=%d\n", _sched_pid);
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
    t->tid        = KERNEL_PID_UNDEF;

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
    printf("[ctx] starting %d task(s)\n", _count);

    thread_t *t = thread_get(_sched_pid);
    if (!t) {
        printf("[ctx] ERROR: sched thread not found!\n");
        return;
    }
    thread_flags_set(t, CTX_FLAG_START);
    thread_wakeup(_sched_pid);
}

int ctx_exec(const char *path)
{
    if (!path) return -EINVAL;
    printf("[ctx_exec] '%s'\n", path);

    xipfs_file_t *filp = _find_filp(path);
    if (!filp)       { printf("[ctx_exec] not found\n");      return -ENOENT; }
    if (!filp->exec) { printf("[ctx_exec] not executable\n"); return -EACCES; }

    char *argv[] = { (char *)path, NULL };
    int ret = xipfs_file_exec(_mp(), filp, argv, (const void **)_syscalls);
    printf("[ctx_exec] returned %d\n", ret);
    return ret;
}