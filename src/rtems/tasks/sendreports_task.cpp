#include <rtems.h>

#include "../../drivers/uart/obdh_uart.h"
#include "../../drivers/telemetry/telemetry.h"
#include "../../drivers/telemetry/adcs_telemetry.h"
#include "../../pus/pus.h"
#include "../../pus/pus_parser.h"
#include "../queues/queues.h"

namespace {

constexpr rtems_interval UART_REINITIALISATION_TICKS = 1000U;
constexpr rtems_interval UART_WORKER_TICKS = 10U;
constexpr uint8_t HDLC_FLAG = 0x7eU;
constexpr uint8_t HDLC_ESCAPE = 0x7dU;
constexpr uint8_t HDLC_ESCAPE_XOR = 0x20U;

struct hdlc_decoder_t {
    uint8_t frame[PUS_FRAME_MAX_SIZE];
    uint16_t length;
    bool in_frame;
    bool escaped;
    bool dropping;
};

void report_receive_error(int error, uint16_t received_size)
{
    pus_packet_t error_packet;

    adcs_build_parser_error_telemetry(&error_packet, error, received_size);
    (void) adcs_send_telemetry(&error_packet);
}

void report_link_error(const obdh_uart_rx_diagnostics_t &diagnostics)
{
    if (diagnostics.flags == 0U && diagnostics.dropped_bytes == 0U) {
        return;
    }

    pus_packet_t error_packet;
    adcs_build_uart_link_error_telemetry(
        &error_packet,
        diagnostics.flags,
        diagnostics.dropped_bytes);
    (void) adcs_send_telemetry(&error_packet);
}

void reset_decoder(hdlc_decoder_t *decoder)
{
    decoder->length = 0U;
    decoder->escaped = false;
    decoder->dropping = false;
}

void finish_decoder_frame(hdlc_decoder_t *decoder)
{
    if (!decoder->in_frame) {
        return;
    }

    if (decoder->escaped || decoder->dropping) {
        report_receive_error(PUS_PARSE_ERROR_TRANSPORT, decoder->length);
    } else if (decoder->length > 0U) {
        pus_handle_rx(decoder->frame, decoder->length);
    }
}

void decode_uart_byte(hdlc_decoder_t *decoder, uint8_t byte)
{
    if (byte == HDLC_FLAG) {
        finish_decoder_frame(decoder);
        decoder->in_frame = true;
        reset_decoder(decoder);
        return;
    }

    if (!decoder->in_frame || decoder->dropping) {
        return;
    }

    if (decoder->escaped) {
        byte ^= HDLC_ESCAPE_XOR;
        decoder->escaped = false;
    } else if (byte == HDLC_ESCAPE) {
        decoder->escaped = true;
        return;
    }

    if (decoder->length >= PUS_FRAME_MAX_SIZE) {
        decoder->dropping = true;
        return;
    }

    decoder->frame[decoder->length++] = byte;
}

void receive_obdh_telecommands(hdlc_decoder_t *decoder)
{
    uint8_t byte = 0U;
    while (obdh_uart_read_byte(&byte)) {
        decode_uart_byte(decoder, byte);
    }
}

} // namespace

/*
 * OBDH telemetry and telecommand task.
 *
 * Every producer (B-dot, housekeeping and PUS parser errors) sends a
 * pus_packet_t to queue_tx.  This is the only UART transmitter; it serialises
 * each raw PUS frame with HDLC-style flag/escape bytes.  The USART RX ISR only
 * buffers bytes and signals this task, so parsing and command execution never
 * run in interrupt context.
 */
extern "C" rtems_task send_reports_task(rtems_task_argument argument)
{
    (void) argument;

    rtems_status_code init_status = RTEMS_UNSATISFIED;
    while (init_status != RTEMS_SUCCESSFUL) {
        init_status = obdh_uart_init(rtems_task_self());
        if (init_status != RTEMS_SUCCESSFUL) {
            /* USART3 is the PUS link; never emit console bytes on this port. */
            rtems_task_wake_after(UART_REINITIALISATION_TICKS);
        }
    }

    hdlc_decoder_t decoder = {};

    while (true) {
        pus_packet_t packet;
        size_t received_size = 0U;
        uint8_t frame[PUS_FRAME_MAX_SIZE];

        rtems_event_set events = 0U;
        const rtems_status_code event_status = rtems_event_receive(
            OBDH_UART_RX_EVENT,
            RTEMS_EVENT_ANY | RTEMS_WAIT,
            UART_WORKER_TICKS,
            &events);

        if (event_status == RTEMS_SUCCESSFUL &&
            (events & OBDH_UART_RX_EVENT) != 0U) {
            receive_obdh_telecommands(&decoder);
        }

        obdh_uart_rx_diagnostics_t diagnostics = {};
        obdh_uart_take_rx_diagnostics(&diagnostics);
        report_link_error(diagnostics);

        const rtems_status_code queue_status = rtems_message_queue_receive(
            queue_tx,
            &packet,
            &received_size,
            RTEMS_NO_WAIT,
            0U);

        if (queue_status == RTEMS_SUCCESSFUL && received_size == sizeof(packet)) {
            const uint16_t frame_size = pus_build_tm(frame, &packet);
            if (frame_size != 0U) {
                (void) obdh_uart_send_pus_frame(frame, frame_size);
            }
        }
    }
}
