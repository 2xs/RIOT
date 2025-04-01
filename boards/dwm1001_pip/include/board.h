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
 * @brief       Board specific configuration for the DWM1001 dev board
 *
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 */

#ifndef BOARD_H
#define BOARD_H

#include "board_common.h"

#include "svc.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name    LED pin configuration
 * @{
 */
#define LED0_PIN            GPIO_PIN(0, 30)  /**< Green LED on D9 */
#define LED1_PIN            GPIO_PIN(0, 14)  /**< Red LED on D12 */
#define LED2_PIN            GPIO_PIN(0, 22)  /**< Red LED on D11 */
#define LED3_PIN            GPIO_PIN(0, 31)  /**< Blue LED on D10 */

#define LED_PORT            (PIP_NRF_GPIO_P0_BASE)
#define LED0_MASK           (1 << 30)
#define LED1_MASK           (1 << 14)
#define LED2_MASK           (1 << 22)
#define LED3_MASK           (1 << 31)
#define LED_MASK            (LED0_MASK | LED1_MASK | LED2_MASK | LED3_MASK)

#define LED0_ON             Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTCLR_INDEX, LED0_MASK)
#define LED0_OFF            Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTSET_INDEX, LED0_MASK)
#define LED0_TOGGLE         Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX, \
                                Pip_in(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX) ^ LED0_MASK)

#define LED1_ON             Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTCLR_INDEX, LED1_MASK)
#define LED1_OFF            Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTSET_INDEX, LED1_MASK)
#define LED1_TOGGLE         Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX, \
                                Pip_in(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX) ^ LED1_MASK)

#define LED2_ON             Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTCLR_INDEX, LED2_MASK)
#define LED2_OFF            Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTSET_INDEX, LED2_MASK)
#define LED2_TOGGLE         Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX, \
                                Pip_in(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX) ^ LED2_MASK)

#define LED3_ON             Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTCLR_INDEX, LED3_MASK)
#define LED3_OFF            Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUTSET_INDEX, LED3_MASK)
#define LED3_TOGGLE         Pip_out(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX, \
                                Pip_in(LED_PORT + PIP_NRF_GPIO_P0_OUT_INDEX) ^ LED3_MASK)

/** @} */

/**
 * @name    Button pin configuration
 * @{
 */
#define BTN0_PIN            GPIO_PIN(0, 2)
#define BTN0_MODE           GPIO_IN_PU
/** @} */

/**
 * @name    LIS2DH12 driver configuration
 * @{
 */
#define LIS2DH12_PARAM_INT_PIN1     GPIO_PIN(0, 25)
/** @} */

/**
 * @name    DW1000 UWB transceiver
 * @{
 */
#define DW1000_PARAM_SPI            SPI_DEV(1)
#define DW1000_PARAM_CS_PIN         GPIO_PIN(0, 17)
#define DW1000_PARAM_INT_PIN        GPIO_PIN(0, 19)
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
/** @} */
