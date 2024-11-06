/*
 * Copyright (C) 2014-2017 Freie Universität Berlin
 *               2015 Jan Wagner <mail@jwagner.eu>
 *               2018 Inria
 *               2020 Philipp-Alexander Blum <philipp-blum@jakiku.de>
 *
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @ingroup     cpu_nrf5x_common_pip
 * @ingroup     drivers_periph_uart
 * @{
 *
 * @file
 * @brief       Implementation of the peripheral UART interface
 *
 * @author      Christian Kühling <kuehling@zedat.fu-berlin.de>
 * @author      Timo Ziegler <timo.ziegler@fu-berlin.de>
 * @author      Hauke Petersen <hauke.petersen@fu-berlin.de>
 * @author      Jan Wagner <mail@jwagner.eu>
 * @author      Alexandre Abadie <alexandre.abadie@inria.fr>
 * @author      Philipp-Alexander Blum <philipp-blum@jakiku.de>
 *
 * @}
 */

#include <stdint.h>
#include <string.h>

#include "compiler_hints.h"
#include "cpu.h"
#include "periph/gpio.h"
#include "periph/uart.h"

#include "svc.h"

#ifdef UARTE_PRESENT
#  define PSEL_RXD          PSEL.RXD
#  define PSEL_TXD          PSEL.TXD
#  define PSEL_RTS          PSEL.RTS
#  define PSEL_CTS          PSEL.CTS
#  define ENABLE_ON         UARTE_ENABLE_ENABLE_Enabled
#  define ENABLE_OFF        UARTE_ENABLE_ENABLE_Disabled
#  define UART_TYPE         NRF_UARTE_Type
#else
#  define PSEL_RXD          PSELRXD
#  define PSEL_TXD          PSELTXD
#  define PSEL_RTS          PSELRTS
#  define PSEL_CTS          PSELCTS
#  define ENABLE_ON         UART_ENABLE_ENABLE_Enabled
#  define ENABLE_OFF        UART_ENABLE_ENABLE_Disabled
#  define UART_TYPE         NRF_UART_Type
#endif

#define RAM_MASK        (0x20000000)

/**
 * @brief Chunk size used for transferring data from ROM [in bytes]
 */
#ifndef NRF_UARTE_CHUNK_SIZE
#define NRF_UARTE_CHUNK_SIZE    (32U)
#endif

/**
 * @brief Allocate memory for the interrupt context
 */
static uart_isr_ctx_t isr_ctx[UART_NUMOF];
#ifdef UARTE_PRESENT
static uint8_t rx_buf[UART_NUMOF];
#endif

#ifdef MODULE_PERIPH_UART_NONBLOCKING

#include "tsrb.h"
/**
 * @brief   Allocate for tx ring buffers
 */
static uint8_t tx_buf[UART_NUMOF];
static tsrb_t uart_tx_rb[UART_NUMOF];
static uint8_t uart_tx_rb_buf[UART_NUMOF][UART_TXBUF_SIZE];
#endif

/**
 * @brief Shared IRQ Callback for UART on nRF53/nRF9160
*/
void uart_isr_handler(void *arg);

/* use an enum to count the number of UART ISR macro names defined by the
 * board */
enum {
#ifdef UART_0_ISR
    UART_0_ISR_NUM,
#endif
#ifdef UART_1_ISR
    UART_1_ISR_NUM,
#endif
    UART_ISR_NUMOF,
};

int uart_init(uart_t uart, uint32_t baudrate, uart_rx_cb_t rx_cb, void *arg)
{
/* ensure the ISR names have been defined as needed */
#if !defined(CPU_NRF53) && !defined(CPU_NRF9160)
    static_assert(UART_NUMOF == UART_ISR_NUMOF, "Define(s) of UART ISR name(s) missing");
#endif
    if ((unsigned)uart >= UART_NUMOF) {
        return UART_NODEV;
    }

    uint32_t dev = uart_config[uart].dev;

    /* remember callback addresses and argument */
    isr_ctx[uart].rx_cb = rx_cb;
    isr_ctx[uart].arg = arg;

#ifndef UARTE_PRESENT
    /* only the legacy non-EasyDMA UART needs to be powered on explicitly */
    dev->POWER = 1;
#endif

    /* reset configuration registers */
    Pip_out(dev + PIP_NRF_UART_UART0_CONFIG_INDEX, 0);

    /* configure RX pin */
    if (rx_cb) {
        gpio_init(uart_config[uart].rx_pin, GPIO_IN);
        Pip_out(dev + PIP_NRF_UART_UART0_PSELRXD_INDEX, uart_config[uart].rx_pin);
    }

    /* configure TX pin */
    gpio_init(uart_config[uart].tx_pin, GPIO_OUT);
    Pip_out(dev + PIP_NRF_UART_UART0_PSELTXD_INDEX, uart_config[uart].tx_pin);

    /* enable HW-flow control if defined */
 #ifdef MODULE_PERIPH_UART_HW_FC
    /* set pin mode for RTS and CTS pins */
    if (uart_config[uart].rts_pin != GPIO_UNDEF && uart_config[uart].cts_pin != GPIO_UNDEF) {
        gpio_init(uart_config[uart].rts_pin, GPIO_OUT);
        gpio_init(uart_config[uart].cts_pin, GPIO_IN);
        /* configure RTS and CTS pins to use */
        Pip_out(dev + PIP_NRF_UART_UART0_PSELRTS_INDEX, uart_config[uart].rts_pin);
        Pip_out(dev + PIP_NRF_UART_UART0_PSELCTS_INDEX, uart_config[uart].cts_pin);
        Pip_out(dev + PIP_NRF_UART_UART0_CONFIG_INDEX,
            Pip_in(dev + PIP_NRF_UART_UART0_CONFIG_INDEX) | UART_CONFIG_HWFC_Msk);     /* enable HW flow control */
    }
    else
#endif
    {
        Pip_out(dev + PIP_NRF_UART_UART0_PSELRTS_INDEX, 0xffffffff);   /* pin disconnected */
        Pip_out(dev + PIP_NRF_UART_UART0_PSELCTS_INDEX, 0xffffffff);   /* pin disconnected */
    }

    /* select baudrate */
    switch (baudrate) {
    case 1200:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud1200);
        break;
    case 2400:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud2400);
        break;
    case 4800:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud4800);
        break;
    case 9600:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud9600);
        break;
    case 14400:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud14400);
        break;
    case 19200:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud19200);
        break;
    case 28800:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud28800);
        break;
    case 38400:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud38400);
        break;
    case 57600:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud57600);
        break;
    case 76800:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud76800);
        break;
    case 115200:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud115200);
        break;
    case 230400:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud230400);
        break;
    case 250000:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud250000);
        break;
    case 460800:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud460800);
        break;
    case 921600:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud921600);
        break;
    case 1000000:
        Pip_out(dev + PIP_NRF_UART_UART0_BAUDRATE_INDEX, UARTE_BAUDRATE_BAUDRATE_Baud1M);
        break;
    default:
        return UART_NOBAUD;
    }

    /* enable the UART device */
    Pip_out(dev + PIP_NRF_UART_UART0_ENABLE_INDEX, ENABLE_ON);

#ifdef MODULE_PERIPH_UART_NONBLOCKING
    /* set up the TX buffer */
    tsrb_init(&uart_tx_rb[uart], uart_tx_rb_buf[uart], UART_TXBUF_SIZE);
#endif

    if (rx_cb) {
#ifdef UARTE_PRESENT
        Pip_out(dev + PIP_NRF_UART_UART0_RXD_MAXCNT_INDEX, 1);
        Pip_out(dev + PIP_NRF_UART_UART0_RXD_PTR_INDEX, (uint32_t)&rx_buf[uart]);
        Pip_out(dev + PIP_NRF_UART_UART0_INTENSET_INDEX, UARTE_INTENSET_ENDRX_Msk);
        Pip_out(dev + PIP_NRF_UART_UART0_SHORTS_INDEX,
            Pip_in(dev + PIP_NRF_UART_UART0_SHORTS_INDEX) | UARTE_SHORTS_ENDRX_STARTRX_Msk);
        Pip_out(dev + PIP_NRF_UART_UART0_TASKS_STARTRX_INDEX, 1);
#else
        Pip_out(dev + PIP_NRF_UART_UART0_INTENSET_INDEX, UART_INTENSET_RXDRDY_Msk);
        Pip_out(dev + PIP_NRF_UART_UART0_TASKS_STARTRX_INDEX, 1);
#endif
    }

    if (rx_cb || IS_USED(MODULE_PERIPH_UART_NONBLOCKING)) {
#if  defined(CPU_NRF53) || defined(CPU_NRF9160)
         shared_irq_register_uart(dev, uart_isr_handler, (void *)(uintptr_t)uart);
#else
         NVIC_EnableIRQ(uart_config[uart].irqn);
#endif
    }
    return UART_OK;
}

void uart_poweron(uart_t uart)
{
    assume((unsigned)uart < UART_NUMOF);

    if (isr_ctx[uart].rx_cb) {
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STARTRX_INDEX, 1);
    }
}

void uart_poweroff(uart_t uart)
{
    assume((unsigned)uart < UART_NUMOF);

    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STOPRX_INDEX, 1);
}

/* Unify macro names across nRF51 (UART) and nRF52 and newer (UARTE) */
#if defined(UARTE_CONFIG_HWFC_Msk)
#  define CONFIG_HWFC_Msk UARTE_CONFIG_HWFC_Msk
#elif defined(UART_CONFIG_HWFC_Msk)
#  define CONFIG_HWFC_Msk UART_CONFIG_HWFC_Msk
#endif

#if defined(UARTE_CONFIG_PARITY_Msk)
#  define CONFIG_PARITY_Msk UARTE_CONFIG_PARITY_Msk
#elif defined(UART_CONFIG_PARITY_Msk)
#  define CONFIG_PARITY_Msk UART_CONFIG_PARITY_Msk
#endif

#if defined(UARTE_CONFIG_STOP_Msk)
#  define CONFIG_STOP_Msk UARTE_CONFIG_STOP_Msk
#elif defined(UART_CONFIG_STOP_Msk)
#  define CONFIG_STOP_Msk UART_CONFIG_STOP_Msk
#endif

int uart_mode(uart_t uart, uart_data_bits_t data_bits, uart_parity_t parity,
              uart_stop_bits_t stop_bits)
{
    assume((unsigned)uart < UART_NUMOF);
    /* Not all nRF5x MCUs support 2 stop bits, but the vendor header files
     * reflect the feature set. */
    switch (stop_bits) {
    case UART_STOP_BITS_1:
#ifdef CONFIG_STOP_Msk
    case UART_STOP_BITS_2:
#endif
        break;
    default:
        return UART_NOMODE;
    }

    if (data_bits != UART_DATA_BITS_8) {
        return UART_NOMODE;
    }

    if ((parity != UART_PARITY_NONE) && (parity != UART_PARITY_EVEN)) {
        return UART_NOMODE;
    }

    /* Do not modify hardware flow control */
    uint32_t conf = Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_CONFIG_INDEX) & CONFIG_HWFC_Msk;

#ifdef CONFIG_STOP_Msk
    if (stop_bits == UART_STOP_BITS_2) {
        conf |= UARTE_CONFIG_STOP_Msk;
    }
#endif

    if (parity == UART_PARITY_EVEN) {
        conf |= CONFIG_PARITY_Msk;
    }

    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_CONFIG_INDEX, conf);
    return UART_OK;
}

/* UART with EasyDMA */
#ifdef UARTE_PRESENT
static void _write_buf(uart_t uart, const uint8_t *data, size_t len)
{
    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDTX_INDEX , 0);
    if (IS_USED(MODULE_PERIPH_UART_NONBLOCKING)) {
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_INTENSET_INDEX,
                UARTE_INTENSET_ENDTX_Msk);
    }
    /* set data to transfer to DMA TX pointer */
    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TXD_PTR_INDEX, (uint32_t)data);
    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TXD_MAXCNT_INDEX, len);
    /* start transmission */
    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STARTTX_INDEX, 1);
    /* wait for the end of transmission */
    if (!IS_USED(MODULE_PERIPH_UART_NONBLOCKING)) {
        while (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDTX_INDEX) == 0) {}
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STOPTX_INDEX, 1);
    }
}

void uart_write(uart_t uart, const uint8_t *data, size_t len)
{
    assume((unsigned)uart < UART_NUMOF);
#ifdef MODULE_PERIPH_UART_NONBLOCKING
    for (size_t i = 0; i < len; i++) {
        /* in IRQ or interrupts disabled */
        if (irq_is_in() || __get_PRIMASK()) {
            if (tsrb_full(&uart_tx_rb[uart])) {
                /* wait for end of ongoing transmission */
                if (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXSTARTED_INDEX) {
                    while (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDTX_INDEX) == 0) {}
                    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXSTARTED_INDEX, 0);
                }
                /* free one spot in buffer */
                tx_buf[uart] = tsrb_get_one(&uart_tx_rb[uart]);
                _write_buf(uart, &tx_buf[uart], 1);
            }
            tsrb_add_one(&uart_tx_rb[uart], data[i]);
        }
        else {
            /* if no transmission is ongoing and ring buffer is full
               free up a spot in the buffer by sending one byte */
            if (!Pip_in((uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXSTARTED_INDEX) && tsrb_full(&uart_tx_rb[uart])) {
                tx_buf[uart] = tsrb_get_one(&uart_tx_rb[uart]);
                _write_buf(uart, &tx_buf[uart], 1);
            }
            while (tsrb_add_one(&uart_tx_rb[uart], data[i]) < 0) {}
        }
    }
    /* if no transmission is ongoing bootstrap the transmission process
       by setting a single byte to be written */
    if (!Pip_in((uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXSTARTED_INDEX)) {
        if (!tsrb_empty(&uart_tx_rb[uart])) {
            tx_buf[uart] = tsrb_get_one(&uart_tx_rb[uart]);
            _write_buf(uart, &tx_buf[uart], 1);
        }
    }
#else
    /* EasyDMA can only transfer data from RAM (see ref. manual, sec. 6.34.1).
     * So if the given `data` buffer resides in ROM, we need to copy it to RAM
     * before being able to transfer it. To make sure the stack does not
     * overflow, we do this chunk-wise. */
    if (!((uint32_t)data & RAM_MASK)) {
        size_t pos = 0;
        while (pos < len) {
            uint8_t tmp[NRF_UARTE_CHUNK_SIZE];
            size_t off = len - pos;
            off = (off > NRF_UARTE_CHUNK_SIZE) ? NRF_UARTE_CHUNK_SIZE : off;
            memcpy(tmp, data + pos, off);
            _write_buf(uart, tmp, off);
            pos += off;
        }
    }
    else {
        _write_buf(uart, data, len);
    }
#endif
}

static void irq_handler(uart_t uart)
{
    if (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDRX_INDEX)) {
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDRX_INDEX, 0);

        /* make sure we actually received new data */
        if (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_RXD_AMOUNT_INDEX) != 0) {
            /* Process received byte */
            isr_ctx[uart].rx_cb(isr_ctx[uart].arg, rx_buf[uart]);
        }
    }

#ifdef MODULE_PERIPH_UART_NONBLOCKING
    if (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDTX_INDEX)) {
        /* reset flags and idsable ISR on EVENTS_ENDTX */
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_ENDTX_INDEX, 0);
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXSTARTED_INDEX, 0);
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_INTENCLR_INDEX, UARTE_INTENSET_ENDTX_Msk);
        if (tsrb_empty(&uart_tx_rb[uart])) {
            Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STOPTX_INDEX, 1);
        } else {
            tx_buf[uart] = tsrb_get_one(&uart_tx_rb[uart]);
            _write_buf(uart, &tx_buf[uart], 1);
        }
    }
#endif

    cortexm_isr_end();
}

#else /* UART without EasyDMA*/

void uart_write(uart_t uart, const uint8_t *data, size_t len)
{
    assume((unsigned)uart < UART_NUMOF);

    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STARTTX_INDEX, 1);

    for (size_t i = 0; i < len; i++) {
        /* This section of the function is not thread safe:
            - another thread may mess up with the uart at the same time.
           In order to avoid an infinite loop in the interrupted thread,
           the TXRDY flag must be cleared before writing the data to be
           sent and not after. This way, the higher priority thread will
           exit this function with the TXRDY flag set, then the interrupted
           thread may have not transmitted his data but will still exit the
           while loop.
        */
        /* reset ready flag */
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXDRDY_INDEX, 0);
        /* write data into transmit register */
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TXD_INDEX, data[i]);
        /* wait for any transmission to be done */
        while (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_TXDRDY_INDEX) == 0) {}
    }

    Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_TASKS_STOPTX_INDEX, 1);
}

static void irq_handler(uart_t uart)
{
    if (Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_RXDRDY_INDEX) == 1) {
        Pip_out(uart_config[uart].dev + PIP_NRF_UART_UART0_EVENTS_RXDRDY_INDEX, 0);
        uint8_t byte = (uint8_t)(Pip_in(uart_config[uart].dev + PIP_NRF_UART_UART0_RXD_INDEX) & 0xff);
        isr_ctx[uart].rx_cb(isr_ctx[uart].arg, byte);
    }

    cortexm_isr_end();
}

#endif

#if defined(CPU_NRF53) || defined(CPU_NRF9160)
void uart_isr_handler(void *arg)
{
    uart_t uart = (uart_t)(uintptr_t)arg;

    irq_handler(uart);
}
#else
#ifdef UART_0_ISR
void UART_0_ISR(void)
{
    irq_handler(UART_DEV(0));
}
#endif

#ifdef UART_1_ISR
void UART_1_ISR(void)
{
    irq_handler(UART_DEV(1));
}
#endif

#endif /* def CPU_NRF53 || CPU_NRF9160 */
