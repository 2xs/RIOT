#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "thread.h"
#include <stdbool.h>

#define THREAD_MANAGER_MAX_TASK  4
#define JOB_STACKSIZE           (THREAD_STACKSIZE_DEFAULT * 4)
#define ARGV_MAX                8
#define ARGV_BUF_SIZE           64

typedef struct {
    char   argv_buf[ARGV_MAX][ARGV_BUF_SIZE]; 
    char  *argv[ARGV_MAX + 1];                
    int    argc;
    char   stack[JOB_STACKSIZE];
    bool   used;
} task_descriptor_t;

/**
 * @brief   Initialise et lance le thread thread_manager
 */
void thread_manager_init(void);

/**
 * @brief   Récupère le PID du thread_manager
 * @return  Le PID du thread_manager
 */

kernel_pid_t thread_manager_get_pid(void);

int _execute_file_handler(int argc, char **argv);

#endif 