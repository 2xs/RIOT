#ifndef CTX_MANAGER_SCHED_H
#define CTX_MANAGER_SCHED_H

#include <stdint.h>
#include <stdbool.h>
#include "thread.h"
#include "mutex.h"
#include "fs/xipfs_fs.h"
#include "include/file.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= CONFIG ================= */

#ifndef CTX_SCHED_MAX_TASKS
#define CTX_SCHED_MAX_TASKS 4
#endif

#ifndef CTX_SCHED_TASK_STACK_SIZE
#define CTX_SCHED_TASK_STACK_SIZE 2048
#endif

#ifndef CTX_SCHED_THREAD_STACK_SIZE
#define CTX_SCHED_THREAD_STACK_SIZE 4096
#endif

#ifndef CTX_SCHED_TICK_MS
#define CTX_SCHED_TICK_MS 10
#endif

#define CTX_FLAG_START  (1U << 0)
#define CTX_FLAG_TICK   (1U << 1)

/* ================= STATES ================= */

typedef enum {
    CTX_TASK_CREATED = 0,
    CTX_TASK_RUNNING,
    CTX_TASK_READY,
    CTX_TASK_DONE
} ctx_task_state_t;

/* ================= CONTEXT REGISTERS ================= */

typedef struct {
    uint32_t r4_r11[8];
    uint32_t psp;
    uint32_t lr;
    uint32_t control;
#ifdef CTX_SCHED_SAVE_FPU
    uint32_t s16_s31[16];
    uint32_t fpscr;
#endif
} ctx_regs_t;

/* ================= TASK ================= */

typedef struct ctx_task {
    char path[128];
    char *argv[8];

    xipfs_file_t *filp;

    uint8_t *stack;
    size_t stack_size;

    ctx_task_state_t state;
    ctx_regs_t regs;

    struct ctx_task *next;
} ctx_task_t;

/* ================= API ================= */

void ctx_sched_init(void);
void ctx_sched_start(void);
int  ctx_sched_enqueue(const char *path);

#ifdef __cplusplus
}
#endif

#endif