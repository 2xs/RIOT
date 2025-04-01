/*
 * Copyright (C) 2014-2017 Freie Universität Berlin
 *               2015 Jan Wagner <mail@jwagner.eu>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_nrf5x_common_pip
 * @ingroup     drivers_periph_rtt
 * @{
 *
 * @file
 * @brief       RTT implementation for NRF5x CPUs
 *
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Jan Wagner <mail@jwagner.eu>
 *
 * @}
 */

#include "cpu.h"
#include "nrf_clock.h"
#include "periph/rtt.h"
#include "svc.h"

/* get the IRQ configuration */
#ifdef NRF_RTC0_S
#if (RTT_DEV == 0)
#define DEV             NRF_RTC0_S
#define ISR             isr_rtc0
#define IRQn            RTC0_IRQn
#elif (RTT_DEV == 1)
#define DEV             NRF_RTC1_S
#define ISR             isr_rtc1
#define IRQn            RTC1_IRQn
#else
#error "RTT configuration: invalid or no RTC device specified (RTT_DEV)"
#endif
#else
#if (RTT_DEV == 1)
#define DEV             PIP_NRF_RTC_RTC1_BASE
#define ISR             isr_rtc1
#define IRQn            RTC1_IRQn
#elif (RTT_DEV == 2)
#define DEV             PIP_NRF_RTC_RTC2_BASE
#define ISR             isr_rtc2
#define IRQn            RTC2_IRQn
#else
#error "RTT configuration: invalid or no RTC device specified (RTT_DEV)"
#endif
#endif /* def NRF_RTC0_S */

/* allocate memory for callbacks and their args */
static rtt_cb_t alarm_cb;
static void *alarm_arg;
static rtt_cb_t overflow_cb;
static void *overflow_arg;

void rtt_init(void)
{
    /* enable the low-frequency clock */
    clock_start_lf();
    /* make sure device is powered */
#ifdef CPU_FAM_NRF51
    Pip_out(DEV + PIP_NRF_RTC_RTC1_POWER_INDEX, 1);
#endif
    /* stop the RTT during configuration */
    Pip_out(DEV + PIP_NRF_RTC_RTC1_TASKS_STOP_INDEX, 1);
    /* configure interrupt */
    NVIC_EnableIRQ(IRQn);
    /* set prescaler */
    Pip_out(DEV + PIP_NRF_RTC_RTC1_PRESCALER_INDEX, (RTT_CLOCK_FREQUENCY / RTT_FREQUENCY) - 1);
    /* start the actual RTT thing */
    Pip_out(DEV + PIP_NRF_RTC_RTC1_TASKS_START_INDEX, 1);
}

void rtt_set_overflow_cb(rtt_cb_t cb, void *arg)
{
    overflow_cb = cb;
    overflow_arg = arg;
    Pip_out(DEV + PIP_NRF_RTC_RTC1_INTENSET_INDEX, RTC_INTENSET_OVRFLW_Msk);
}

void rtt_clear_overflow_cb(void)
{
    Pip_out(DEV + PIP_NRF_RTC_RTC1_INTENCLR_INDEX, RTC_INTENCLR_OVRFLW_Msk);
}

uint32_t rtt_get_counter(void)
{
    return Pip_in(DEV + PIP_NRF_RTC_RTC1_COUNTER_INDEX);
}

void rtt_set_alarm(uint32_t alarm, rtt_cb_t cb, void *arg)
{
    alarm_cb = cb;
    alarm_arg = arg;
    Pip_out(DEV + PIP_NRF_RTC_RTC1_CC_0_INDEX, (alarm & RTT_MAX_VALUE));
    Pip_out(DEV + PIP_NRF_RTC_RTC1_INTENSET_INDEX, RTC_INTENSET_COMPARE0_Msk);
}

uint32_t rtt_get_alarm(void)
{
    return Pip_in(DEV + PIP_NRF_RTC_RTC1_CC_0_INDEX);
}

void rtt_clear_alarm(void)
{
    Pip_out(DEV + PIP_NRF_RTC_RTC1_INTENCLR_INDEX, RTC_INTENSET_COMPARE0_Msk);
}

void rtt_poweron(void)
{
#ifdef CPU_FAM_NRF51
    Pip_out(DEV + PIP_NRF_RTC_RTC1_POWER_INDEX, 1);
#endif
    Pip_out(DEV + PIP_NRF_RTC_RTC1_TASKS_START_INDEX, 1);
}

void rtt_poweroff(void)
{
    Pip_out(DEV + PIP_NRF_RTC_RTC1_TASKS_STOP_INDEX, 1);
#ifdef CPU_FAM_NRF51
    Pip_out(DEV + PIP_NRF_RTC_RTC1_POWER_INDEX, 0);
#endif
}

void ISR(void)
{
    if (Pip_in(DEV + PIP_NRF_RTC_RTC1_EVENTS_COMPARE_0_INDEX) == 1) {
        Pip_out(DEV + PIP_NRF_RTC_RTC1_EVENTS_COMPARE_0_INDEX, 0);
        Pip_out(DEV + PIP_NRF_RTC_RTC1_INTENCLR_INDEX, RTC_INTENSET_COMPARE0_Msk);
        alarm_cb(alarm_arg);
    }

    if (Pip_in(DEV + PIP_NRF_RTC_RTC1_EVENTS_OVRFLW_INDEX) == 1) {
        Pip_out(DEV + PIP_NRF_RTC_RTC1_EVENTS_OVRFLW_INDEX, 0);
        overflow_cb(overflow_arg);
    }

    cortexm_isr_end();
}
