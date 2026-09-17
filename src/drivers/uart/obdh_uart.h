#ifndef OBDH_UART_H
#define OBDH_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <rtems.h>

/* USART3 is reserved for the OBDH PUS link: PD8/TX, PD9/RX, 8N1. */
#ifndef OBDH_UART_BAUD_RATE
#define OBDH_UART_BAUD_RATE 115200U
#endif

#ifndef OBDH_UART_TX_GPIO_PORT
#define OBDH_UART_TX_GPIO_PORT 3U /* GPIOD */
#endif

#ifndef OBDH_UART_TX_GPIO_PIN
#define OBDH_UART_TX_GPIO_PIN 8U
#endif

#ifndef OBDH_UART_RX_GPIO_PORT
#define OBDH_UART_RX_GPIO_PORT 3U /* GPIOD */
#endif

#ifndef OBDH_UART_RX_GPIO_PIN
#define OBDH_UART_RX_GPIO_PIN 9U
#endif

#define OBDH_UART_RX_EVENT RTEMS_EVENT_2

/*
 * Transport errors are reported separately from PUS parser errors.  They
 * describe bytes lost before a complete PUS frame can be presented to the
 * parser.
 */
#define OBDH_UART_RX_ERROR_RING_OVERFLOW 0x00000001U
#define OBDH_UART_RX_ERROR_OVERRUN       0x00000002U
#define OBDH_UART_RX_ERROR_NOISE         0x00000004U
#define OBDH_UART_RX_ERROR_FRAMING       0x00000008U
#define OBDH_UART_RX_ERROR_PARITY        0x00000010U

typedef enum {
    OBDH_UART_SUCCESS = 0,
    OBDH_UART_INVALID_ARGUMENT,
    OBDH_UART_NOT_INITIALISED,
    OBDH_UART_TIMEOUT
} obdh_uart_status_t;

typedef struct {
    uint32_t flags;
    uint32_t dropped_bytes;
} obdh_uart_rx_diagnostics_t;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Configures USART3 and installs the RX interrupt handler.  target_task is
 * notified with OBDH_UART_RX_EVENT after one or more bytes are buffered.
 */
rtems_status_code obdh_uart_init(rtems_id target_task);

/* Reads one byte from the ISR-owned RX ring; callable only by target_task. */
bool obdh_uart_read_byte(uint8_t *byte);

/* Atomically returns and clears accumulated RX errors and drop count. */
void obdh_uart_take_rx_diagnostics(obdh_uart_rx_diagnostics_t *diagnostics);

/*
 * Sends one raw PUS frame using HDLC-style 0x7e framing and 0x7d escaping.
 * The caller owns the UART transmitter; this application uses the reports
 * task as its sole transmitter.
 */
obdh_uart_status_t obdh_uart_send_pus_frame(const uint8_t *frame, size_t length);

#ifdef __cplusplus
}
#endif

#endif /* OBDH_UART_H */
