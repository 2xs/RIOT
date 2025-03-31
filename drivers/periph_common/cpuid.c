/*
 * Copyright (C) 2017 Eistec AB
 * Copyright (C) 2014-2016 Freie Universität Berlin
 * Copyright (C) 2015 James Hollister
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     drivers_periph_cpuid
 * @{
 *
 * @file
 * @brief       Generic implementation of the CPUID driver interface
 *
 * @author      Thomas Eichinger <thomas.eichinger@fu-berlin.de>
 * @author      James Hollister <jhollisterjr@gmail.com>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Joakim Nohlgård <joakim.nohlgard@eistec.se>
 *
 * @}
 */

#include <stdint.h>
#include <string.h>

#include "periph/cpuid.h"

typedef struct {
    uint8_t id[CPUID_LEN];
} cpuid_t;

#ifdef CPUID_ADDR
void cpuid_get(void *id)
{
    cpuid_t *dest = id;
    /**
     * Ok, not the best implementation yet, but allows progression in porting riot over Pip-MPU for now
     */
#ifdef NRF52_PIP
    *(uint32_t*)(dest.id + 0) = Pip_in(CPUID_ADDR + 0);
    *(uint32_t*)(dest.id + 4) = Pip_in(CPUID_ADDR + 1);
#else
    const volatile cpuid_t *src = (const void *)CPUID_ADDR;
    *dest = *src;
#endif
}
#endif
