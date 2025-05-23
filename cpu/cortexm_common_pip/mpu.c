/*
 * Copyright (C) 2016 Loci Controls Inc.
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_cortexm_common_pip
 * @{
 *
 * @file        mpu.c
 * @brief       Cortex-M Memory Protection Unit (MPU) Driver
 *
 * @author      Ian Martin <ian@locicontrols.com>
 *
 * @}
 */

#include "cpu.h"
#include "mpu.h"

int mpu_disable(void) {
#if __MPU_PRESENT
    return 0;
#else
    return -1;
#endif
}

int mpu_enable(void) {
#if __MPU_PRESENT
    return 0;
#else
    return -1;
#endif
}

bool mpu_enabled(void) {
#if __MPU_PRESENT
    return false;
#else
    return false;
#endif
}

int mpu_configure(uint_fast8_t region, uintptr_t base, uint_fast32_t attr) {
    /* Todo enable MPU support for Cortex-M23/M33 */
#if __MPU_PRESENT && !defined(__ARM_ARCH_8M_MAIN__) && !defined(__ARM_ARCH_8M_BASE__)
    (void)region;
    (void)base;
    (void)attr;
    return 0;
#else
    (void)region;
    (void)base;
    (void)attr;
    return -1;
#endif
}
