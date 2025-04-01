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
 * @ingroup     drivers_periph_timer
 * @{
 *
 * @file
 * @brief       Implementation of the peripheral timer interface
 *
 * @author      Christian Kühling <kuehling@zedat.fu-berlin.de>
 * @author      Timo Ziegler <timo.ziegler@fu-berlin.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Jan Wagner <mail@jwagner.eu>
 *
 * @}
 */

#include "irq.h"
#include "periph/timer.h"

#include "svc.h"

#define F_TIMER             (16000000U)     /* the timer is clocked at 16MHz */

typedef struct {
    timer_cb_t cb;
    void *arg;
    uint8_t flags;
    uint8_t is_periodic;
} tim_ctx_t;

/**
 * @brief timer state memory
 */
static tim_ctx_t ctx[TIMER_NUMOF];

static inline uint32_t dev(tim_t tim)
{
    return timer_config[tim].dev;
}

uword_t timer_query_freqs_numof(tim_t dev)
{
    assert(dev < TIMER_NUMOF);
    (void)dev;
    return 10;
}

uword_t timer_query_channel_numof(tim_t dev)
{
    assert(dev < TIMER_NUMOF);
    return timer_config[dev].channels;
}

uint32_t timer_query_freqs(tim_t dev, uword_t index)
{
    assert(dev < TIMER_NUMOF);
    (void)dev;
    if (index >= 10) {
        return 0;
    }

    return F_TIMER >> index;
}

int timer_init(tim_t tim, uint32_t freq, timer_cb_t cb, void *arg)
{
    /* make sure the given timer is valid */
    if (tim >= TIMER_NUMOF) {
        return -1;
    }

    /* save interrupt context */
    ctx[tim].cb = cb;
    ctx[tim].arg = arg;

    /* power on timer */
#if CPU_FAM_NRF51
    dev(tim)->POWER = 1;
#endif

    /* reset and configure the timer */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_STOP_INDEX, 1);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_BITMODE_INDEX, timer_config[tim].bitmode);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_MODE_INDEX, TIMER_MODE_MODE_Timer);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_CLEAR_INDEX, 1);

    /* figure out if desired frequency is available */
    int i;
    unsigned long cando = F_TIMER;
    for (i = 0; i < 10; i++) {
        if (freq == cando) {
            Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_PRESCALER_INDEX, i);
            break;
        }
        cando >>= 1;
    }
    if (i == 10) {
        return -1;
    }

    /* reset compare state */
    for(unsigned i = 0; i < timer_config[tim].channels; i++) {
        Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + i, 0);
    }

    /* enable interrupts */
    if (cb != NULL) {
        NVIC_EnableIRQ(timer_config[tim].irqn);
    }
    /* start the timer */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_START_INDEX, 1);

    return 0;
}

int timer_set_absolute(tim_t tim, int chan, unsigned int value)
{
    /* see if channel is valid */
    if (chan >= timer_config[tim].channels) {
        return -1;
    }

    unsigned irq_state = irq_disable();
    ctx[tim].flags |= (1 << chan);
    ctx[tim].is_periodic &= ~(1 << chan);
    irq_restore(irq_state);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_CC_0_INDEX + chan, value);

    /* clear spurious IRQs */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan, 0);
    (void)Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan);

    /* enable IRQ */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_INTENSET_INDEX, (TIMER_INTENSET_COMPARE0_Msk << chan));

    return 0;
}

int timer_set(tim_t tim, int chan, unsigned int timeout)
{
    static const uint32_t max_mask[] = {
        [TIMER_BITMODE_BITMODE_08Bit] = 0x000000ff,
        [TIMER_BITMODE_BITMODE_16Bit] = 0x0000ffff,
        [TIMER_BITMODE_BITMODE_24Bit] = 0x00ffffff,
        [TIMER_BITMODE_BITMODE_32Bit] = 0xffffffff,
    };
    /* see if channel is valid */
    if (chan >= timer_config[tim].channels) {
        return -1;
    }

    unsigned value = timer_read(tim) + timeout;

    unsigned irq_state = irq_disable();
    ctx[tim].flags |= (1 << chan);
    ctx[tim].is_periodic &= ~(1 << chan);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_CC_0_INDEX + chan, value);

    /* clear spurious IRQs */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan, 0);
    (void)Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan);

    /* enable IRQ */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_INTENSET_INDEX, (TIMER_INTENSET_COMPARE0_Msk << chan));

    unsigned expires = value - timer_read(tim);
    expires &= max_mask[timer_config[tim].bitmode];
    if (expires > timeout) {
        /* timer already expired, check if IRQ flag is set */
        if (!Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan)) {
            /* timer has expired but IRQ flag is not set. The only way to not
             * wait *a full period* is now to set a new target to the next tick.
             * (Setting it to the current timer value will not trigger the IRQ
             * flag.) We briefly stop the timer to avoid a race, losing one
             * timer tick in accuracy. But that is better than a timer firing
             * a whole period too late */
            Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_STOP_INDEX, 1);
            Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_CC_0_INDEX + chan, timer_read(tim) + 1);
            Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_START_INDEX, 1);
        }
    }

    irq_restore(irq_state);

    return 0;
}

int timer_set_periodic(tim_t tim, int chan, unsigned int value, uint8_t flags)
{
    /* see if channel is valid */
    if (chan >= timer_config[tim].channels) {
        return -1;
    }

    /* stop timer to avoid race condition */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_STOP_INDEX, 1);

    unsigned irq_state = irq_disable();
    ctx[tim].flags |= (1 << chan);
    ctx[tim].is_periodic |= (1 << chan);
    irq_restore(irq_state);
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_CC_0_INDEX + chan, value);
    if (flags & TIM_FLAG_RESET_ON_MATCH) {
        Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_SHORTS_INDEX,
            Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_SHORTS_INDEX) | (1 << chan));
    }
    if (flags & TIM_FLAG_RESET_ON_SET) {
        Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_CLEAR_INDEX, 1);
    }

    /* clear spurious IRQs */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan, 0);
    (void)Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + chan);

    /* enable IRQ */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_INTENSET_INDEX, (TIMER_INTENSET_COMPARE0_Msk << chan));

    /* re-start timer */
    if (!(flags & TIM_FLAG_SET_STOPPED)) {
        Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_START_INDEX, 1);
    }

    return 0;
}

int timer_clear(tim_t tim, int chan)
{
    /* see if channel is valid */
    if (chan >= timer_config[tim].channels) {
        return -1;
    }

    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_INTENCLR_INDEX, (TIMER_INTENSET_COMPARE0_Msk << chan));
    /* Clear out the Compare->Clear flag of this channel */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_SHORTS_INDEX,
        Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_SHORTS_INDEX) & ~(1 << chan));
    ctx[tim].flags &= ~(1 << chan);
    ctx[tim].is_periodic &= ~(1 << chan);

    return 0;
}

unsigned int timer_read(tim_t tim)
{
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_CAPTURE_0_INDEX + timer_config[tim].channels, 1);
    return Pip_in(dev(tim) + PIP_NRF_TIMER_TIMER1_CC_0_INDEX + timer_config[tim].channels);
}

void timer_start(tim_t tim)
{
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_START_INDEX, 1);
}

void timer_stop(tim_t tim)
{
    /* Errata: [78] TIMER: High current consumption when using
     *                     timer STOP task only
     *
     * # Symptoms
     *
     * Increased current consumption when the timer has been running and the
     * STOP task is used to stop it.
     *
     * # Conditions
     * The timer has been running (after triggering a START task) and then it is
     * stopped using a STOP task only.
     *
     * # Consequences
     *
     * Increased current consumption.
     *
     * # Workaround
     *
     * Use the SHUTDOWN task after the STOP task or instead of the STOP task
     *
     * cf. https://infocenter.nordicsemi.com/pdf/nRF52833_Engineering_A_Errata_v1.4.pdf
     */
    Pip_out(dev(tim) + PIP_NRF_TIMER_TIMER1_TASKS_SHUTDOWN_INDEX, 1);
}

static inline void irq_handler(int num)
{
    for (unsigned i = 0; i < timer_config[num].channels; i++) {
        if (Pip_in(dev(num) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + i) == 1) {
            Pip_out(dev(num) + PIP_NRF_TIMER_TIMER1_EVENTS_COMPARE_0_INDEX + i, 0);
            if (ctx[num].flags & (1 << i)) {
                if ((ctx[num].is_periodic & (1 << i)) == 0) {
                    ctx[num].flags &= ~(1 << i);
                    Pip_out(dev(num) + PIP_NRF_TIMER_TIMER1_INTENCLR_INDEX, (TIMER_INTENSET_COMPARE0_Msk << i));
                }
                ctx[num].cb(ctx[num].arg, i);
            }
        }
    }
    cortexm_isr_end();
}

#ifdef TIMER_0_ISR
void TIMER_0_ISR(void)
{
    irq_handler(0);
}
#endif

#ifdef TIMER_1_ISR
void TIMER_1_ISR(void)
{
    irq_handler(1);
}
#endif

#ifdef TIMER_2_ISR
void TIMER_2_ISR(void)
{
    irq_handler(2);
}
#endif

#ifdef TIMER_3_ISR
void TIMER_3_ISR(void)
{
    irq_handler(3);
}
#endif
