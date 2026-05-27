#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include "thread_manager.h"
#include "ztimer.h"
#include "thread.h"
#include "mutex.h"
#include "container.h"
#include "fs/xipfs_fs.h"

static char          _stack[THREAD_STACKSIZE_DEFAULT * 2];
static kernel_pid_t  _tm_pid = KERNEL_PID_UNDEF;
static task_descriptor_t _jobs[THREAD_MANAGER_MAX_TASK];

list_node_t  _runqueue;
mutex_t      _runqueue_mutex = MUTEX_INIT;

static ztimer_t      _quantum_timer;
static list_node_t  *_current = NULL;


kernel_pid_t thread_manager_get_pid(void) { return _tm_pid; }


static task_descriptor_t *_get_free_slot(void)
{
    for (int i = 0; i < THREAD_MANAGER_MAX_TASK; i++) {
        if (!_jobs[i].used) return &_jobs[i];
    }
    return NULL;
}


int thread_manager_add_task(task_descriptor_t *src)
{
    mutex_lock(&_runqueue_mutex);

    task_descriptor_t *slot = _get_free_slot();
    if (!slot) {
        printf("[tm] PLEIN, '%s' refusé\n", src->argv[0]);
        mutex_unlock(&_runqueue_mutex);
        return -1;
    }

    /* copie dans le slot stable */
    slot->argc       = src->argc;
    slot->used       = true;
    slot->is_started = false;
    slot->list_node.next = NULL;

    for (int i = 0; i < src->argc && i < ARGV_MAX; i++) {
        strncpy(slot->argv_buf[i], src->argv[i], ARGV_BUF_SIZE - 1);
        slot->argv_buf[i][ARGV_BUF_SIZE - 1] = '\0';
        slot->argv[i] = slot->argv_buf[i];
    }
    slot->argv[src->argc] = NULL;

    list_add(&_runqueue, &slot->list_node);
    printf("[tm] '%s' ajouté\n", slot->argv[0]);

    mutex_unlock(&_runqueue_mutex);
    return 0;
}

/* ------------------------------------------------------------------ */
static list_node_t *_next_valid_node(list_node_t *from)
{
    list_node_t *node = from;
    while (node) {
        task_descriptor_t *t = container_of(node, task_descriptor_t, list_node);
        if (t->used) return node;
        list_node_t *next = node->next;
        list_remove(&_runqueue, node);
        node = next;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
void switch_ctx(ctx_t *old, ctx_t *next)
{
    __asm__ volatile(
        "cmp r0, #0         \n"
        "beq 1f             \n"
        "str r4,  [r0, #4]  \n"
        "str r5,  [r0, #8]  \n"
        "str r6,  [r0, #12] \n"
        "str r7,  [r0, #16] \n"
        "str r8,  [r0, #20] \n"
        "str r9,  [r0, #24] \n"
        "str r10, [r0, #28] \n"
        "str r11, [r0, #32] \n"
        "str lr,  [r0, #36] \n"
        "mov r2,  sp        \n"
        "str r2,  [r0, #0]  \n"
        "1:                 \n"
        "ldr r2,  [r1, #0]  \n"
        "mov sp,  r2        \n"
        "ldr r4,  [r1, #4]  \n"
        "ldr r5,  [r1, #8]  \n"
        "ldr r6,  [r1, #12] \n"
        "ldr r7,  [r1, #16] \n"
        "ldr r8,  [r1, #20] \n"
        "ldr r9,  [r1, #24] \n"
        "ldr r10, [r1, #28] \n"
        "ldr r11, [r1, #32] \n"
        "ldr lr,  [r1, #36] \n"
        :
        : "r"(old), "r"(next)
        : "memory","r2","r4","r5","r6","r7","r8","r9","r10","r11","lr"
    );
}


void task_resume(task_descriptor_t *old_task, task_descriptor_t *next_task)
{
    if (!next_task->is_started) {
        next_task->is_started = true;

        printf("[task_resume] lancement '%s'\n", next_task->argv[0]);

        
        int ret = xipfs_extended_driver_execv(
            next_task->argv[0],             
            (char *const *)next_task->argv   
        );

        if (ret < 0) {
            printf("[task_resume] erreur : %d\n", ret);
        }

        /* tâche terminée */
        mutex_lock(&_runqueue_mutex);
        list_remove(&_runqueue, &next_task->list_node);
        next_task->used = false;
        mutex_unlock(&_runqueue_mutex);

        printf("[task_resume] '%s' terminée\n", next_task->argv[0]);

    } else {
        ctx_t *old_ctx = old_task ? &old_task->ctx : NULL;
        switch_ctx(old_ctx, &next_task->ctx);
    }
}


static void _scheduler_yield(void *arg)
{
    (void)arg;
    thread_wakeup(_tm_pid);
    ztimer_set(ZTIMER_MSEC, &_quantum_timer, QUANTUM_MS);
}

/* ------------------------------------------------------------------ */
static void *_thread_manager_run(void *arg)
{
    (void)arg;

    _quantum_timer.callback = _scheduler_yield;
    _quantum_timer.arg      = NULL;
    ztimer_set(ZTIMER_MSEC, &_quantum_timer, QUANTUM_MS);

    puts("[tm] started");

    while (1) {
        thread_sleep();

        mutex_lock(&_runqueue_mutex);

        if (_runqueue.next == NULL) {
            mutex_unlock(&_runqueue_mutex);
            continue;
        }

        if (_current == NULL)
            _current = _runqueue.next;
        else
            _current = _current->next;

        if (_current == NULL)
            _current = _runqueue.next;

        _current = _next_valid_node(_current);

        if (_current == NULL) {
            mutex_unlock(&_runqueue_mutex);
            continue;
        }

        task_descriptor_t *task = container_of(
            _current, task_descriptor_t, list_node);

        printf("[sched] %s\n", task->argv[0]);
        mutex_unlock(&_runqueue_mutex);

        task_resume(NULL, task);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
void thread_manager_init(void)
{
    memset(_jobs, 0, sizeof(_jobs));
    _runqueue.next = NULL;
    _current       = NULL;

    _tm_pid = thread_create(
        _stack,
        sizeof(_stack),
        THREAD_PRIORITY_MAIN - 1,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "tm"
    );

    printf("[tm] créé PID=%d\n", _tm_pid);
}