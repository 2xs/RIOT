#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include "thread_manager.h"
#include "msg.h"
#include "ztimer.h"
#include "mutex.h"
#include "thread.h"


#define THREAD_MANAGER_STACKSIZE  (THREAD_STACKSIZE_DEFAULT)
#define THREAD_MANAGER_PRIORITY   (THREAD_PRIORITY_MAIN - 1)
#define JOB_PRIORITY              (THREAD_PRIORITY_MAIN + 1)
#define QUEUE_SIZE                THREAD_MANAGER_MAX_TASK


static char              _stack[THREAD_MANAGER_STACKSIZE];
static msg_t             _msg_queue[QUEUE_SIZE];
static kernel_pid_t      _tm_pid       = KERNEL_PID_UNDEF;
static task_descriptor_t _jobs[THREAD_MANAGER_MAX_TASK];
static list_node_t       _runqueue;
static mutex_t           _runqueue_mutex = MUTEX_INIT;
static ztimer_t          _quantum_timer;
static list_node_t      *_current_node  = NULL;



kernel_pid_t thread_manager_get_pid(void) { return _tm_pid; }


static task_descriptor_t *_get_free_slot(void)
{
    for (int i = 0; i < THREAD_MANAGER_MAX_TASK; i++) {
        if (!_jobs[i].used) {
            return &_jobs[i];
        }
    }
    return NULL;
}


static void _copy_task(task_descriptor_t *dst, task_descriptor_t *src)
{
    dst->argc = src->argc;
    for (int i = 0; i < src->argc && i < ARGV_MAX; i++) {
        strncpy(dst->argv_buf[i], src->argv[i], ARGV_BUF_SIZE - 1);
        dst->argv_buf[i][ARGV_BUF_SIZE - 1] = '\0';
        dst->argv[i] = dst->argv_buf[i];
    }
    dst->argv[src->argc] = NULL;
}


static list_node_t *_next_valid_node(list_node_t *from)
{
    list_node_t *node = from;

    while (node) {
        task_descriptor_t *task = (task_descriptor_t *)(void *)
            ((char *)node - offsetof(task_descriptor_t, list_node));

        if (task->used) {
            return node;  
        }

        
        list_node_t *next = node->next;
        list_remove(&_runqueue, node);
        node = next;
    }

    return NULL;
}



static void _scheduler_handler(void *arg)
{
    (void)arg;

    mutex_lock(&_runqueue_mutex);

    
    if (_current_node != NULL) {
        task_descriptor_t *current = (task_descriptor_t *)(void *)
            ((char *)_current_node - offsetof(task_descriptor_t, list_node));

        if (current->used && current->pid != KERNEL_PID_UNDEF) {
            printf("[scheduler] suspend '%s' (PID=%d)\n",
                   current->argv[0], current->pid);
            mutex_unlock(&_runqueue_mutex);
            thread_suspend_by_pid(current->pid);
            mutex_lock(&_runqueue_mutex);
        }

        
        _current_node = _current_node->next;
    }

    
    if (_current_node == NULL) {
        _current_node = _runqueue.next;
    }

    
    _current_node = _next_valid_node(_current_node);


    if (_current_node != NULL) {
        task_descriptor_t *next = (task_descriptor_t *)(void *)
            ((char *)_current_node - offsetof(task_descriptor_t, list_node));

        printf("[scheduler] quantum → '%s' (PID=%d)\n",
               next->argv[0], next->pid);

        thread_wakeup(next->pid);
        int i = 0;
        while(i<10000000) { i++; }
        
        ztimer_set(ZTIMER_MSEC, &_quantum_timer, QUANTUM_MS);

    } else {
        
        ztimer_set(ZTIMER_MSEC, &_quantum_timer, 100);
    }

    mutex_unlock(&_runqueue_mutex);
}


static void *_job_run(void *arg)
{
    task_descriptor_t *task = (task_descriptor_t *)arg;

    printf("[job:'%s'] démarré, j'attends le scheduler\n", task->argv[0]);

    
    //thread_sleep();

    printf("[job:'%s'] j'ai la main, j'exécute\n", task->argv[0]);

    int ret = _execute_file_handler(task->argc, task->argv);
    if (ret < 0) {
        printf("[job:'%s'] erreur d'exécution : %d\n", task->argv[0], ret);
    }

    //ztimer_sleep(ZTIMER_MSEC, 3000);
    printf("[job:'%s'] terminé\n", task->argv[0]);

   

    mutex_lock(&_runqueue_mutex);
    task->used = false;
    mutex_unlock(&_runqueue_mutex);

    return NULL;
}



static void *_thread_manager_run(void *arg)
{
    (void)arg;
    msg_t msg;

    msg_init_queue(_msg_queue, QUEUE_SIZE);
    puts("[thread_manager] prêt à recevoir des tâches");

    while (1) {
        msg_receive(&msg);

        task_descriptor_t *incoming = (task_descriptor_t *)msg.content.ptr;
        printf("[thread_manager] job reçu : '%s' (%d args)\n",
               incoming->argv[0], incoming->argc);

        mutex_lock(&_runqueue_mutex);
        task_descriptor_t *slot = _get_free_slot();
        if (!slot) {
            mutex_unlock(&_runqueue_mutex);
            printf("[thread_manager] PLEIN ! job '%s' refusé\n",
                   incoming->argv[0]);
            continue;
        }

        _copy_task(slot, incoming);
        slot->used  = true;
        slot->state = false;
        slot->pid   = KERNEL_PID_UNDEF;

        
        list_add(&_runqueue, &slot->list_node);
        mutex_unlock(&_runqueue_mutex);

        
        kernel_pid_t pid = thread_create(
            slot->stack,
            sizeof(slot->stack),
            JOB_PRIORITY,
            THREAD_CREATE_STACKTEST,
            _job_run,
            slot,
            slot->argv[0]
        );

        mutex_lock(&_runqueue_mutex);
        slot->pid = pid;
        mutex_unlock(&_runqueue_mutex);

        printf("[thread_manager] thread créé pour '%s' (PID=%d)\n",
               slot->argv[0], pid);
    }

    return NULL;
}

void thread_manager_init(void)
{
    memset(_jobs, 0, sizeof(_jobs));
    _runqueue.next = NULL;
    _current_node  = NULL;


    _tm_pid = thread_create(
        _stack,
        sizeof(_stack),
        THREAD_MANAGER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "thread_manager"
    );

    _quantum_timer.callback = _scheduler_handler;
    _quantum_timer.arg      = NULL;
    ztimer_set(ZTIMER_MSEC, &_quantum_timer, QUANTUM_MS);

    puts("[scheduler] timer lancé");
    printf("[thread_manager] init OK, PID=%d\n", _tm_pid);
}