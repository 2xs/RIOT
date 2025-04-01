/*
 * Copyright (C) 2017 HAW Hamburg
 *               2017 Freie Universität Berlin
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_nrf52_pip
 * @{
 *
 * @file
 * @brief       Low-level ADC driver implementation
 *
 * @author      Dimitri Nahm <dimitri.nahm@haw-hamburg.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 *
 * @}
 */

#include <assert.h>

#include "cpu.h"
#include "mutex.h"
#include "periph/adc.h"
#include "periph_conf.h"

#include "svc.h"

/**
 * @name    Default ADC reference, gain configuration and acquisition time
 *
 * Can be overridden by the board configuration if needed. The default
 * configuration uses the full VDD (typically 3V3) as reference and samples for
 * 10us.
 * @{
 */
#ifndef ADC_REF
#define ADC_REF             SAADC_CH_CONFIG_REFSEL_VDD1_4
#endif
#ifndef ADC_GAIN
#define ADC_GAIN            SAADC_CH_CONFIG_GAIN_Gain1_4
#endif
#ifndef ADC_TACQ
#define ADC_TACQ            SAADC_CH_CONFIG_TACQ_10us
#endif
/** @} */

/**
 * @brief   Lock to prevent concurrency issues when used from different threads
 */
static mutex_t lock = MUTEX_INIT;

/**
 * @brief   We use a static result buffer so we do not have to reprogram the
 *          result pointer register
 */
static int16_t result;

static inline void prep(void)
{
    mutex_lock(&lock);
    Pip_out(PIP_NRF_SAADC_SAADC_ENABLE, 1);
}

static inline void done(void)
{
    Pip_out(PIP_NRF_SAADC_SAADC_ENABLE, 0);
    mutex_unlock(&lock);
}

int adc_init(adc_t line)
{
    if (line >= ADC_NUMOF) {
        return -1;
    }

    prep();

    /* prevent multiple initialization by checking the result ptr register */
    if (Pip_in(PIP_NRF_SAADC_SAADC_RESULT_PTR) != (uint32_t)&result) {
        /* set data pointer and the single channel we want to convert */
        Pip_out(PIP_NRF_SAADC_SAADC_RESULT_MAXCNT, 1);
        Pip_out(PIP_NRF_SAADC_SAADC_RESULT_PTR, (uint32_t)&result);

        /* configure the first channel (the only one we use):
         * - bypass resistor ladder+
         * - acquisition time as defined by board (or 10us as default)
         * - reference and gain as defined by board (or VDD as default)
         * - no oversampling */
        Pip_out(PIP_NRF_SAADC_SAADC_CH_0_CONFIG, ((ADC_GAIN << SAADC_CH_CONFIG_GAIN_Pos) |
                                   (ADC_REF << SAADC_CH_CONFIG_REFSEL_Pos) |
                                   (ADC_TACQ << SAADC_CH_CONFIG_TACQ_Pos)));
        Pip_out(PIP_NRF_SAADC_SAADC_CH_0_PSELN, SAADC_CH_PSELN_PSELN_NC);
        Pip_out(PIP_NRF_SAADC_SAADC_OVERSAMPLE, SAADC_OVERSAMPLE_OVERSAMPLE_Bypass);

        /* calibrate SAADC */
        Pip_out(PIP_NRF_SAADC_SAADC_EVENTS_CALIBRATEDONE, 0);
        Pip_out(PIP_NRF_SAADC_SAADC_TASKS_CALIBRATEOFFSET, 1);

        while (Pip_in(PIP_NRF_SAADC_SAADC_EVENTS_CALIBRATEDONE) == 0) {}
    }

    done();

    return 0;
}

int32_t adc_sample(adc_t line, adc_res_t res)
{
    assert(line < ADC_NUMOF);

    /* check if resolution is valid */
    if (res > 2) {
        return -1;
    }

#ifdef SAADC_CH_PSELP_PSELP_VDDHDIV5
    if (line == NRF52_VDDHDIV5) {
        line = SAADC_CH_PSELP_PSELP_VDDHDIV5;
    } else {
        line += 1;
    }
#else
    line += 1;
#endif

    /* prepare device */
    prep();

    /* set resolution */
    Pip_out(PIP_NRF_SAADC_SAADC_RESOLUTION, res);
    /* set line to sample */
    Pip_out(PIP_NRF_SAADC_SAADC_CH_0_PSELP, line);

    /* start the SAADC and wait for the started event */
    Pip_out(PIP_NRF_SAADC_SAADC_EVENTS_STARTED, 0);
    Pip_out(PIP_NRF_SAADC_SAADC_TASKS_START, 1);
    while (Pip_in(PIP_NRF_SAADC_SAADC_EVENTS_STARTED) == 0) {}

    /* trigger the actual conversion */
    Pip_out(PIP_NRF_SAADC_SAADC_EVENTS_END, 0);
    Pip_out(PIP_NRF_SAADC_SAADC_TASKS_SAMPLE, 1);
    while (Pip_in(PIP_NRF_SAADC_SAADC_EVENTS_END) == 0) {}

    /* stop the SAADC */
    Pip_out(PIP_NRF_SAADC_SAADC_EVENTS_STOPPED, 0);
    Pip_out(PIP_NRF_SAADC_SAADC_TASKS_STOP, 1);
    while (Pip_in(PIP_NRF_SAADC_SAADC_EVENTS_STOPPED) == 0) {}

    /* free device */
    done();

    /* hack -> the result can be a small negative number when a AINx pin is
     * connected via jumper wire a the board's GND pin. There seems to be a
     * slight difference between the internal CPU GND and the board's GND
     * voltage levels?! (observed on nrf52dk and nrf52840dk) */
    return (result < 0) ? 0 : (int)result;
}
