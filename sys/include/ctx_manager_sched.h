#ifndef CTX_MANAGER_SCHED_H
#define CTX_MANAGER_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include "thread.h"
#include "mutex.h"
#include "fs/xipfs_fs.h"
#include "include/file.h"
#include "include/xipfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CTX_SCHED_MAX_TASKS
#define CTX_SCHED_MAX_TASKS        4
#endif
#ifndef CTX_SCHED_TASK_STACK_SIZE
#define CTX_SCHED_TASK_STACK_SIZE  2048
#endif
#ifndef CTX_SCHED_THREAD_STACK_SIZE
#define CTX_SCHED_THREAD_STACK_SIZE 4096
#endif
#ifndef CTX_SCHED_TICK_MS
#define CTX_SCHED_TICK_MS          100
#endif

#define CTX_FLAG_START  (1U << 0)
#define CTX_FLAG_TICK   (1U << 1)

typedef enum {
    CTX_TASK_CREATED = 0,
    CTX_TASK_RUNNING,
    CTX_TASK_READY,
    CTX_TASK_DONE
} ctx_task_state_t;

typedef struct {
    uint32_t r4_r11[8];
    uint32_t psp;
    uint32_t lr;
} ctx_regs_t;

typedef struct ctx_task {
    char             path[128];
    char            *argv[8];
    xipfs_file_t    *filp;
    uint8_t         *stack;
    size_t           stack_size;
    ctx_task_state_t state;
    ctx_regs_t       regs;
    struct ctx_task *next;
} ctx_task_t;

void ctx_sched_init   (vfs_xipfs_mount_t *mp);
int  ctx_sched_enqueue(const char *path);
void ctx_sched_start  (void);
int  ctx_exec         (const char *path);

int  ctx_save         (ctx_regs_t *r);
void ctx_restore      (ctx_regs_t *r);

#ifdef __cplusplus
}
#endif
#endif