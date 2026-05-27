#ifndef THREAD_MANAGER_H
#define THREAD_MANAGER_H

#include "thread.h"
#include "list.h"
#include "mutex.h"
#include <stdbool.h>
#include <stdint.h>


#define THREAD_MANAGER_MAX_TASK  4
#define ARGV_MAX                 8
#define ARGV_BUF_SIZE            64
#define QUANTUM_MS               2000


typedef struct {
    uint32_t sp, r4, r5, r6, r7, r8, r9, r10, r11, lr;
} ctx_t;

typedef struct {
    list_node_t  list_node;

    char         argv_buf[ARGV_MAX][ARGV_BUF_SIZE];
    char        *argv[ARGV_MAX + 1];
    int          argc;

    kernel_pid_t pid;
    ctx_t        ctx;
    bool         used;
    bool         is_started;
} task_descriptor_t;


extern list_node_t  _runqueue;
extern mutex_t      _runqueue_mutex;


void         thread_manager_init(void);
kernel_pid_t thread_manager_get_pid(void);
int          thread_manager_add_task(task_descriptor_t *src);
void         switch_ctx(ctx_t *old, ctx_t *next);
void         task_resume(task_descriptor_t *old_task, task_descriptor_t *next_task);

#endif