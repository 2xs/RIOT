#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H
#include "thread.h"

/**
 * @brief   Initialise et lance le thread thread_manager
 */
void thread_manager_init(void);

/**
 * @brief   Récupère le PID du thread_manager
 * @return  Le PID du thread_manager
 */

kernel_pid_t thread_manager_get_pid(void);

#endif 