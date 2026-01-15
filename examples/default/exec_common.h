#ifndef __EXEC_COMMON_H__
#define __EXEC_COMMON_H__

#include "crt0_ctx.h"
#include "xipfs_crt0_ctx_data.h"

#define DEFAULT_STACK_SIZE 1024

#define ROUND(x, y) \
    (((x) + (y) - 1) & ~((y) - 1))

#define THUMB_ADDRESS(x) ((x) | 1)

typedef void (*entryPoint_t)(crt0_ctx_t *crt0_ctx);


/**
 * @warning The order of the members in the enumeration must
 * remain synchronized with the order of the members of the same
 * enumeration declared in caller site (xipfs_format stdriot's one).
 *
 * @brief An enumeration describing the index of functions.
 * @see xipfs_execv
 */
typedef enum xipfs_syscall_e {
    XIPFS_SYSCALL_EXIT,
    XIPFS_SYSCALL_VPRINTF,
    XIPFS_SYSCALL_GET_TEMP,
    XIPFS_SYSCALL_ISPRINT,
    XIPFS_SYSCALL_STRTOL,
    XIPFS_SYSCALL_GET_LED,
    XIPFS_SYSCALL_SET_LED,
    XIPFS_SYSCALL_COPY_FILE,
    XIPFS_SYSCALL_GET_FILE_SIZE,
    XIPFS_SYSCALL_MEMSET,
    XIPFS_SYSCALL_MAX
} xipfs_syscall_t;

extern int get_temp(void);
extern int get_led(int pos);
extern int set_led(int pos, int val);
extern ssize_t copy_file(const char *name, void *buf, size_t nbyte);
extern int get_file_size(const char *name, size_t *size);

#endif /* __EXEC_COMMON_H__ */
