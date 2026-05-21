#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "thread_manager.h"
#include "msg.h"
#include "ztimer.h"
#include "mutex.h"

#define THREAD_MANAGER_STACKSIZE  (THREAD_STACKSIZE_DEFAULT)
#define SCHEDULER_STACKSIZE       (THREAD_STACKSIZE_DEFAULT)
#define THREAD_MANAGER_PRIORITY   (THREAD_PRIORITY_MAIN + 1)
#define SCHEDULER_PRIORITY  (THREAD_PRIORITY_MAIN + 2)
#define JOB_PRIORITY        (THREAD_PRIORITY_MAIN + 3)
#define QUEUE_SIZE                THREAD_MANAGER_MAX_TASK

static char             _stack[THREAD_MANAGER_STACKSIZE];
static char              _scheduler_stack[SCHEDULER_STACKSIZE];

static msg_t            _msg_queue[QUEUE_SIZE];
static kernel_pid_t     _tm_pid = KERNEL_PID_UNDEF;
static task_descriptor_t _jobs[THREAD_MANAGER_MAX_TASK];

list_node_t runqueue;
static mutex_t runqueue_mutex = MUTEX_INIT;

kernel_pid_t thread_manager_get_pid(void) { return _tm_pid; }

static task_descriptor_t *_get_free_slot(void)
{
    for (int i = 0; i < THREAD_MANAGER_MAX_TASK; i++) {
        if (!_jobs[i].used) 
        {
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


static void *_job_run(void *arg)
{
    task_descriptor_t *task = (task_descriptor_t *)arg;

    printf("[job:%s] je demarre la tache\n", task->argv[0]);

    thread_sleep();

    int ret = _execute_file_handler(task->argc, task->argv);
    if (ret < 0) {
        printf("[job:%s] erreur : %d\n", task->argv[0], ret);
    }


    printf("[job:%s] je termine la tache, je dors 5 secondes pour simuler du travail\n", task->argv[0]);
    ztimer_sleep(ZTIMER_MSEC, 5000);


    printf("[job:%s] terminé\n", task->argv[0]);

    mutex_lock(&runqueue_mutex);
    task->used = 0;
    mutex_unlock(&runqueue_mutex);
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

        printf("[thread_manager] job reçu : %s (%d args)\n",
               incoming->argv[0], incoming->argc);

        mutex_lock(&runqueue_mutex);

        task_descriptor_t *slot = _get_free_slot();
        if (!slot) {
            mutex_unlock(&runqueue_mutex);
            printf("[thread_manager] Le slot est plein pour le job '%s', refuse\n",
                   incoming->argv[0]);
            continue;
        }

        
        _copy_task(slot, incoming);
        slot->used = true;
        slot->state = false;

        list_add(&runqueue, &slot->list_node);
        mutex_unlock(&runqueue_mutex);

        kernel_pid_t pid = thread_create(
            slot->stack,
            sizeof(slot->stack),
            JOB_PRIORITY,
            THREAD_CREATE_STACKTEST,
            _job_run,
            slot,
            slot->argv[0]
        );

        mutex_lock(&runqueue_mutex);
        slot->pid = pid;
        mutex_unlock(&runqueue_mutex);

        printf("[thread_manager] thread créé pour '%s'\n", slot->argv[0]);
    }

    return NULL;
}

static void *_thread_manager_scheduler_run(void *arg)
{
    (void)arg;

    while (1) {

        mutex_lock(&runqueue_mutex);
        list_node_t *node = runqueue.next;
        mutex_unlock(&runqueue_mutex);

        if (node == NULL) {
            ztimer_sleep(ZTIMER_MSEC, 100);
            continue;
        }

        while (node) {
            mutex_lock(&runqueue_mutex);
            list_node_t *next = node->next; 
            task_descriptor_t *task = (task_descriptor_t *)(void *) ((char *)next - offsetof(task_descriptor_t, list_node));
             

            if (!task->used) {
                list_remove(&runqueue, node);
                mutex_unlock(&runqueue_mutex);
                node = next;
                continue;
            }


            kernel_pid_t pid = task->pid;
            mutex_unlock(&runqueue_mutex);

            
            printf("[scheduler] quantum → job %s (PID=%d)\n",task->argv[0], pid);
            ztimer_sleep(ZTIMER_MSEC, 5000);
            
            thread_wakeup(pid);
            ztimer_sleep(ZTIMER_MSEC, QUANTUM_MS);

    
            node = next;
        }
    }
    return NULL;
}


void thread_manager_init(void)
{
    memset(_jobs, 0, sizeof(_jobs));
    runqueue.next = &runqueue;

    _tm_pid = thread_create(
        _stack,
        sizeof(_stack),
        THREAD_MANAGER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "thread_manager"
    );

    thread_create(
        _scheduler_stack,
        sizeof(_scheduler_stack),
        SCHEDULER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_scheduler_run,
        NULL,
        "thread_manager_scheduler"
    );

    printf("[thread_manager] initialisation, PID=%d\n", _tm_pid);
}