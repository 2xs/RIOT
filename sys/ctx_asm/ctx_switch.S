.syntax unified
.cpu cortex-m4
.thumb

.global ctx_save_asm
.global ctx_restore_asm

/* ================= SAVE ================= */

ctx_save_asm:
    stmia r0!, {r4-r11}

    mrs r1, psp
    str r1, [r0], #4

    str lr, [r0], #4

    mrs r1, control
    str r1, [r0], #4

    movs r0, #0
    bx lr

/* ================= RESTORE ================= */

ctx_restore_asm:
    ldr r1, [r0, #40]
    msr control, r1
    isb

    ldr r1, [r0, #32]
    msr psp, r1

    ldr lr, [r0, #36]

    ldmia r0!, {r4-r11}

    movs r0, #1
    bx lr