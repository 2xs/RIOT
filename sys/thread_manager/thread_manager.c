#include <stdio.h>
//#include "thread.h"
#include "thread_manager.h"
#include "msg.h"

#define THREAD_MANAGER_STACKSIZE     (THREAD_STACKSIZE_DEFAULT)
#define THREAD_MANAGER_PRIORITY      (THREAD_PRIORITY_MAIN - 1)

static char _stack[THREAD_MANAGER_STACKSIZE];


#define QUEUE_SIZE 8
static msg_t _msg_queue[QUEUE_SIZE];


static kernel_pid_t _tm_pid = KERNEL_PID_UNDEF;

kernel_pid_t thread_manager_get_pid(void)
{
    return _tm_pid;
}

static void *_thread_manager_run(void *arg)
{
    (void)arg;
    msg_t msg;

    msg_init_queue(_msg_queue, QUEUE_SIZE);

    puts("[thread_manager] prêt à recevoir des taches");

    while (1) {
        msg_receive(&msg);

        
        char *task = (char *)msg.content.ptr;
        printf("[thread_manager] j'exécute la tache: %s\n", task);
    }

    return NULL;
}
void thread_manager_init(void)
{
    _tm_pid = thread_create(
        _stack,
        sizeof(_stack),
        THREAD_MANAGER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "thread_manager"
    );
}