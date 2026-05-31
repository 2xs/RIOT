#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void _ctx_first_launch_asm(void);
int ctx_save_asm(void *);
void ctx_restore_asm(void *);

#ifdef __cplusplus
}
#endif