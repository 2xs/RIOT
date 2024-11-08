/*
 * Copyright (C) 2014-2016 Freie Universität Berlin
 *               2015 Jan Wagner <mail@jwagner.eu>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_nrf5x_common_pip
 * @ingroup     drivers_periph_hwrng
 * @{
 *
 * @file
 * @brief       Implementation of the hardware random number generator interface
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Frank Holtz <frank-riot2015@holtznet.de>
 * @author      Jan Wagner <mail@jwagner.eu>
 *
 * @}
 */

#include "cpu.h"
#include "periph/hwrng.h"
#include "assert.h"
#include "svc.h"

void hwrng_init(void)
{
    /* enable bias correction */
    Pip_out(PIP_NRF_RNG_RNG_CONFIG, 1);
}

void hwrng_read(void *buf, unsigned int num)
{
    unsigned int count = 0;
    uint8_t *b = (uint8_t *)buf;

    /* power on RNG */
#ifdef CPU_FAM_NRF51
    NRF_RNG->POWER = 1;
#endif
    Pip_out(PIP_NRF_RNG_RNG_INTENSET, RNG_INTENSET_VALRDY_Msk);
    Pip_out(PIP_NRF_RNG_RNG_TASKS_START, 1);

    /* read the actual random data */
    while (count < num) {
        /* sleep until number is generated */
        while (Pip_in(PIP_NRF_RNG_RNG_EVENTS_VALRDY) == 0) {
            cortexm_sleep_until_event();
        }

        b[count++] = (uint8_t)Pip_in(PIP_NRF_RNG_RNG_VALUE);
        /* NRF51 PAN #21 -> read value before clearing VALRDY */
        Pip_out(PIP_NRF_RNG_RNG_EVENTS_VALRDY, 0);
        NVIC_ClearPendingIRQ(RNG_IRQn);
    }

    /* power off RNG */
    Pip_out(PIP_NRF_RNG_RNG_INTENCLR, RNG_INTENSET_VALRDY_Msk);
    Pip_out(PIP_NRF_RNG_RNG_TASKS_STOP, 1);
#ifdef CPU_FAM_NRF51
    NRF_RNG->POWER = 0;
#endif
}
