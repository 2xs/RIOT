#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "thread.h"
#include "list.h"
#include <stdbool.h>

#define THREAD_MANAGER_MAX_TASK  8
#define JOB_STACKSIZE           (THREAD_STACKSIZE_DEFAULT *2)
#define ARGV_MAX                8
#define ARGV_BUF_SIZE           64
#define QUANTUM_MS  2000

typedef struct {
    list_node_t  list_node;
    char         argv_buf[ARGV_MAX][ARGV_BUF_SIZE];
    char        *argv[ARGV_MAX + 1];
    int          argc;
    kernel_pid_t pid;       
    char         stack[JOB_STACKSIZE];
    bool         used;
    bool         state;
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