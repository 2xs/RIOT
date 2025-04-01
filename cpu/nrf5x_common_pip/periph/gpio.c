/*
 * Copyright (C) 2015 Jan Wagner <mail@jwagner.eu>
 *               2015-2016 Freie Universität Berlin
 *               2019 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_nrf5x_common_pip
 * @ingroup     drivers_periph_gpio
 * @{
 *
 * @file
 * @brief       Low-level GPIO driver implementation
 *
 * @note        This GPIO driver implementation supports only one pin to be
 *              defined as external interrupt.
 *
 * @author      Christian Kühling <kuehling@zedat.fu-berlin.de>
 * @author      Timo Ziegler <timo.ziegler@fu-berlin.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Jan Wagner <mail@jwagner.eu>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 * @}
 */

#include <assert.h>

#include "cpu.h"
#include "periph/gpio.h"
#include "periph_cpu.h"
#include "periph_conf.h"
#include "svc.h"

#define PORT_BIT            (1 << 5)
#define PIN_MASK            (0x1f)

/* Compatibility wrapper defines for nRF9160 */
#ifdef NRF_P0_S
#define NRF_P0 NRF_P0_S
#endif

#ifdef NRF_P1_S
#define NRF_P1 NRF_P1_S
#endif

#ifdef NRF_GPIOTE0_S
#define NRF_GPIOTE NRF_GPIOTE0_S
#define GPIOTE_IRQn GPIOTE0_IRQn
#endif

#ifdef MODULE_PERIPH_GPIO_IRQ

#if CPU_FAM_NRF51
#define GPIOTE_CHAN_NUMOF     (4U)
#else
#define GPIOTE_CHAN_NUMOF     (8U)
#endif

/**
 * @brief   Index of next interrupt in GPIOTE channel list.
 *
 * The index is incremented at the end of each call to gpio_init_int.
 * The index cannot be greater or equal than GPIOTE_CHAN_NUMOF.
 */
static uint8_t _gpiote_next_index = 0;

/**
 * @brief   Array containing a mapping between GPIOTE channel and pin
 */
static gpio_t _exti_pins[GPIOTE_CHAN_NUMOF];

/**
 * @brief   Place to store the interrupt context
 */
static gpio_isr_ctx_t exti_chan[GPIOTE_CHAN_NUMOF];
#endif /* MODULE_PERIPH_GPIO_IRQ */

/**
 * @brief   Get the port's base id
 */
static inline uint32_t port(gpio_t pin)
{
#if (CPU_FAM_NRF51)
    (void) pin;
    return NRF_GPIO;
#elif defined(NRF_P1)
    return (pin & PORT_BIT) ? NRF_P1 : NRF_P0;
#else
    (void) pin;
    return PIP_NRF_GPIO_P0_BASE;
#endif
}

/**
 * @brief   Get a pin's offset
 */
static inline int pin_num(gpio_t pin)
{
#if GPIO_COUNT > 1
    return (pin & PIN_MASK);
#else
    return (int)pin;
#endif
}

int gpio_init(gpio_t pin, gpio_mode_t mode)
{
    switch (mode) {
        case GPIO_IN:
        case GPIO_IN_PD:
        case GPIO_IN_PU:
        case GPIO_IN_OD_PU:
        case GPIO_OUT:
            /* configure pin direction, input buffer, pull resistor state
             * and drive configuration */
            Pip_out(port(pin) + PIP_NRF_GPIO_P0_PIN_CNF_0_INDEX + pin_num(pin), mode);
            break;
        default:
            return -1;
    }

    return 0;
}

bool gpio_read(gpio_t pin)
{
    if (Pip_in(port(pin) + PIP_NRF_GPIO_P0_DIR_INDEX) & (1 << pin_num(pin))) {
        return (Pip_in(port(pin) + PIP_NRF_GPIO_P0_OUT_INDEX) & (1 << pin_num(pin))) ? 1 : 0;
    }
    else {
        return (Pip_in(port(pin) + PIP_NRF_GPIO_P0_IN_INDEX) & (1 << pin_num(pin))) ? 1 : 0;
    }
}

void gpio_set(gpio_t pin)
{
    Pip_out(port(pin) + PIP_NRF_GPIO_P0_OUTSET_INDEX, (1 << pin_num(pin)));
}

void gpio_clear(gpio_t pin)
{
    Pip_out(port(pin) + PIP_NRF_GPIO_P0_OUTCLR_INDEX, (1 << pin_num(pin)));
}

void gpio_toggle(gpio_t pin)
{
    Pip_out(port(pin) + PIP_NRF_GPIO_P0_OUT_INDEX,
        Pip_in(port(pin) + PIP_NRF_GPIO_P0_OUT_INDEX) ^ (1 << pin_num(pin)));
}

void gpio_write(gpio_t pin, bool value)
{
    if (value) {
        Pip_out(port(pin) + PIP_NRF_GPIO_P0_OUTSET_INDEX, (1 << pin_num(pin)));
    }
    else {
        Pip_out(port(pin) + PIP_NRF_GPIO_P0_OUTCLR_INDEX, (1 << pin_num(pin)));
    }
}

#ifdef MODULE_PERIPH_GPIO_IRQ
uint8_t gpio_int_get_exti(gpio_t pin)
{
    /* Looking for already known pin in exti table */
    for (unsigned int i = 0; i < _gpiote_next_index; i++) {
        if (_exti_pins[i] == pin) {
            return i;
        }
    }
    return 0xff;
}

int gpio_init_int(gpio_t pin, gpio_mode_t mode, gpio_flank_t flank,
                  gpio_cb_t cb, void *arg)
{
    uint8_t _pin_index = gpio_int_get_exti(pin);

    /* New pin */
    if (_pin_index == 0xff) {
        assert(_gpiote_next_index < GPIOTE_CHAN_NUMOF);
        _pin_index = _gpiote_next_index;
        /* associate the current pin with channel index */
        _exti_pins[_pin_index] = pin;
        /* increase next index for next pin initialization */
        _gpiote_next_index++;
    }

    /* save callback */
    exti_chan[_pin_index].cb = cb;
    exti_chan[_pin_index].arg = arg;
    /* configure pin as input */
    gpio_init(pin, mode);
    /* set interrupt priority and enable global GPIOTE interrupt */
    NVIC_EnableIRQ(GPIOTE_IRQn);
    /* configure the GPIOTE channel: set even mode, pin and active flank */
    Pip_out(PIP_NRF_GPIOTE_GPIOTE_CONFIG_0 + _pin_index, (GPIOTE_CONFIG_MODE_Event |
                             (pin_num(pin) << GPIOTE_CONFIG_PSEL_Pos) |
#if GPIO_COUNT > 1
                             ((pin & PORT_BIT) << 8) |
#endif
                             (flank << GPIOTE_CONFIG_POLARITY_Pos)));
    /* enable external interrupt */
    Pip_out(PIP_NRF_GPIOTE_GPIOTE_INTENSET, (GPIOTE_INTENSET_IN0_Msk << _pin_index));

    return 0;
}

void gpio_irq_enable(gpio_t pin)
{
    for (unsigned int i = 0; i < _gpiote_next_index; i++) {
        if (_exti_pins[i] == pin) {
            Pip_out(PIP_NRF_GPIOTE_GPIOTE_CONFIG_0 + i,
                Pip_in(PIP_NRF_GPIOTE_GPIOTE_CONFIG_0 + i) | GPIOTE_CONFIG_MODE_Event);

            Pip_out(PIP_NRF_GPIOTE_GPIOTE_INTENSET, (GPIOTE_INTENSET_IN0_Msk << i));
            break;
        }
    }
}

void gpio_irq_disable(gpio_t pin)
{
    for (unsigned int i = 0; i < _gpiote_next_index; i++) {
        if (_exti_pins[i] == pin) {
            /* Clear mode configuration: 00 = Disabled */
            Pip_out(PIP_NRF_GPIOTE_GPIOTE_CONFIG_0 + i,
                Pip_in(PIP_NRF_GPIOTE_GPIOTE_CONFIG_0 + i) & ~(GPIOTE_CONFIG_MODE_Msk));
            Pip_out(PIP_NRF_GPIOTE_GPIOTE_INTENCLR, (GPIOTE_INTENCLR_IN0_Msk << i));
            break;
        }
    }
}

void ISR_GPIOTE(void)
{
    for (unsigned int i = 0; i < _gpiote_next_index; ++i) {
        if (Pip_in(PIP_NRF_GPIOTE_GPIOTE_EVENTS_IN_0 + i) == 1) {
            Pip_out(PIP_NRF_GPIOTE_GPIOTE_EVENTS_IN_0 + i, 0);
            exti_chan[i].cb(exti_chan[i].arg);
            break;
        }
    }
    cortexm_isr_end();
}
#endif /* MODULE_PERIPH_GPIO_IRQ */
