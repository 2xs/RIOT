#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "thread_manager.h"
#include "msg.h"

#define THREAD_MANAGER_STACKSIZE  (THREAD_STACKSIZE_DEFAULT * 2)
#define THREAD_MANAGER_PRIORITY   (THREAD_PRIORITY_MAIN - 1)
#define JOB_PRIORITY              (THREAD_PRIORITY_MAIN + 1)
#define QUEUE_SIZE                THREAD_MANAGER_MAX_TASK

static char             _stack[THREAD_MANAGER_STACKSIZE];
static msg_t            _msg_queue[QUEUE_SIZE];
static kernel_pid_t     _tm_pid = KERNEL_PID_UNDEF;
static task_descriptor_t _jobs[THREAD_MANAGER_MAX_TASK];


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

    
    int ret = _execute_file_handler(task->argc, task->argv);
    if (ret < 0) {
        printf("[job:%s] erreur : %d\n", task->argv[0], ret);
    }

    printf("[job:%s] terminé\n", task->argv[0]);
    task->used = 0;
    return NULL;
}

/
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

        task_descriptor_t *slot = _get_free_slot();
        if (!slot) {
            printf("[thread_manager] Le slot est plein pour le job '%s', refuse\n",
                   incoming->argv[0]);
            continue;
        }

        
        _copy_task(slot, incoming);
        slot->used = true;

        thread_create(
            slot->stack,
            sizeof(slot->stack),
            JOB_PRIORITY,
            THREAD_CREATE_STACKTEST,
            _job_run,
            slot,
            slot->argv[0]
        );

        printf("[thread_manager] thread créé pour '%s'\n", slot->argv[0]);
    }

    return NULL;
}


void thread_manager_init(void)
{
    memset(_jobs, 0, sizeof(_jobs));

    _tm_pid = thread_create(
        _stack,
        sizeof(_stack),
        THREAD_MANAGER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "thread_manager"
    );

    printf("[thread_manager] initialisation, PID=%d\n", _tm_pid);
}