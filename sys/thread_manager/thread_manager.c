/* sys/thread_manage/thread_manage.c */
#include <stdio.h>
#include "thread.h"
#include "thread_manager.h"

#define THREAD_MANAGER_STACKSIZE     (THREAD_STACKSIZE_DEFAULT)
#define THREAD_MANAGER_PRIORITY      (THREAD_PRIORITY_MAIN + 1)

static char _stack[THREAD_MANAGER_STACKSIZE];

static void *_thread_manager_run(void *arg)
{
    (void)arg;

    puts("[thread_manager] je suis en vie, j'ai la main !");    
    
    while (1) {
        puts("[thread_manager] je tourne ici...");
        thread_sleep();
    }

    return NULL;
}

void thread_manager_init(void)
{
    thread_create(
        _stack,
        sizeof(_stack),
        THREAD_MANAGER_PRIORITY,
        THREAD_CREATE_STACKTEST,
        _thread_manager_run,
        NULL,
        "thread_manager"
    );
}