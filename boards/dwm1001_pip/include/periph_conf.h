/*
 * Copyright (C) 2020 Inria
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     boards_dwm1001_pip
 * @{
 *
 * @file
 * @brief       Peripheral configuration for the DWM1001 dev board
 *
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 *
 */

#ifndef PERIPH_CONF_H
#define PERIPH_CONF_H

#include "cfg_clock_32_1.h"
#include "cfg_rtt_default.h"
#include "cfg_timer_default.h"
#include "periph_cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    UART configuration
 * @{
 */
static const uart_conf_t uart_config[] = {
    { /* Mapped to USB virtual COM port */
        .dev        = PIP_NRF_UARTE_UARTE0_BASE,
        .rx_pin     = GPIO_PIN(0, 11),
        .tx_pin     = GPIO_PIN(0, 5),
#ifdef MODULE_PERIPH_UART_HW_FC
        .rts_pin    = GPIO_UNDEF,
        .cts_pin    = GPIO_UNDEF,
#endif
        .irqn       = UARTE0_UART0_IRQn,
    },
};

#define UART_NUMOF          ARRAY_SIZE(uart_config)
#define UART_0_ISR          (isr_uart0)
/** @} */

/**
 * @name    SPI configuration
 * @{
 */
static const spi_conf_t spi_config[] = {
    {
        .dev  = PIP_NRF_SPIM_SPIM0_BASE,
        .sclk = GPIO_PIN(0, 4),
        .mosi = GPIO_PIN(0, 6),
        .miso = GPIO_PIN(0, 7),
        .ppi = 0,
    },
    {   /* Connected to the DWM1001 UWB transceiver */
        .dev  = PIP_NRF_SPIM_SPIM1_BASE,
        .sclk = GPIO_PIN(0, 16),
        .mosi = GPIO_PIN(0, 20),
        .miso = GPIO_PIN(0, 18),
        .ppi = 0,
    },
};

#define SPI_NUMOF           ARRAY_SIZE(spi_config)
/** @} */

/**
 * @name    I2C configuration
 * @{
 */
static const i2c_conf_t i2c_config[] = {
    {
        .dev = PIP_NRF_TWIM_TWIM1_BASE,
        .scl = GPIO_PIN(0, 28),
        .sda = GPIO_PIN(0, 29),
        .speed = I2C_SPEED_NORMAL
    }
};
#define I2C_NUMOF           ARRAY_SIZE(i2c_config)
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* PERIPH_CONF_H */
/** @} */
