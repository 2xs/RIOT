.syntax unified
.cpu cortex-m4
.thumb

.global _ctx_first_launch_asm
.extern xipfs_file_exec
.extern _sched_regs
.extern ctx_restore_asm

_ctx_first_launch_asm:

    push {r4-r7, lr}

    mov r4, sp
    mov r5, r1
    mov r6, r2

    mov sp, r3

    mov r1, r5
    mov r2, r6

    bl xipfs_file_exec

    mov sp, r4

    ldr r0, =_sched_regs
    pop {r4-r7, lr}

    b ctx_restore_asm